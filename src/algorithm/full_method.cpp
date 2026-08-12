#include "full_method.hpp"
#include "lshape_relaxed.hpp"
#include "lshape_monotonicity_cut.hpp"
#include "parallel_utils.hpp"
#include "wireless_reform.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
constexpr double kSparseCoeffTol = 1e-12;
constexpr double kCutKeyTol = 1e-9;
constexpr bool kEnablePhase2RSP = true;

namespace fs = std::filesystem;

bool IsRelevantCoeff(double coeff)
{
    return std::fabs(coeff) > kSparseCoeffTol;
}

std::string RouteThetaName(int scenarioIndex, int routeId)
{
    return "theta_scenario_" + std::to_string(scenarioIndex)
        + "_route_" + std::to_string(routeId);
}

void RemovePhase1GlobalRouteThetaLink(GRBModel& model)
{
    model.remove(model.getConstrByName("theta_route_sum"));
}

void AddScenarioRouteThetaLink(GRBModel& model, const ScenarioSubproblemInfo& scenarioInfo,
    const GRBVar& scenarioTheta, size_t phase2Index)
{
    GRBLinExpr routeThetaSum = 0.0;
    for (int routeId : scenarioInfo.routeIds) {
        routeThetaSum += model.getVarByName(RouteThetaName(scenarioInfo.scenarioIndex, routeId));
    }
    model.addConstr(routeThetaSum <= scenarioTheta,
        "theta_route_to_p2_scenario_" + std::to_string(phase2Index));
}

void AddFeasibilityCut(GRBModel& model, const BendersCutInfo& cutInfo,
    const std::vector<GRBVar>& masterVars, const std::string& cutName)
{
    GRBLinExpr cutExpr = cutInfo.constant;
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        const double coeff = cutInfo.yCoeffs[idx];
        if (IsRelevantCoeff(coeff))
            cutExpr += coeff * masterVars[idx];
    }
    if (cutInfo.sense == '>')
        model.addConstr(cutExpr >= cutInfo.rhs, cutName);
    else if (cutInfo.sense == '<')
        model.addConstr(cutExpr <= cutInfo.rhs, cutName);
    else
        throw std::invalid_argument("Unsupported feasibility cut sense");
}

long long QuantizeCutValue(double value)
{
    return static_cast<long long>(std::llround(value / kCutKeyTol));
}

std::vector<int> ExtractBinaryCutTerms(const BendersCutInfo& cutInfo)
{
    std::vector<int> terms;
    for (size_t idx = 0; idx < cutInfo.yCoeffs.size(); ++idx) {
        if (IsRelevantCoeff(cutInfo.yCoeffs[idx])) {
            terms.push_back(static_cast<int>(idx));
        }
    }
    return terms;
}

double ExtractMonotoneDelta(const BendersCutInfo& cutInfo)
{
    for (double coeff : cutInfo.yCoeffs) {
        if (IsRelevantCoeff(coeff)) {
            return std::fabs(coeff);
        }
    }
    return 0.0;
}

struct MonotoneFeasKey {
    char sense = '>';
    long long rhs = 0;
    std::vector<int> terms;

    bool operator<(const MonotoneFeasKey& other) const
    {
        return std::tie(sense, rhs, terms) < std::tie(other.sense, other.rhs, other.terms);
    }
};

struct MonotoneOptKey {
    int thetaScope = -1;
    long long qValue = 0;
    long long delta = 0;
    std::vector<int> terms;

    bool operator<(const MonotoneOptKey& other) const
    {
        return std::tie(thetaScope, qValue, delta, terms)
            < std::tie(other.thetaScope, other.qValue, other.delta, other.terms);
    }
};

MonotoneFeasKey BuildMonotoneFeasKey(const BendersCutInfo& cutInfo)
{
    return MonotoneFeasKey{
        cutInfo.sense,
        QuantizeCutValue(cutInfo.rhs),
        ExtractBinaryCutTerms(cutInfo)
    };
}

MonotoneOptKey BuildMonotoneOptKey(size_t thetaScope, const BendersCutInfo& cutInfo)
{
    return MonotoneOptKey{
        static_cast<int>(thetaScope),
        QuantizeCutValue(cutInfo.constant),
        QuantizeCutValue(ExtractMonotoneDelta(cutInfo)),
        ExtractBinaryCutTerms(cutInfo)
    };
}

struct SubProblemEvalResult {
    Status status = ERROR;
    BendersCutInfo cutInfo;
    double subObj = 0.0;
    int grbStatus = -1;
    long long solveMs = 0;
};

SubProblemEvalResult EvaluateScenarioSubproblem(const ProblemData& problemData,
    ISubProblem_LShape& strategy, GRBModel& subModel,
    LShapeSubProblemContext& context, const std::vector<double>& zValues,
    bool relaxed)
{
    SubProblemEvalResult result;
    const auto solveStart = Tools::Clock::now();
    strategy.UpdateSubProblem(problemData, subModel, context, zValues);
    if (relaxed)
        result.status = strategy.SolveRelaxedSubProblem(problemData, subModel, context,
            zValues, result.cutInfo, result.subObj);
    else
        result.status = strategy.SolveSubProblem(problemData, subModel, context,
            zValues, result.cutInfo, result.subObj);
    result.solveMs = Tools::ElapsedMs(solveStart, Tools::Clock::now());
    result.grbStatus = subModel.get(GRB_IntAttr_Status);
    return result;
}

// Build a simple continuous cut expression for optimality cuts (same as BuildContinuousCut pattern)
void BuildContinuousCutExpr(const BendersCutInfo& cutInfo, const std::vector<GRBVar>& masterVars,
    GRBLinExpr& cutExpr, double& lhsAtCurrent, const std::vector<double>& zCurrent)
{
    cutExpr = cutInfo.constant;
    lhsAtCurrent = cutInfo.constant;
    for (size_t j = 0; j < masterVars.size(); ++j) {
        const double coeff = cutInfo.yCoeffs[j];
        if (!IsRelevantCoeff(coeff))
            continue;
        cutExpr += coeff * masterVars[j];
        lhsAtCurrent += coeff * zCurrent[j];
    }
}

bool MeetsOuterGap(double bestUpperBound, double bestLowerBound, const SolverConfig& solverConfig)
{
    if (!std::isfinite(bestUpperBound)) return false;
    const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
    const double relGap = (bestUpperBound - bestLowerBound) / gapDenom;
    return relGap <= solverConfig.mipGap;
}

std::string CsvEscape(const std::string& value)
{
    bool needsQuotes = false;
    for (char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return value;
    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') escaped += '"';
        escaped += ch;
    }
    escaped += '"';
    return escaped;
}

} // namespace

struct FullMethod::ScenarioSubProblemState {
    int scenarioIndex = 0;
    ScenarioSubproblemInfo scenarioInfo;
    std::unique_ptr<ISubProblem_LShape> strategy;
    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    LShapeSubProblemContext context;
};

FullMethod::FullMethod(ProblemType problemType,
    std::string dataFolder, int maxIters, double tol)
    : problemType(problemType), dataFolder(std::move(dataFolder)),
      maxIters(maxIters), tolerance(tol)
{}

Status FullMethod::Initialize()
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
    RemovePhase1GlobalRouteThetaLink(*model_);

    scenarioThetaVars_.clear();
    scenarioThetaVars_.reserve(scenarioSubproblems.size());
    GRBLinExpr scenarioThetaSum = 0.0;
    for (size_t idx = 0; idx < scenarioSubproblems.size(); ++idx) {
        GRBVar gTheta = model_->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
            "scenario_theta_p2_" + std::to_string(idx));
        scenarioThetaVars_.push_back(gTheta);
        scenarioThetaSum += gTheta;
        AddScenarioRouteThetaLink(*model_, scenarioSubproblems[idx], gTheta, idx);
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

        auto sub = std::make_unique<ScenarioSubProblemState>();
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
        sub->model->set(GRB_IntParam_Threads, ParallelConfig::Phase2SubProblemThreads());
        sub->context.Clear();
        subProblems_.push_back(std::move(sub));
    }

    // ── Pass 2 (parallel): InitSubProblem on independent models ──
    {
        const ProblemData* pd = problemData_;
        try {
            RunBoundedIndexedTasks(subProblems_.size(),
                ParallelConfig::Phase2SubProblemConcurrency(),
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

Status FullMethod::Solve()
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
    std::set<MonotoneFeasKey> monotoneFeasPool;
    std::set<MonotoneOptKey> monotoneOptPool;

    for (int iter = 0; iter < maxIters; ++iter) {
        int monotoneFeasDedupSkip = 0;
        int monotoneOptDedupSkip = 0;
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
            zValues[i] = std::round(zVars_[i].get(GRB_DoubleAttr_X));
            fixedCost += zVars_[i].get(GRB_DoubleAttr_Obj) * zValues[i];
        }
        const double thetaValue = theta_.get(GRB_DoubleAttr_X);
        const double masterBound = model_->get(GRB_DoubleAttr_ObjBound);
        bestLowerBound_ = std::max(bestLowerBound_, masterBound);

        // ── RSP: LP relaxation of scenario subproblems ──
        bool rspAnyInfeasible = false;
        if (kEnablePhase2RSP) {
            // Evaluate LP relaxation for lower-bounding before MIP verification.
            const ProblemData* pd = problemData_;
            std::vector<SubProblemEvalResult> results;
            try {
                results = RunBoundedIndexedTasks(subProblems_.size(),
                    ParallelConfig::Phase2SubProblemConcurrency(),
                    [pd, &zValues, this](size_t idx) {
                        auto* sub = subProblems_[idx].get();
                        try {
                            return EvaluateScenarioSubproblem(*pd, *sub->strategy, *sub->model,
                                sub->context, zValues, true); // relaxed = LP relaxation
                        } catch (const std::exception& e) {
                            throw std::runtime_error("RSP idx=" + std::to_string(idx)
                                + " failed: " + e.what());
                        }
                    });
            }
            catch (const std::exception& e) {
                std::cerr << "Phase 2 sub-problem failed: " << e.what() << std::endl;
                return ERROR;
            }

            int feasCount = 0;
            int optCount = 0;

            for (size_t idx = 0; idx < results.size(); ++idx) {
                const SubProblemEvalResult& result = results[idx];
                if (result.status != OK) return result.status;

                if (!result.cutInfo.isOptimalityCut) {
                    // Infeasible LP relaxation �?feasibility cut + strengthened no-good
                    rspAnyInfeasible = true;
                    AddFeasibilityCut(*model_, result.cutInfo, zVars_,
                        "p2_feas_" + std::to_string(iter) + "_" + std::to_string(idx));
                    ++feasCount;
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
                    const MonotoneFeasKey noGoodKey = BuildMonotoneFeasKey(noGood);
                    if (monotoneFeasPool.insert(noGoodKey).second) {
                        AddFeasibilityCut(*model_, noGood, zVars_,
                            "p2_nogood_" + std::to_string(iter) + "_" + std::to_string(idx));
                        ++feasCount;
                    } else {
                        ++monotoneFeasDedupSkip;
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
                if (monotoneFeasDedupSkip > 0) {
                    std::cout << ", monotone_feas_dedup_skip x" << monotoneFeasDedupSkip;
                }
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
                if (MeetsOuterGap(bestUpperBound_, bestLowerBound_, solverConfig)) {
                    std::cout << "Phase 2 converged." << std::endl;
                    std::cout << "UB: " << bestUpperBound_ << ", LB: " << bestLowerBound_ << std::endl;
                    phase2ElapsedMs_ = Tools::ElapsedMs(phase2Start_, Tools::Clock::now());
                    std::cout << "Phase 2 final elapsed time: " << phase2ElapsedMs_ << " ms" << std::endl;
                    std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                              << (phase1ElapsedMs_ + phase2ElapsedMs_) << " ms" << std::endl;
                    ExportIncumbentSolution();
                    return OK;
                }
                continue;
            }
            if (optCount > 0) model_->update();
        } else {
            std::cout << "Phase 2 iter " << iter << " RSP disabled, skip LP relaxation" << std::endl;
        }

        // ── SP: Full MIP sub-problems (for incumbent update + integer optimality cuts) ──
        const ProblemData* pd = problemData_;
        std::vector<SubProblemEvalResult> mipResults;
        try {
            mipResults = RunBoundedIndexedTasks(subProblems_.size(),
                ParallelConfig::Phase2SubProblemConcurrency(),
                [pd, &zValues, this](size_t idx) {
                    auto* sub = subProblems_[idx].get();
                    try {
                        return EvaluateScenarioSubproblem(*pd, *sub->strategy, *sub->model,
                            sub->context, zValues, false); // full MIP
                    } catch (const std::exception& e) {
                        throw std::runtime_error("SP idx=" + std::to_string(idx)
                            + " failed: " + e.what());
                    }
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
            for (size_t idx = 0; idx < mipCutInfos.size(); ++idx) {
                if (mipCutInfos[idx].isOptimalityCut) continue;
                AddFeasibilityCut(*model_, mipCutInfos[idx], zVars_,
                    "p2_mip_feas_" + std::to_string(iter) + "_" + std::to_string(idx));
                ++feasCount;
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
                const MonotoneFeasKey noGoodKey = BuildMonotoneFeasKey(noGood);
                if (monotoneFeasPool.insert(noGoodKey).second) {
                    AddFeasibilityCut(*model_, noGood, zVars_,
                        "p2_mip_nogood_" + std::to_string(iter) + "_" + std::to_string(idx));
                    ++feasCount;
                } else {
                    ++monotoneFeasDedupSkip;
                }
            }
            model_->update();
            std::cout << "Phase 2 iter " << iter << ", MIP infeasible, feasibility cut x" << feasCount;
            if (monotoneFeasDedupSkip > 0) {
                std::cout << ", monotone_feas_dedup_skip x" << monotoneFeasDedupSkip;
            }
            if (std::isfinite(bestUpperBound_)) {
                double denom = std::max(1.0, std::fabs(bestUpperBound_));
                std::cout << ", UB=" << bestUpperBound_ << ", LB=" << bestLowerBound_
                          << ", gap=" << (bestUpperBound_ - bestLowerBound_) / denom * 100.0 << "%";
            }
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs
                      << ", mip_eval_ms=" << Tools::ElapsedMs(masterEnd, iterEnd)
                      << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start_, iterEnd) << std::endl;
            if (MeetsOuterGap(bestUpperBound_, bestLowerBound_, solverConfig)) {
                std::cout << "Phase 2 converged." << std::endl;
                std::cout << "UB: " << bestUpperBound_ << ", LB: " << bestLowerBound_ << std::endl;
                phase2ElapsedMs_ = Tools::ElapsedMs(phase2Start_, Tools::Clock::now());
                std::cout << "Phase 2 final elapsed time: " << phase2ElapsedMs_ << " ms" << std::endl;
                std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                          << (phase1ElapsedMs_ + phase2ElapsedMs_) << " ms" << std::endl;
                ExportIncumbentSolution();
                return OK;
            }
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
            const MonotoneOptKey optKey = BuildMonotoneOptKey(idx, optCut);
            if (monotoneOptPool.insert(optKey).second) {
                GRBLinExpr cutExpr;
                double lhsAtCurrent;
                BuildContinuousCutExpr(optCut, zVars_, cutExpr, lhsAtCurrent, zValues);
                model_->addConstr(cutExpr <= scenarioThetaVars_[idx],
                    "p2_mip_sopt_" + std::to_string(iter) + "_" + std::to_string(idx));
            } else {
                ++monotoneOptDedupSkip;
            }
        }
        model_->update();

        if (MeetsOuterGap(bestUpperBound_, bestLowerBound_, solverConfig)) {
            const auto iterEnd = Tools::Clock::now();
            if (monotoneOptDedupSkip > 0) {
                std::cout << ", monotone_opt_dedup_skip x" << monotoneOptDedupSkip;
            }
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
            ExportIncumbentSolution();
            return OK;
        }

        const auto iterEnd = Tools::Clock::now();
        if (monotoneOptDedupSkip > 0) {
            std::cout << ", monotone_opt_dedup_skip x" << monotoneOptDedupSkip;
        }
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

void FullMethod::ExportIncumbentSolution() const
{
    if (!exportSolution_) return;
    if (!problemData_ || incumbentZValues_.empty()) return;

    const fs::path exportDir = fs::path("results") / dataFolder / "full_solution";
    fs::create_directories(exportDir);

    const auto& masterVars = problemData_->getData<std::vector<ProblemDataVar>>("masterVars");
    const auto& scenarioSubproblems = problemData_->getData<std::vector<ScenarioSubproblemInfo>>(
        "wireless_scenario_subproblems");

    {
        std::ofstream out(exportDir / "solution_metadata.csv");
        out << std::fixed << std::setprecision(6);
        out << "metric,value\n";
        out << "best_upper_bound," << bestUpperBound_ << '\n';
        out << "best_lower_bound," << bestLowerBound_ << '\n';
        out << "phase1_elapsed_ms," << phase1ElapsedMs_ << '\n';
        out << "phase2_elapsed_ms," << phase2ElapsedMs_ << '\n';
        out << "total_elapsed_ms," << (phase1ElapsedMs_ + phase2ElapsedMs_) << '\n';
    }

    {
        std::ofstream out(exportDir / "master_variables.csv");
        out << std::fixed << std::setprecision(6);
        out << "index,var_name,value,obj,lb,ub,vtype\n";
        for (size_t idx = 0; idx < masterVars.size(); ++idx) {
            const double value = incumbentZValues_[idx];
            out << idx << ',' << CsvEscape(masterVars[idx].name) << ',' << value << ','
                << masterVars[idx].obj << ',' << masterVars[idx].lb << ','
                << masterVars[idx].ub << ',' << masterVars[idx].type << '\n';
        }
    }

    {
        std::ofstream out(exportDir / "subproblem_status.csv");
        out << std::fixed << std::setprecision(6);
        out << "subproblem_index,scenario_index,status,obj\n";

        std::ofstream varOut(exportDir / "subproblem_variables.csv");
        varOut << std::fixed << std::setprecision(6);
        varOut << "subproblem_index,scenario_index,var_index,var_name,value,obj,lb,ub,vtype\n";

        for (size_t idx = 0; idx < subProblems_.size(); ++idx) {
            auto* sub = subProblems_[idx].get();
            sub->strategy->UpdateSubProblem(*problemData_, *sub->model, sub->context, incumbentZValues_);
            sub->model->optimize();

            const int status = sub->model->get(GRB_IntAttr_Status);
            double obj = 0.0;
            if (status == GRB_OPTIMAL) {
                obj = sub->model->get(GRB_DoubleAttr_ObjVal);
            }

            int scenarioIndex = sub->scenarioIndex;
            if (idx < scenarioSubproblems.size()) {
                scenarioIndex = scenarioSubproblems[idx].scenarioIndex;
            }
            out << idx << ',' << scenarioIndex << ',' << status << ',' << obj << '\n';

            if (status != GRB_OPTIMAL) {
                std::cout << "Solution export skipped variables for scenario subproblem " << idx
                          << " because status=" << status << std::endl;
                continue;
            }

            const int numVars = sub->model->get(GRB_IntAttr_NumVars);
            GRBVar* vars = sub->model->getVars();
            for (int vIdx = 0; vIdx < numVars; ++vIdx) {
                const std::string name = vars[vIdx].get(GRB_StringAttr_VarName);
                const double value = vars[vIdx].get(GRB_DoubleAttr_X);
                const double objCoeff = vars[vIdx].get(GRB_DoubleAttr_Obj);
                const double lb = vars[vIdx].get(GRB_DoubleAttr_LB);
                const double ub = vars[vIdx].get(GRB_DoubleAttr_UB);
                const char vtype = vars[vIdx].get(GRB_CharAttr_VType);
                varOut << idx << ',' << scenarioIndex << ',' << vIdx << ','
                       << CsvEscape(name) << ',' << value << ',' << objCoeff << ','
                       << lb << ',' << ub << ',' << vtype << '\n';
            }
            delete[] vars;
        }
    }

    std::cout << "Solution export written to " << exportDir.string() << std::endl;
}

FullMethod::~FullMethod() = default;
