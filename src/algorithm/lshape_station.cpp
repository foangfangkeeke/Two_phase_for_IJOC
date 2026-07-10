#include "lshape_station.hpp"
#include "lshape_station_cut.hpp"
#include "wireless_reform.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {

using DataInitFactory = std::function<std::unique_ptr<IDataInit_LShape>(const std::string&, bool)>;
using SubFactory = std::function<std::unique_ptr<ISubProblem_LShape>()>;

struct SubProblemEvalResult {
    Status status = ERROR;
    LShapeCutInfo cutInfo;
    double subObj = 0.0;
};

void AddFeasibilityCut(GRBModel& model, const LShapeCutInfo& cutInfo,
    const std::vector<GRBVar>& masterVars, const std::string& cutName)
{
    GRBLinExpr cutExpr = cutInfo.constant;
    for (size_t idx = 0; idx < masterVars.size(); ++idx)
        cutExpr += cutInfo.yCoeffs[idx] * masterVars[idx];
    if (cutInfo.sense == '>') {
        model.addConstr(cutExpr >= cutInfo.rhs, cutName);
        return;
    }
    if (cutInfo.sense == '<') {
        model.addConstr(cutExpr <= cutInfo.rhs, cutName);
        return;
    }
    throw std::invalid_argument("Unsupported feasibility cut sense");
}

static const std::map<ProblemType, std::tuple<DataInitFactory, SubFactory>> kStrategyMap = {
    {
        WIRELESS_CAPACITY_REFORM,
        std::make_tuple(
            [](const std::string& dataFolder, bool perStation) {
                return std::make_unique<WirelessReformDataInit>(dataFolder, perStation);
            },
            []() { return std::make_unique<WirelessStationGroupSubProblem>(); })
    }
};

SubProblemEvalResult EvaluateStationGroupSubproblem(const ProblemData& problemData,
    ISubProblem_LShape& strategy, GRBModel& subModel,
    LShapeSubProblemContext& context, const std::vector<double>& zValues, bool relaxed)
{
    SubProblemEvalResult result;
    strategy.UpdateSubProblem(problemData, subModel, context, zValues);
    result.status = relaxed
        ? strategy.SolveRelaxedSubProblem(problemData, subModel, context, zValues,
              result.cutInfo, result.subObj)
        : strategy.SolveSubProblem(problemData, subModel, context, zValues,
              result.cutInfo, result.subObj);
    return result;
}

} // namespace

// --- LShapeStation::StationSubProblemState ---

struct LShapeStation::StationSubProblemState {
    int stationGroupId = 0;
    int scenarioIndex = 0;
    std::unique_ptr<ISubProblem_LShape> strategy;
    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    LShapeSubProblemContext context;
};

// --- Constructor ---

LShapeStation::LShapeStation(ProblemType problemType, std::string dataFolder,
    bool useImprovedCuts, bool twoPhase, int maxIters, double tol, bool perStationMode)
    : problemType(problemType), dataFolder(std::move(dataFolder)),
      maxIters(maxIters), tolerance(tol),
      useImprovedCuts(useImprovedCuts), twoPhase(twoPhase),
      perStationMode(perStationMode),
      globalLowerBound(0.0), bestUpperBound(std::numeric_limits<double>::infinity()),
      bestLowerBound(-std::numeric_limits<double>::infinity()),
      incumbentSecondStageValue(std::numeric_limits<double>::infinity())
{}

LShapeStation::~LShapeStation() = default;

std::string LShapeStation::AlgorithmLabel() const
{
    if (twoPhase) {
        return useImprovedCuts
            ? "Lifted two-phase ILS with strengthened cuts"
            : "Lifted two-phase ILS";
    }
    return useImprovedCuts
        ? "Lifted ILS with strengthened cuts"
        : "Lifted ILS";
}

// --- Initialize ---

Status LShapeStation::Initialize()
{
    env = std::make_unique<GRBEnv>(true);
    env->set(GRB_IntParam_OutputFlag, 0);
    env->set("LogFile", "gurobi_log.txt");
    env->start();
    model = std::make_shared<GRBModel>(*env);
    model->set(GRB_IntParam_OutputFlag, 0);

    if (problemType != WIRELESS_CAPACITY_REFORM) {
        throw std::invalid_argument("LShapeStation requires WIRELESS_CAPACITY_REFORM problem type");
    }

    auto it = kStrategyMap.find(problemType);
    if (it == kStrategyMap.end()) {
        throw std::invalid_argument("Unsupported problem type for lifted ILS");
    }

    dataIniter = std::get<0>(it->second)(dataFolder, perStationMode);
    problemData = std::make_unique<ProblemData>();
    const auto dataInitStart = Tools::Clock::now();
    dataIniter->DataInit(*problemData);
    const std::vector<ProblemDataConstr> masterConstrs = dataIniter->ConstrInit(*problemData);
    const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
    InitializeBendersMasterModel(*model, masterVars, masterConstrs, zVars, theta, true);
    model->set(GRB_DoubleParam_MIPGap, 0.0);
    // Apply solver config for custom algorithm (presolve/method/threads only)
    model->set(GRB_IntParam_Presolve, solverConfig.presolve);
    model->set(GRB_IntParam_Method, solverConfig.method);
    model->set(GRB_IntParam_Threads, solverConfig.threads);

    // Multi-cut: one theta per subproblem, theta >= sum(theta_i)
    const auto& stationGroups = problemData->getData<std::vector<StationGroupInfo>>("wireless_station_groups");
    groupThetaVars.clear();
    groupThetaVars.reserve(stationGroups.size());
    GRBLinExpr thetaSum = 0.0;
    for (size_t sgIdx = 0; sgIdx < stationGroups.size(); ++sgIdx) {
        GRBVar t = model->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
            "theta_sg" + std::to_string(sgIdx));
        groupThetaVars.push_back(t);
        thetaSum += t;
    }
    model->addConstr(thetaSum <= theta, "theta_multi_sum");

    const auto dataInitEnd = Tools::Clock::now();
    const std::string label = AlgorithmLabel();
    std::cout << label << " data/master initialization elapsed_ms="
              << Tools::ElapsedMs(dataInitStart, dataInitEnd) << std::endl;

    const auto subProblemsInitStart = Tools::Clock::now();
    subProblems.clear();

    for (size_t sgIdx = 0; sgIdx < stationGroups.size(); ++sgIdx) {
        const StationGroupInfo& sgInfo = stationGroups[sgIdx];

        auto sub = std::make_unique<StationSubProblemState>();
        sub->stationGroupId = sgInfo.stationGroupId;
        sub->scenarioIndex = sgInfo.scenarioIndex;
        sub->strategy = std::get<1>(it->second)();
        static_cast<WirelessStationGroupSubProblem*>(sub->strategy.get())->SetStationGroupInfo(sgInfo);
        sub->env = std::make_unique<GRBEnv>(true);
        sub->env->set(GRB_IntParam_OutputFlag, 0);
        sub->env->start();
        sub->model = std::make_unique<GRBModel>(*sub->env);
        sub->model->set(GRB_IntParam_OutputFlag, 0);
        sub->model->set(GRB_IntParam_Presolve, solverConfig.presolve);
        sub->model->set(GRB_IntParam_Method, solverConfig.method);
        sub->model->set(GRB_IntParam_Threads, solverConfig.threads);
        sub->context.Clear();
        sub->strategy->InitSubProblem(*problemData, static_cast<int>(sgIdx), *sub->model, sub->context);
        subProblems.push_back(std::move(sub));
    }

    const auto subProblemsInitEnd = Tools::Clock::now();
    std::cout << label << " operational sub-problems init count=" << subProblems.size()
              << " elapsed_ms=" << Tools::ElapsedMs(subProblemsInitStart, subProblemsInitEnd) << std::endl;

    model->update();
    initialized = true;
    return OK;
}

// --- BuildContinuousCut ---

LShapeStation::CutEval LShapeStation::BuildContinuousCut(const LShapeCutInfo& cutInfo,
    const std::vector<double>& zCurrent, int iter, int subProblemIndex) const
{
    const BendersCutEvaluation cutEvaluation = BuildBendersCutEvaluation(cutInfo, zVars, zCurrent);
    CutEval cut;
    cut.name = "sg_l_shaped_cut_continuous_" + std::to_string(iter) + "_sub_" + std::to_string(subProblemIndex);
    cut.expr = cutEvaluation.expr;
    cut.lhsAtCurrent = cutEvaluation.lhsAtCurrent;
    return cut;
}

// --- EvaluateSubProblems ---

Status LShapeStation::EvaluateSubProblems(const std::vector<double>& zValues,
    LShapeCutInfo& cutInfo, double& subObj,
    std::vector<double>& groupSubObjs, std::vector<LShapeCutInfo>& groupCutInfos,
    bool relaxed)
{
    if (subProblems.empty()) {
        std::cerr << AlgorithmLabel() << " operational sub-problems are not initialized" << std::endl;
        return ERROR;
    }

    cutInfo = LShapeCutInfo{};
    cutInfo.isOptimalityCut = true;
    cutInfo.sense = '>';
    cutInfo.rhs = 0.0;
    cutInfo.yCoeffs.assign(zValues.size(), 0.0);
    cutInfo.constant = 0.0;
    subObj = 0.0;
    groupSubObjs.assign(subProblems.size(), 0.0);
    groupCutInfos.assign(subProblems.size(), LShapeCutInfo{});

    const ProblemData* problemDataPtr = problemData.get();
    for (size_t idx = 0; idx < subProblems.size(); ++idx) {
        auto& sub = subProblems[idx];
        // TODO: revert to async dispatch once stability is confirmed
        SubProblemEvalResult result = EvaluateStationGroupSubproblem(*problemDataPtr,
            *sub->strategy, *sub->model, sub->context, zValues, relaxed);

        if (result.status != OK) return result.status;

        groupSubObjs[idx] = result.subObj;
        groupCutInfos[idx] = result.cutInfo;

        if (!result.cutInfo.isOptimalityCut) {
            if (cutInfo.isOptimalityCut) cutInfo = result.cutInfo;
            continue;
        }

        subObj += result.subObj;
        cutInfo.constant += result.cutInfo.constant;
        for (size_t coeffIdx = 0; coeffIdx < zValues.size(); ++coeffIdx) {
            cutInfo.yCoeffs[coeffIdx] += result.cutInfo.yCoeffs[coeffIdx];
        }
    }

    return OK;
}

// --- WarmStart / UpdateIncumbent / PrintBestSolution ---

void LShapeStation::SetExternalWarmStart(const std::vector<double>& zValues, double upperBound)
{
    externalWarmStartReady_ = true;
    externalWarmStartZValues_ = zValues;
    externalWarmStartUpperBound_ = upperBound;
}

Status LShapeStation::ApplyWarmStart()
{
    if (externalWarmStartReady_) {
        for (size_t i = 0; i < zVars.size(); ++i)
            zVars[i].set(GRB_DoubleAttr_Start, externalWarmStartZValues_[i]);
        // IMPORTANT: Phase 1 UB is NOT valid for Phase 2 (charger capacity constraints
        // are stricter). Phase 1 cost is a lower bound for Phase 2, not an upper bound.
        // Keep bestUpperBound at infinity; it will be set when Phase 2 finds a feasible solution.
        bestUpperBound = 1e9;  // effectively infinite, same as default warm start
        globalLowerBound = std::max(0.0, externalWarmStartUpperBound_ * 0.99);
        for (auto& sub : subProblems) sub->strategy->SetLowerBound(0.0);
        std::cout << AlgorithmLabel() << " warm-started from Phase 1 solution"
                  << ", Phase1_obj=" << externalWarmStartUpperBound_
                  << ", init_LB=" << globalLowerBound << std::endl;
        return OK;
    }
    bestUpperBound = 1e9;
    globalLowerBound = 0.0;
    for (auto& sub : subProblems) sub->strategy->SetLowerBound(0.0);
    return OK;
}

void LShapeStation::UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue)
{
    bestUpperBound = 0.0;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        bestUpperBound += zVars[idx].get(GRB_DoubleAttr_Obj) * zValues[idx];
    }
    bestUpperBound += secondStageValue;
    incumbentZValues = zValues;
    incumbentSecondStageValue = secondStageValue;
}

void LShapeStation::PrintBestSolution() const
{
    std::cout << "UB: " << bestUpperBound
              << ", LB: " << bestLowerBound << std::endl;
    if (!std::isfinite(bestUpperBound)) return;
    std::cout << "Active master variables:" << std::endl;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        if (incumbentZValues[idx] > 0.5) {
            std::cout << "  " << zVars[idx].get(GRB_StringAttr_VarName) << std::endl;
        }
    }
    std::cout << "Q(z): " << incumbentSecondStageValue << std::endl;
}

// --- Solve ---

Status LShapeStation::Solve()
{
    algorithmStart = Tools::Clock::now();
    const auto solveStart = algorithmStart;

    Status warmStartStatus = ApplyWarmStart();
    if (warmStartStatus != OK) return warmStartStatus;

    model->addConstr(theta >= globalLowerBound, "theta_global_lb");
    model->update();

    for (int iter = 0; iter < maxIters; ++iter) {
        const double elapsedSec = Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) / 1000.0;
        if (elapsedSec >= solverConfig.timeLimit) {
            std::cout << AlgorithmLabel() << " reached time limit: " << solverConfig.timeLimit << "s" << std::endl;
            return ERROR;
        }

        // --- Master solve ---
        const auto masterStart = Tools::Clock::now();
        model->optimize();
        const auto masterEnd = Tools::Clock::now();
        const long long masterMs = Tools::ElapsedMs(masterStart, masterEnd);
        int masterStatus = model->get(GRB_IntAttr_Status);
        if (!(masterStatus == GRB_OPTIMAL ||
              (masterStatus == GRB_TIME_LIMIT && model->get(GRB_IntAttr_SolCount) > 0))) {
            std::cerr << AlgorithmLabel() << " master problem not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }

        const BendersMasterSolution masterSolution = GetBendersMasterSolution(*model, zVars, theta);
        const std::vector<double>& zValues = masterSolution.values;
        const double fixedCost = masterSolution.firstStageValue;
        const double thetaValue = masterSolution.thetaValue;
        bestLowerBound = std::max(bestLowerBound, masterSolution.objectiveValue);

        // --- RSP: Relaxed subproblems (LP, for dual-based optimality cuts) ---
        bool rspAnyInfeasible = false;
        if (twoPhase) {
            double relaxedQValue = 0.0;
            LShapeCutInfo relaxedCutInfo;
            std::vector<double> relaxedGroupQValues;
            std::vector<LShapeCutInfo> relaxedGroupCutInfos;
            const auto relaxedStart = Tools::Clock::now();
            Status relaxedStatus = EvaluateSubProblems(zValues, relaxedCutInfo, relaxedQValue,
                relaxedGroupQValues, relaxedGroupCutInfos, true);
            const auto relaxedEnd = Tools::Clock::now();
            const long long relaxedMs = Tools::ElapsedMs(relaxedStart, relaxedEnd);
            if (relaxedStatus != OK) return ERROR;

            const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
            const auto& stationGroups = problemData->getData<std::vector<StationGroupInfo>>("wireless_station_groups");
            const auto& facilityVarNames = problemData->getData<std::vector<std::vector<std::vector<std::string>>>>(
                "wireless_route_facility_var_names");

            int feasCount = 0;
            int optCount = 0;
            std::set<std::string> addedFeasCuts;
            for (size_t idx = 0; idx < relaxedGroupCutInfos.size(); ++idx) {
                if (!relaxedGroupCutInfos[idx].isOptimalityCut) {
                    // Infeasible subproblem: Farkas cut + strengthened no-good
                    rspAnyInfeasible = true;
                    const std::string sig = MakeFeasibilityCutSignature(relaxedGroupCutInfos[idx]);
                    if (addedFeasCuts.insert(sig).second) {
                        AddFeasibilityCut(*model, relaxedGroupCutInfos[idx], zVars,
                            "sg_relaxed_feasibility_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                        ++feasCount;
                    }
                    if (useImprovedCuts) {
                        const auto& sgInfo = stationGroups[idx];
                        std::vector<std::string> sgFacilityVarNames;
                        int scIdx = sgInfo.scenarioIndex;
                        for (int rid : sgInfo.routeIds) {
                            const auto& routeNames = facilityVarNames.at(static_cast<size_t>(scIdx)).at(static_cast<size_t>(rid));
                            sgFacilityVarNames.insert(sgFacilityVarNames.end(), routeNames.begin(), routeNames.end());
                        }
                        const BendersCutInfo noGoodCut = BuildStationGroupStrengthenedNoGoodCut(
                            masterVars, zValues, sgInfo.scenarioIndex, sgInfo.routeIds, sgFacilityVarNames);
                        AddFeasibilityCut(*model, noGoodCut, zVars,
                            "sg_strengthened_nogood_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                        ++feasCount;
                    }
                } else if (groupThetaVars[idx].get(GRB_DoubleAttr_X) + tolerance < relaxedGroupQValues[idx]) {
                    // Feasible subproblem, need optimality cut
                    const CutEval cutContinuous = BuildContinuousCut(
                        relaxedGroupCutInfos[idx], zValues, iter, static_cast<int>(idx));
                    model->addConstr(cutContinuous.expr <= groupThetaVars[idx], cutContinuous.name);
                    ++optCount;
                }
            }

            if (rspAnyInfeasible) {
                std::cout << AlgorithmLabel() << " iter " << iter << ", feasibility cut from relaxation x" << feasCount;
                if (std::isfinite(bestUpperBound)) {
                    const double denom = std::max(1.0, std::fabs(bestUpperBound));
                    std::cout << ", UB=" << bestUpperBound << ", LB=" << bestLowerBound
                              << ", gap=" << (bestUpperBound - bestLowerBound) / denom * 100.0 << "%";
                }
                model->update();
                std::cout << ", master_ms=" << masterMs << ", relaxed_ms=" << relaxedMs
                          << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, Tools::Clock::now()) << std::endl;
                continue;
            }
            if (optCount > 0) model->update();
        }

        // --- SP: MIP subproblems ---
        double aggregatedQValue = 0.0;
        LShapeCutInfo aggregateCutInfo;
        std::vector<double> groupQValues;
        std::vector<LShapeCutInfo> groupCutInfos;
        const auto integerStart = Tools::Clock::now();
        Status secondStageStatus = EvaluateSubProblems(zValues, aggregateCutInfo, aggregatedQValue,
            groupQValues, groupCutInfos, false);
        const auto integerEnd = Tools::Clock::now();
        const long long integerMs = Tools::ElapsedMs(integerStart, integerEnd);
        if (secondStageStatus != OK) {
            std::cerr << AlgorithmLabel() << " second-stage evaluation failed in iteration " << iter << std::endl;
            return ERROR;
        }

        // --- MIP feasibility cuts (per-subproblem) ---
        int feasCount = 0;
        bool mipAnyInfeasible = false;
        {
            std::set<std::string> addedFeasibilityCuts;
            const auto& mVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
            const auto& sGroups = problemData->getData<std::vector<StationGroupInfo>>("wireless_station_groups");
            const auto& fVarNames = problemData->getData<std::vector<std::vector<std::vector<std::string>>>>(
                "wireless_route_facility_var_names");
            for (size_t idx = 0; idx < groupCutInfos.size(); ++idx) {
                if (groupCutInfos[idx].isOptimalityCut) continue;
                mipAnyInfeasible = true;
                const std::string cutSignature = MakeFeasibilityCutSignature(groupCutInfos[idx]);
                if (addedFeasibilityCuts.insert(cutSignature).second) {
                    AddFeasibilityCut(*model, groupCutInfos[idx], zVars,
                        "sg_feasibility_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                    ++feasCount;
                }
                if (useImprovedCuts) {
                    const auto& sgInfo = sGroups[idx];
                    std::vector<std::string> sgFacilityVarNames;
                    int scIdx = sgInfo.scenarioIndex;
                    for (int rid : sgInfo.routeIds) {
                        const auto& routeNames = fVarNames.at(static_cast<size_t>(scIdx)).at(static_cast<size_t>(rid));
                        sgFacilityVarNames.insert(sgFacilityVarNames.end(), routeNames.begin(), routeNames.end());
                    }
                    const BendersCutInfo noGoodCut = BuildStationGroupStrengthenedNoGoodCut(
                        mVars, zValues, sgInfo.scenarioIndex, sgInfo.routeIds, sgFacilityVarNames);
                    AddFeasibilityCut(*model, noGoodCut, zVars,
                        "sg_strengthened_nogood_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                    ++feasCount;
                }
            }
        }

        if (mipAnyInfeasible) {
            std::cout << AlgorithmLabel() << " iter " << iter << ", feasibility cut x" << feasCount;
            if (std::isfinite(bestUpperBound)) {
                const double denom = std::max(1.0, std::fabs(bestUpperBound));
                std::cout << ", UB=" << bestUpperBound << ", LB=" << bestLowerBound
                          << ", gap=" << (bestUpperBound - bestLowerBound) / denom * 100.0 << "%";
            }
            model->update();
            std::cout << ", master_ms=" << masterMs << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, Tools::Clock::now()) << std::endl;
            continue;
        }

        // --- Optimality: update incumbent ---
        const double candidateObjective = fixedCost + aggregatedQValue;
        if (candidateObjective + tolerance < bestUpperBound) {
            UpdateIncumbent(zValues, aggregatedQValue);
        }

        std::cout << AlgorithmLabel() << " iter " << iter
                  << ", theta=" << thetaValue << ", Q(z)=" << aggregatedQValue;
        if (std::isfinite(bestUpperBound)) {
            const double denom = std::max(1.0, std::fabs(bestUpperBound));
            std::cout << ", UB=" << bestUpperBound << ", LB=" << bestLowerBound
                      << ", gap=" << (bestUpperBound - bestLowerBound) / denom * 100.0 << "%";
        }

        const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
        const double relGap = (bestUpperBound - bestLowerBound) / gapDenom;
        if (relGap <= solverConfig.mipGap) {
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
            PrintBestSolution();
            const auto solveEnd = Tools::Clock::now();
            std::cout << AlgorithmLabel() << " converged." << std::endl;
            std::cout << AlgorithmLabel() << " final elapsed time: "
                      << Tools::ElapsedMs(solveStart, solveEnd) << " ms" << std::endl;
            return OK;
        }

        // Add optimality cuts: per-group theta_i
        for (size_t idx = 0; idx < groupCutInfos.size(); ++idx) {
            if (!groupCutInfos[idx].isOptimalityCut) continue;
            const CutEval cutDual = BuildContinuousCut(groupCutInfos[idx], zValues, iter, static_cast<int>(idx));
            model->addConstr(cutDual.expr <= groupThetaVars[idx], cutDual.name);
        }

        model->update();
        const auto iterEnd = Tools::Clock::now();
        std::cout << ", master_ms=" << masterMs << ", integer_ms=" << integerMs
                  << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
    }

    PrintBestSolution();
    std::cout << AlgorithmLabel() << " reached max iterations: " << maxIters << std::endl;
    return ERROR;
}
