#include "full_method.hpp"
#include "lshape_relaxed.hpp"
#include "lshape_monotonicity_cut.hpp"
#include "parallel_utils.hpp"
#include "wireless_reform.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void AddFeasibilityCut(GRBModel& model, const BendersCutInfo& cutInfo,
    const std::vector<GRBVar>& masterVars, const std::string& cutName)
{
    GRBLinExpr cutExpr = cutInfo.constant;
    for (size_t idx = 0; idx < masterVars.size(); ++idx)
        cutExpr += cutInfo.yCoeffs[idx] * masterVars[idx];
    if (cutInfo.sense == '>')
        model.addConstr(cutExpr >= cutInfo.rhs, cutName);
    else if (cutInfo.sense == '<')
        model.addConstr(cutExpr <= cutInfo.rhs, cutName);
    else
        throw std::invalid_argument("Unsupported feasibility cut sense");
}

struct SubProblemEvalResult {
    Status status = ERROR;
    BendersCutInfo cutInfo;
    double subObj = 0.0;
};

SubProblemEvalResult EvaluateScenarioSubproblem(const ProblemData& problemData,
    ISubProblem_LShape& strategy, GRBModel& subModel,
    LShapeSubProblemContext& context, const std::vector<double>& zValues,
    bool relaxed)
{
    SubProblemEvalResult result;
    strategy.UpdateSubProblem(problemData, subModel, context, zValues);
    if (relaxed)
        result.status = strategy.SolveRelaxedSubProblem(problemData, subModel, context,
            zValues, result.cutInfo, result.subObj);
    else
        result.status = strategy.SolveSubProblem(problemData, subModel, context,
            zValues, result.cutInfo, result.subObj);
    return result;
}

// Build a simple continuous cut expression for optimality cuts (same as BuildContinuousCut pattern)
void BuildContinuousCutExpr(const BendersCutInfo& cutInfo, const std::vector<GRBVar>& masterVars,
    GRBLinExpr& cutExpr, double& lhsAtCurrent, const std::vector<double>& zCurrent)
{
    cutExpr = cutInfo.constant;
    for (size_t j = 0; j < masterVars.size(); ++j)
        cutExpr += cutInfo.yCoeffs[j] * masterVars[j];
    lhsAtCurrent = cutInfo.constant;
    for (size_t j = 0; j < masterVars.size(); ++j)
        lhsAtCurrent += cutInfo.yCoeffs[j] * zCurrent[j];
}

} // namespace

struct LShapeRelaxedStation::StationSubProblemState {
    int scenarioIndex = 0;
    ScenarioSubproblemInfo scenarioInfo;
    std::unique_ptr<ISubProblem_LShape> strategy;
    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    LShapeSubProblemContext context;
};

LShapeRelaxedStation::LShapeRelaxedStation(ProblemType problemType,
    std::string dataFolder, int maxIters, double tol)
    : problemType(problemType), dataFolder(std::move(dataFolder)),
      maxIters(maxIters), tolerance(tol)
{}

Status LShapeRelaxedStation::Initialize()
{
    if (initialized) return OK;

    fullAlgorithmStart_ = Tools::Clock::now();

    // ── Phase 1: operational relaxation ──
    LShapeRelaxed phase1(problemType, dataFolder, maxIters, tolerance);
    phase1.SetSolverConfig(solverConfig);
    phase1.SetQuiet(true);

    Status s = phase1.Initialize();
    if (s != OK) { std::cerr << "Phase 1 init failed" << std::endl; return s; }

    s = phase1.Solve();
    if (s != OK) { std::cerr << "Phase 1 solve failed" << std::endl; return s; }
    const auto phase1End = Tools::Clock::now();
    phase1ElapsedMs_ = Tools::ElapsedMs(fullAlgorithmStart_, phase1End);
    std::cout << "Phase 1 elapsed time: " << phase1ElapsedMs_ << " ms" << std::endl;
    if (phase1ElapsedMs_ / 1000.0 >= solverConfig.timeLimit) {
        std::cout << "Two-phase reached time limit after Phase 1; skip Phase 2 initialization." << std::endl;
        std::cout << "Total elapsed time (Phase 1 + Phase 2): " << phase1ElapsedMs_ << " ms" << std::endl;
        return ERROR;
    }

    // Capture Phase 1 state before phase1 goes out of scope
    phase1ZValues = phase1.GetIncumbentZValues();
    phase1UpperBound = phase1.GetBestUpperBound();
    env_ = phase1.ReleaseEnv();
    model_ = phase1.GetModel();
    problemDataOwned_ = phase1.ReleaseProblemData();
    problemData_ = problemDataOwned_.get();
    zVars_ = phase1.GetZVars();
    theta_ = model_->getVarByName("theta");

    if (!model_ || !problemData_) {
        std::cerr << "Phase 1 did not produce a valid model" << std::endl;
        return ERROR;
    }

    bestLowerBound_ = phase1.GetBestLowerBound();
    // Phase 1 UB is NOT valid for Phase 2 (different sub-problems have different costs)
    bestUpperBound_ = std::numeric_limits<double>::infinity();

    std::cout << std::endl << "── Phase 2: MIP verification (reusing Phase 1 master + all cuts) ──" << std::endl;

    // ── Build scenario subproblems ──
    phase2Start_ = Tools::Clock::now();

    const auto& scenarioSubproblems = problemData_->getData<std::vector<ScenarioSubproblemInfo>>(
        "wireless_scenario_subproblems");

    if (scenarioSubproblems.empty()) {
        std::cerr << "No scenario subproblems found" << std::endl;
        return ERROR;
    }

    // Add one Phase 2 theta variable per scenario subproblem.
    scenarioThetaVars_.clear();
    scenarioThetaVars_.reserve(scenarioSubproblems.size());
    GRBLinExpr scenarioThetaSum = 0.0;
    for (size_t idx = 0; idx < scenarioSubproblems.size(); ++idx) {
        GRBVar gTheta = model_->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
            "scenario_theta_p2_" + std::to_string(idx));
        scenarioThetaVars_.push_back(gTheta);
        scenarioThetaSum += gTheta;
    }
    model_->addConstr(scenarioThetaSum <= theta_, "theta_scenario_sum_p2");

    subProblems_.clear();
    subProblems_.reserve(scenarioSubproblems.size());

    // ── Pass 1 (serial): Create GRBEnv + GRBModel (license init not thread-safe) ──
    for (size_t idx = 0; idx < scenarioSubproblems.size(); ++idx) {
        std::cout << "Phase 2 init sub-problem " << idx << "/" << scenarioSubproblems.size()
                  << " sc=" << scenarioSubproblems[idx].scenarioIndex
                  << " routes=" << scenarioSubproblems[idx].routeIds.size()
                  << " EBs=" << scenarioSubproblems[idx].ebIndices.size() << std::endl;

        auto sub = std::make_unique<StationSubProblemState>();
        sub->scenarioIndex = scenarioSubproblems[idx].scenarioIndex;
        sub->scenarioInfo = scenarioSubproblems[idx];
        sub->strategy = std::make_unique<WirelessScenarioSubProblem>();
        static_cast<WirelessScenarioSubProblem*>(sub->strategy.get())
            ->SetScenarioSubproblemInfo(scenarioSubproblems[idx]);
        static_cast<WirelessScenarioSubProblem*>(sub->strategy.get())
            ->SetRelaxChargerCapacity(false);

        sub->env = std::make_unique<GRBEnv>(true);
        sub->env->set(GRB_IntParam_OutputFlag, 0);
        sub->env->start();
        sub->model = std::make_unique<GRBModel>(*sub->env);
        sub->model->set(GRB_IntParam_OutputFlag, 0);
        sub->model->set(GRB_DoubleParam_MIPGap, 0.0);
        sub->model->set(GRB_IntParam_Presolve, solverConfig.presolve);
        sub->model->set(GRB_IntParam_Method, solverConfig.method);
        sub->model->set(GRB_IntParam_Threads, ParallelConfig::SubProblemThreads());
        sub->context.Clear();
        subProblems_.push_back(std::move(sub));
    }

    // ── Pass 2 (parallel): InitSubProblem on independent models ──
    {
        const ProblemData* pd = problemData_;
        try {
            RunBoundedIndexedTasks(subProblems_.size(),
                ParallelConfig::MaxSubProblemConcurrency(solverConfig),
                [pd, this](size_t idx) {
                    auto* sub = subProblems_[idx].get();
                    sub->strategy->InitSubProblem(*pd, static_cast<int>(idx),
                        *sub->model, sub->context);
                    return OK;
                });
        }
        catch (const std::exception& e) {
            std::cerr << "Phase 2 sub-problem init failed: " << e.what() << std::endl;
            return ERROR;
        }
        for (size_t idx = 0; idx < subProblems_.size(); ++idx) {
            std::cout << "  done, cols=" << subProblems_[idx]->model->get(GRB_IntAttr_NumVars)
                      << " rows=" << subProblems_[idx]->model->get(GRB_IntAttr_NumConstrs) << std::endl;
        }
    }

    const auto phase2InitEnd = Tools::Clock::now();

    std::cout << "Phase 2 scenario subproblems count=" << subProblems_.size()
              << ", master constraints=" << model_->get(GRB_IntAttr_NumConstrs)
              << ", init_elapsed_ms=" << Tools::ElapsedMs(phase2Start_, phase2InitEnd) << std::endl;

    initialized = true;
    return OK;
}

Status LShapeRelaxedStation::Solve()
{
    if (!initialized || !model_) {
        std::cerr << "Phase 2 not initialized" << std::endl;
        return ERROR;
    }

    // Apply MIP start from Phase 1 solution
    for (size_t i = 0; i < zVars_.size(); ++i)
        zVars_[i].set(GRB_DoubleAttr_Start, phase1ZValues[i]);
    model_->update();

    std::cout << "Phase 2 MIP start applied, starting Benders..." << std::endl;

    const auto& scenarioSubproblems = problemData_->getData<std::vector<ScenarioSubproblemInfo>>(
        "wireless_scenario_subproblems");
    const auto& masterVars = problemData_->getData<std::vector<ProblemDataVar>>("masterVars");
    const auto& facilityVarNames = problemData_->getData<std::vector<std::vector<std::vector<std::string>>>>(
        "wireless_route_facility_var_names");

    for (int iter = 0; iter < maxIters; ++iter) {
        const double elapsedSec = Tools::ElapsedMs(fullAlgorithmStart_, Tools::Clock::now()) / 1000.0;
        if (elapsedSec >= solverConfig.timeLimit) {
            std::cout << "Phase 2 reached time limit: " << solverConfig.timeLimit << "s" << std::endl;
            phase2ElapsedMs_ = Tools::ElapsedMs(phase2Start_, Tools::Clock::now());
            std::cout << "Phase 2 elapsed time: " << phase2ElapsedMs_ << " ms" << std::endl;
            std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                      << (phase1ElapsedMs_ + phase2ElapsedMs_) << " ms" << std::endl;
            return ERROR;
        }

        // ── Master solve ──
        const auto masterStart = Tools::Clock::now();
        model_->optimize();
        const auto masterEnd = Tools::Clock::now();
        const long long masterMs = Tools::ElapsedMs(masterStart, masterEnd);

        int masterStatus = model_->get(GRB_IntAttr_Status);
        if (!(masterStatus == GRB_OPTIMAL ||
              (masterStatus == GRB_TIME_LIMIT && model_->get(GRB_IntAttr_SolCount) > 0))) {
            std::cerr << "Phase 2 master not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }

        double fixedCost = 0.0;
        std::vector<double> zValues(zVars_.size());
        for (size_t i = 0; i < zVars_.size(); ++i) {
            zValues[i] = zVars_[i].get(GRB_DoubleAttr_X);
            fixedCost += zVars_[i].get(GRB_DoubleAttr_Obj) * zValues[i];
        }
        const double thetaValue = theta_.get(GRB_DoubleAttr_X);
        const double masterBound = model_->get(GRB_DoubleAttr_ObjBound);
        bestLowerBound_ = std::max(bestLowerBound_, masterBound);

        // ── RSP: LP relaxation of scenario subproblems ──
        bool rspAnyInfeasible = false;
        {
            // Evaluate LP relaxation for lower-bounding before MIP verification.
            const ProblemData* pd = problemData_;
            std::vector<SubProblemEvalResult> results;
            try {
                results = RunBoundedIndexedTasks(subProblems_.size(),
                    ParallelConfig::MaxSubProblemConcurrency(solverConfig),
                    [pd, &zValues, this](size_t idx) {
                        auto* sub = subProblems_[idx].get();
                        return EvaluateScenarioSubproblem(*pd, *sub->strategy, *sub->model,
                            sub->context, zValues, true); // relaxed = LP relaxation
                    });
            }
            catch (const std::exception& e) {
                std::cerr << "Phase 2 sub-problem failed: " << e.what() << std::endl;
                return ERROR;
            }

            int feasCount = 0;
            int optCount = 0;
            std::set<std::string> addedFeasCuts;

            for (size_t idx = 0; idx < results.size(); ++idx) {
                const SubProblemEvalResult& result = results[idx];
                if (result.status != OK) return result.status;

                if (!result.cutInfo.isOptimalityCut) {
                    // Infeasible LP relaxation �?feasibility cut + strengthened no-good
                    rspAnyInfeasible = true;
                    const std::string sig = MakeFeasibilityCutSignature(result.cutInfo);
                    if (addedFeasCuts.insert(sig).second) {
                        AddFeasibilityCut(*model_, result.cutInfo, zVars_,
                            "p2_feas_" + std::to_string(iter) + "_" + std::to_string(idx));
                        ++feasCount;
                    }
                    // Strengthened no-good cut for the current scenario-subproblem scope.
                    const auto& scenarioInfo = scenarioSubproblems[idx];
                    std::vector<std::string> scenarioFacilityNames;
                    int scIdx = scenarioInfo.scenarioIndex;
                    for (int rid : scenarioInfo.routeIds) {
                        const auto& rn = facilityVarNames.at(static_cast<size_t>(scIdx))
                            .at(static_cast<size_t>(rid));
                        scenarioFacilityNames.insert(scenarioFacilityNames.end(), rn.begin(), rn.end());
                    }
                    const BendersCutInfo noGood = BuildScenarioStrengthenedNoGoodCut(
                        masterVars, zValues, scIdx, scenarioInfo.routeIds, scenarioFacilityNames);
                    const std::string ngSig = MakeFeasibilityCutSignature(noGood);
                    if (addedFeasCuts.insert(ngSig).second) {
                        AddFeasibilityCut(*model_, noGood, zVars_,
                            "p2_nogood_" + std::to_string(iter) + "_" + std::to_string(idx));
                        ++feasCount;
                    }
                } else {
                    // Feasible LP �?optimality cut on scenarioThetaVar
                    if (scenarioThetaVars_[idx].get(GRB_DoubleAttr_X) + tolerance < result.subObj) {
                        GRBLinExpr cutExpr;
                        double lhsAtCurrent;
                        BuildContinuousCutExpr(result.cutInfo, zVars_, cutExpr, lhsAtCurrent, zValues);
                        model_->addConstr(cutExpr <= scenarioThetaVars_[idx],
                            "p2_opt_" + std::to_string(iter) + "_" + std::to_string(idx));
                        ++optCount;
                    }
                }
            }

            if (rspAnyInfeasible) {
                std::cout << "Phase 2 iter " << iter << ", feasibility cut x" << feasCount;
                if (std::isfinite(bestUpperBound_)) {
                    double denom = std::max(1.0, std::fabs(bestUpperBound_));
                    std::cout << ", UB=" << bestUpperBound_ << ", LB=" << bestLowerBound_
                              << ", gap=" << (bestUpperBound_ - bestLowerBound_) / denom * 100.0 << "%";
                }
                model_->update();
                const auto iterEnd = Tools::Clock::now();
                std::cout << ", master_ms=" << masterMs
                          << ", eval_ms=" << Tools::ElapsedMs(masterEnd, iterEnd)
                          << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start_, iterEnd) << std::endl;
                continue;
            }
            if (optCount > 0) model_->update();
        }

        // ── SP: Full MIP sub-problems (for incumbent update + integer optimality cuts) ──
        const ProblemData* pd = problemData_;
        std::vector<SubProblemEvalResult> mipResults;
        try {
            mipResults = RunBoundedIndexedTasks(subProblems_.size(),
                ParallelConfig::MaxSubProblemConcurrency(solverConfig),
                [pd, &zValues, this](size_t idx) {
                    auto* sub = subProblems_[idx].get();
                    return EvaluateScenarioSubproblem(*pd, *sub->strategy, *sub->model,
                        sub->context, zValues, false); // full MIP
                });
        }
        catch (const std::exception& e) {
            std::cerr << "Phase 2 MIP sub-problem failed: " << e.what() << std::endl;
            return ERROR;
        }

        double aggregatedQValue = 0.0;
        bool spAnyInfeasible = false;
        std::vector<BendersCutInfo> mipCutInfos(subProblems_.size());
        std::vector<double> mipSubObjs(subProblems_.size(), 0.0);
        for (size_t idx = 0; idx < mipResults.size(); ++idx) {
            const SubProblemEvalResult& result = mipResults[idx];
            if (result.status != OK) return result.status;

            mipCutInfos[idx] = result.cutInfo;
            mipSubObjs[idx] = result.subObj;
            if (result.cutInfo.isOptimalityCut)
                aggregatedQValue += result.subObj;
            else
                spAnyInfeasible = true;
        }

        if (spAnyInfeasible) {
            // MIP infeasible: add feasibility cut plus strengthened no-good cut.
            int feasCount = 0;
            std::set<std::string> addedFeasCuts;
            for (size_t idx = 0; idx < mipCutInfos.size(); ++idx) {
                if (mipCutInfos[idx].isOptimalityCut) continue;
                const std::string sig = MakeFeasibilityCutSignature(mipCutInfos[idx]);
                if (addedFeasCuts.insert(sig).second) {
                    AddFeasibilityCut(*model_, mipCutInfos[idx], zVars_,
                        "p2_mip_feas_" + std::to_string(iter) + "_" + std::to_string(idx));
                    ++feasCount;
                }
                // Strengthened no-good cut for the current scenario-subproblem scope.
                const auto& scenarioInfo = scenarioSubproblems[idx];
                std::vector<std::string> scenarioFacilityNames;
                int scIdx = scenarioInfo.scenarioIndex;
                for (int rid : scenarioInfo.routeIds) {
                    const auto& rn = facilityVarNames.at(static_cast<size_t>(scIdx))
                        .at(static_cast<size_t>(rid));
                    scenarioFacilityNames.insert(scenarioFacilityNames.end(), rn.begin(), rn.end());
                }
                const BendersCutInfo noGood = BuildScenarioStrengthenedNoGoodCut(
                    masterVars, zValues, scIdx, scenarioInfo.routeIds, scenarioFacilityNames);
                const std::string ngSig = MakeFeasibilityCutSignature(noGood);
                if (addedFeasCuts.insert(ngSig).second) {
                    AddFeasibilityCut(*model_, noGood, zVars_,
                        "p2_mip_nogood_" + std::to_string(iter) + "_" + std::to_string(idx));
                    ++feasCount;
                }
            }
            model_->update();
            std::cout << "Phase 2 iter " << iter << ", MIP infeasible, feasibility cut x" << feasCount;
            if (std::isfinite(bestUpperBound_)) {
                double denom = std::max(1.0, std::fabs(bestUpperBound_));
                std::cout << ", UB=" << bestUpperBound_ << ", LB=" << bestLowerBound_
                          << ", gap=" << (bestUpperBound_ - bestLowerBound_) / denom * 100.0 << "%";
            }
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs
                      << ", mip_eval_ms=" << Tools::ElapsedMs(masterEnd, iterEnd)
                      << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start_, iterEnd) << std::endl;
            continue;
        }

        // Update incumbent
        double candidateObj = fixedCost + aggregatedQValue;
        if (candidateObj + tolerance < bestUpperBound_) {
            bestUpperBound_ = candidateObj;
            incumbentZValues_ = zValues;
            incumbentQ_ = aggregatedQValue;
        }

        std::cout << "Phase 2 iter " << iter << ", theta=" << thetaValue
                  << ", Q(z)=" << aggregatedQValue;
        if (std::isfinite(bestUpperBound_)) {
            double denom = std::max(1.0, std::fabs(bestUpperBound_));
            std::cout << ", UB=" << bestUpperBound_ << ", LB=" << bestLowerBound_
                      << ", gap=" << (bestUpperBound_ - bestLowerBound_) / denom * 100.0 << "%";
        }

        // Add MIP optimality cuts: monotonicity-strengthened for each feasible scenario subproblem.
        const auto& scenarioSubproblems = problemData_->getData<std::vector<ScenarioSubproblemInfo>>(
            "wireless_scenario_subproblems");
        const auto& facilityVarNames = problemData_->getData<
            std::vector<std::vector<std::vector<std::string>>>>("wireless_route_facility_var_names");
        for (size_t idx = 0; idx < mipCutInfos.size(); ++idx) {
            if (!mipCutInfos[idx].isOptimalityCut) continue;
            const auto& scenarioInfo = scenarioSubproblems[idx];
            int scIdx = scenarioInfo.scenarioIndex;
            std::vector<std::string> scenarioFacilityNames;
            for (int rid : scenarioInfo.routeIds) {
                const auto& rn = facilityVarNames.at(static_cast<size_t>(scIdx))
                    .at(static_cast<size_t>(rid));
                scenarioFacilityNames.insert(scenarioFacilityNames.end(), rn.begin(), rn.end());
            }
            const BendersCutInfo optCut = BuildMonotonicityStrengthenedOptimalityCut(
                masterVars, zValues, scIdx, scenarioInfo.routeIds, scenarioFacilityNames,
                mipSubObjs[idx], globalLowerBound_);
            GRBLinExpr cutExpr;
            double lhsAtCurrent;
            BuildContinuousCutExpr(optCut, zVars_, cutExpr, lhsAtCurrent, zValues);
            model_->addConstr(cutExpr <= scenarioThetaVars_[idx],
                "p2_mip_sopt_" + std::to_string(iter) + "_" + std::to_string(idx));
        }
        model_->update();

        // Convergence check �?skip on iter 0 since MIP cuts haven't been
        // re-optimized yet (LB from Phase 1 cuts may be deceptively tight).
        if (iter > 0) {
            double gapDenom = std::max(1.0, std::fabs(bestUpperBound_));
            double relGap = (bestUpperBound_ - bestLowerBound_) / gapDenom;
            if (relGap <= solverConfig.mipGap) {
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs
                      << ", mip_eval_ms=" << Tools::ElapsedMs(masterEnd, iterEnd)
                      << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start_, iterEnd) << std::endl;
            std::cout << "Phase 2 converged." << std::endl;
            std::cout << "UB: " << bestUpperBound_ << ", LB: " << bestLowerBound_ << std::endl;
            if (!incumbentZValues_.empty()) {
                std::cout << "Active master variables:" << std::endl;
                for (size_t i = 0; i < zVars_.size(); ++i) {
                    if (incumbentZValues_[i] > 0.5) {
                        const std::string name = zVars_[i].get(GRB_StringAttr_VarName);
                        if (name.rfind("w_", 0) != 0) continue;
                        std::cout << name << std::endl;
                    }
                }
                std::cout << "Q(z): " << incumbentQ_ << std::endl;
            }
            phase2ElapsedMs_ = Tools::ElapsedMs(phase2Start_, Tools::Clock::now());
            std::cout << "Phase 2 final elapsed time: " << phase2ElapsedMs_ << " ms" << std::endl;
            std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                      << (phase1ElapsedMs_ + phase2ElapsedMs_) << " ms" << std::endl;
            return OK;
            }
        }  // iter > 0

        const auto iterEnd = Tools::Clock::now();
        std::cout << ", master_ms=" << masterMs
                  << ", mip_eval_ms=" << Tools::ElapsedMs(masterEnd, iterEnd)
                  << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start_, iterEnd) << std::endl;
    }

    std::cout << "UB: " << bestUpperBound_ << ", LB: " << bestLowerBound_ << std::endl;
    std::cout << "Phase 2 reached max iterations: " << maxIters << std::endl;
    phase2ElapsedMs_ = Tools::ElapsedMs(phase2Start_, Tools::Clock::now());
    std::cout << "Phase 2 elapsed time: " << phase2ElapsedMs_ << " ms" << std::endl;
    std::cout << "Total elapsed time (Phase 1 + Phase 2): "
              << (phase1ElapsedMs_ + phase2ElapsedMs_) << " ms" << std::endl;
    return ERROR;
}

LShapeRelaxedStation::~LShapeRelaxedStation() = default;
