#include "lshape_relaxed_station.hpp"
#include "lshape_relaxed.hpp"
#include "lshape_station_cut.hpp"
#include "wireless_reform.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <future>
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

SubProblemEvalResult EvaluateStationGroup(const ProblemData& problemData,
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
    StationGroupInfo sgInfo;
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

    // ── Phase 1: sg-relax ──
    LShapeRelaxed phase1(problemType, dataFolder, maxIters, tolerance);
    phase1.SetSolverConfig(solverConfig);

    Status s = phase1.Initialize();
    if (s != OK) { std::cerr << "Phase 1 init failed" << std::endl; return s; }

    s = phase1.Solve();
    if (s != OK) { std::cerr << "Phase 1 solve failed" << std::endl; return s; }

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

    std::cout << std::endl << "── Phase 2: sg-full (reusing Phase 1 master + all cuts) ──" << std::endl;

    // ── Build station-group sub-problems ──
    const auto& stationGroups = problemData_->getData<std::vector<StationGroupInfo>>(
        "wireless_station_groups");

    if (stationGroups.empty()) {
        std::cerr << "No station groups found" << std::endl;
        return ERROR;
    }

    // Add groupThetaVars (one per station group, like sg-full)
    groupThetaVars_.clear();
    groupThetaVars_.reserve(stationGroups.size());
    GRBLinExpr groupThetaSum = 0.0;
    for (size_t idx = 0; idx < stationGroups.size(); ++idx) {
        GRBVar gTheta = model_->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
            "group_theta_p2_" + std::to_string(idx));
        groupThetaVars_.push_back(gTheta);
        groupThetaSum += gTheta;
    }
    model_->addConstr(groupThetaSum <= theta_, "theta_group_sum_p2");

    subProblems_.clear();
    subProblems_.reserve(stationGroups.size());

    for (size_t idx = 0; idx < stationGroups.size(); ++idx) {
        std::cout << "Phase 2 init sub-problem " << idx << "/" << stationGroups.size()
                  << " sc=" << stationGroups[idx].scenarioIndex
                  << " routes=" << stationGroups[idx].routeIds.size()
                  << " EBs=" << stationGroups[idx].ebIndices.size() << std::endl;

        auto sub = std::make_unique<StationSubProblemState>();
        sub->scenarioIndex = stationGroups[idx].scenarioIndex;
        sub->sgInfo = stationGroups[idx];
        sub->strategy = std::make_unique<WirelessStationGroupSubProblem>();
        static_cast<WirelessStationGroupSubProblem*>(sub->strategy.get())
            ->SetStationGroupInfo(stationGroups[idx]);
        static_cast<WirelessStationGroupSubProblem*>(sub->strategy.get())
            ->SetRelaxChargerCapacity(false);

        sub->env = std::make_unique<GRBEnv>(true);
        sub->env->set(GRB_IntParam_OutputFlag, 0);
        sub->env->start();
        sub->model = std::make_unique<GRBModel>(*sub->env);
        sub->model->set(GRB_IntParam_OutputFlag, 0);
        sub->model->set(GRB_DoubleParam_MIPGap, 0.0);
        sub->context.Clear();
        sub->strategy->InitSubProblem(*problemData_, static_cast<int>(idx),
            *sub->model, sub->context);
        std::cout << "  done, cols=" << sub->model->get(GRB_IntAttr_NumVars)
                  << " rows=" << sub->model->get(GRB_IntAttr_NumConstrs) << std::endl;
        subProblems_.push_back(std::move(sub));
    }

    std::cout << "Phase 2 station-group sub-problems count=" << subProblems_.size()
              << ", master constraints=" << model_->get(GRB_IntAttr_NumConstrs) << std::endl;

    initialized = true;
    return OK;
}

Status LShapeRelaxedStation::Solve()
{
    if (!initialized || !model_) {
        std::cerr << "Phase 2 not initialized" << std::endl;
        return ERROR;
    }

    algorithmStart_ = Tools::Clock::now();
    const auto solveStart = algorithmStart_;

    // Apply MIP start from Phase 1 solution
    for (size_t i = 0; i < zVars_.size(); ++i)
        zVars_[i].set(GRB_DoubleAttr_Start, phase1ZValues[i]);
    model_->update();

    std::cout << "Phase 2 MIP start applied, starting Benders..." << std::endl;

    const auto& stationGroups = problemData_->getData<std::vector<StationGroupInfo>>(
        "wireless_station_groups");
    const auto& masterVars = problemData_->getData<std::vector<ProblemDataVar>>("masterVars");
    const auto& facilityVarNames = problemData_->getData<std::vector<std::vector<std::vector<std::string>>>>(
        "wireless_route_facility_var_names");

    for (int iter = 0; iter < maxIters; ++iter) {
        const double elapsedSec = Tools::ElapsedMs(algorithmStart_, Tools::Clock::now()) / 1000.0;
        if (elapsedSec >= solverConfig.timeLimit) {
            std::cout << "Phase 2 reached time limit: " << solverConfig.timeLimit << "s" << std::endl;
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
        const double objVal = fixedCost + thetaValue;
        bestLowerBound_ = std::max(bestLowerBound_, objVal);

        // ── RSP: LP relaxation of station-group sub-problems ──
        bool rspAnyInfeasible = false;
        {
            // Evaluate LP relaxation (same as sg-full twoPhase=true)
            std::vector<std::future<SubProblemEvalResult>> futures;
            futures.reserve(subProblems_.size());
            const ProblemData* pd = problemData_;
            for (auto& sub : subProblems_) {
                futures.push_back(std::async(std::launch::async,
                    [pd, &zValues, sub = sub.get()] {
                        return EvaluateStationGroup(*pd, *sub->strategy, *sub->model,
                            sub->context, zValues, true); // relaxed = LP relaxation
                    }));
            }

            int feasCount = 0;
            int optCount = 0;
            std::set<std::string> addedFeasCuts;

            for (size_t idx = 0; idx < futures.size(); ++idx) {
                SubProblemEvalResult result;
                try { result = futures[idx].get(); }
                catch (const std::exception& e) {
                    std::cerr << "Phase 2 sub-problem failed: " << e.what() << std::endl;
                    return ERROR;
                }
                if (result.status != OK) return result.status;

                if (!result.cutInfo.isOptimalityCut) {
                    // Infeasible LP relaxation → feasibility cut + strengthened no-good
                    rspAnyInfeasible = true;
                    const std::string sig = MakeFeasibilityCutSignature(result.cutInfo);
                    if (addedFeasCuts.insert(sig).second) {
                        AddFeasibilityCut(*model_, result.cutInfo, zVars_,
                            "p2_feas_" + std::to_string(iter) + "_" + std::to_string(idx));
                        ++feasCount;
                    }
                    // Strengthened no-good (matching sg-full)
                    const auto& sgInfo = stationGroups[idx];
                    std::vector<std::string> sgFacNames;
                    int scIdx = sgInfo.scenarioIndex;
                    for (int rid : sgInfo.routeIds) {
                        const auto& rn = facilityVarNames.at(static_cast<size_t>(scIdx))
                            .at(static_cast<size_t>(rid));
                        sgFacNames.insert(sgFacNames.end(), rn.begin(), rn.end());
                    }
                    const BendersCutInfo noGood = BuildStationGroupStrengthenedNoGoodCut(
                        masterVars, zValues, scIdx, sgInfo.routeIds, sgFacNames);
                    const std::string ngSig = MakeFeasibilityCutSignature(noGood);
                    if (addedFeasCuts.insert(ngSig).second) {
                        AddFeasibilityCut(*model_, noGood, zVars_,
                            "p2_nogood_" + std::to_string(iter) + "_" + std::to_string(idx));
                        ++feasCount;
                    }
                } else {
                    // Feasible LP → optimality cut on groupThetaVar
                    if (groupThetaVars_[idx].get(GRB_DoubleAttr_X) + tolerance < result.subObj) {
                        GRBLinExpr cutExpr;
                        double lhsAtCurrent;
                        BuildContinuousCutExpr(result.cutInfo, zVars_, cutExpr, lhsAtCurrent, zValues);
                        model_->addConstr(cutExpr <= groupThetaVars_[idx],
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
                          << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
                continue;
            }
            if (optCount > 0) model_->update();
        }

        // ── SP: Full MIP sub-problems (for incumbent update + integer optimality cuts) ──
        std::vector<std::future<SubProblemEvalResult>> mipFutures;
        mipFutures.reserve(subProblems_.size());
        const ProblemData* pd = problemData_;
        for (auto& sub : subProblems_) {
            mipFutures.push_back(std::async(std::launch::async,
                [pd, &zValues, sub = sub.get()] {
                    return EvaluateStationGroup(*pd, *sub->strategy, *sub->model,
                        sub->context, zValues, false); // full MIP
                }));
        }

        double aggregatedQValue = 0.0;
        bool spAnyInfeasible = false;
        std::vector<BendersCutInfo> mipCutInfos(subProblems_.size());
        for (size_t idx = 0; idx < mipFutures.size(); ++idx) {
            SubProblemEvalResult result;
            try { result = mipFutures[idx].get(); }
            catch (const std::exception& e) {
                std::cerr << "Phase 2 MIP sub-problem failed: " << e.what() << std::endl;
                return ERROR;
            }
            if (result.status != OK) return result.status;

            mipCutInfos[idx] = result.cutInfo;
            if (result.cutInfo.isOptimalityCut)
                aggregatedQValue += result.subObj;
            else
                spAnyInfeasible = true;
        }

        if (spAnyInfeasible) {
            // MIP infeasible — add feasibility cut + strengthened no-good (matching sg-full)
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
                // Strengthened no-good (matching sg-full)
                const auto& sgInfo = stationGroups[idx];
                std::vector<std::string> sgFacNames;
                int scIdx = sgInfo.scenarioIndex;
                for (int rid : sgInfo.routeIds) {
                    const auto& rn = facilityVarNames.at(static_cast<size_t>(scIdx))
                        .at(static_cast<size_t>(rid));
                    sgFacNames.insert(sgFacNames.end(), rn.begin(), rn.end());
                }
                const BendersCutInfo noGood = BuildStationGroupStrengthenedNoGoodCut(
                    masterVars, zValues, scIdx, sgInfo.routeIds, sgFacNames);
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
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
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

        // Add MIP integer optimality cuts (matching sg-full lines 494-499)
        // These tighten the LB on the NEXT master re-solve.
        for (size_t idx = 0; idx < mipCutInfos.size(); ++idx) {
            if (!mipCutInfos[idx].isOptimalityCut) continue;
            GRBLinExpr cutExpr;
            double lhsAtCurrent;
            BuildContinuousCutExpr(mipCutInfos[idx], zVars_, cutExpr, lhsAtCurrent, zValues);
            model_->addConstr(cutExpr <= groupThetaVars_[idx],
                "p2_mip_opt_" + std::to_string(iter) + "_" + std::to_string(idx));
        }
        model_->update();

        // Convergence check — skip on iter 0 since MIP cuts haven't been
        // re-optimized yet (LB from Phase 1 cuts may be deceptively tight).
        if (iter > 0) {
            double gapDenom = std::max(1.0, std::fabs(bestUpperBound_));
            double relGap = (bestUpperBound_ - bestLowerBound_) / gapDenom;
            if (relGap <= solverConfig.mipGap) {
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs
                      << ", mip_eval_ms=" << Tools::ElapsedMs(masterEnd, iterEnd)
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
            std::cout << "Phase 2 converged." << std::endl;
            std::cout << "UB: " << bestUpperBound_ << ", LB: " << bestLowerBound_ << std::endl;
            if (!incumbentZValues_.empty()) {
                std::cout << "Active master variables:" << std::endl;
                for (size_t i = 0; i < zVars_.size(); ++i)
                    if (incumbentZValues_[i] > 0.5)
                        std::cout << zVars_[i].get(GRB_StringAttr_VarName) << std::endl;
                std::cout << "Q(z): " << incumbentQ_ << std::endl;
            }
            const auto solveEnd = Tools::Clock::now();
            std::cout << "Phase 2 final elapsed time: "
                      << Tools::ElapsedMs(solveStart, solveEnd) << " ms" << std::endl;
            return OK;
            }
        }  // iter > 0

        const auto iterEnd = Tools::Clock::now();
        std::cout << ", master_ms=" << masterMs
                  << ", mip_eval_ms=" << Tools::ElapsedMs(masterEnd, iterEnd)
                  << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
    }

    std::cout << "UB: " << bestUpperBound_ << ", LB: " << bestLowerBound_ << std::endl;
    std::cout << "Phase 2 reached max iterations: " << maxIters << std::endl;
    return ERROR;
}

LShapeRelaxedStation::~LShapeRelaxedStation() = default;
