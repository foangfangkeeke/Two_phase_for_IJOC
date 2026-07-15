#include "lifted_ils.hpp"
#include "lshape_monotonicity_cut.hpp"
#include "wireless_reform.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace {

using DataInitFactory = std::function<std::unique_ptr<IDataInit_LShape>(const std::string&)>;
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
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        cutExpr += cutInfo.yCoeffs[idx] * masterVars[idx];
    }
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

const std::map<ProblemType, std::tuple<DataInitFactory, SubFactory>> kStrategyMap = {
    {
        WIRELESS_CAPACITY_REFORM,
        std::make_tuple(
            [](const std::string& dataFolder) {
                return std::make_unique<WirelessReformDataInit>(dataFolder);
            },
            []() { return std::make_unique<WirelessStationGroupSubProblem>(); })
    }
};

SubProblemEvalResult EvaluateStationGroupSubproblem(const ProblemData& problemData,
    ISubProblem_LShape& strategy, GRBModel& subModel,
    LShapeSubProblemContext& context, const std::vector<double>& zValues)
{
    SubProblemEvalResult result;
    strategy.UpdateSubProblem(problemData, subModel, context, zValues);
    result.status = strategy.SolveSubProblem(problemData, subModel, context, zValues,
        result.cutInfo, result.subObj);
    return result;
}

} // namespace

struct LiftedILS::StationSubProblemState {
    int stationGroupId = 0;
    int scenarioIndex = 0;
    std::unique_ptr<ISubProblem_LShape> strategy;
    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    LShapeSubProblemContext context;
};

LiftedILS::LiftedILS(ProblemType problemType, std::string dataFolder,
    int maxIters, double tol)
    : problemType(problemType),
      dataFolder(std::move(dataFolder)),
      maxIters(maxIters),
      tolerance(tol),
      globalLowerBound(0.0),
      bestUpperBound(std::numeric_limits<double>::infinity()),
      bestLowerBound(-std::numeric_limits<double>::infinity()),
      incumbentSecondStageValue(std::numeric_limits<double>::infinity())
{}

LiftedILS::~LiftedILS() = default;

Status LiftedILS::Initialize()
{
    env = std::make_unique<GRBEnv>(true);
    env->set(GRB_IntParam_OutputFlag, 0);
    env->set("LogFile", "gurobi_log.txt");
    env->start();
    model = std::make_shared<GRBModel>(*env);
    model->set(GRB_IntParam_OutputFlag, 0);

    if (problemType != WIRELESS_CAPACITY_REFORM) {
        throw std::invalid_argument("Lifted ILS requires WIRELESS_CAPACITY_REFORM problem type");
    }

    auto it = kStrategyMap.find(problemType);
    if (it == kStrategyMap.end()) {
        throw std::invalid_argument("Unsupported problem type for Lifted ILS");
    }

    dataIniter = std::get<0>(it->second)(dataFolder);
    problemData = std::make_unique<ProblemData>();
    const auto dataInitStart = Tools::Clock::now();
    dataIniter->DataInit(*problemData);
    const std::vector<ProblemDataConstr> masterConstrs = dataIniter->ConstrInit(*problemData);
    const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
    InitializeBendersMasterModel(*model, masterVars, masterConstrs, zVars, theta, true);
    model->set(GRB_DoubleParam_MIPGap, 0.0);
    model->set(GRB_IntParam_Presolve, solverConfig.presolve);
    model->set(GRB_IntParam_Method, solverConfig.method);
    model->set(GRB_IntParam_Threads, solverConfig.threads);

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
    std::cout << "Lifted ILS data/master initialization elapsed_ms="
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
        sub->model->set(GRB_DoubleParam_MIPGap, 0.0);
        sub->model->set(GRB_IntParam_Presolve, solverConfig.presolve);
        sub->model->set(GRB_IntParam_Method, solverConfig.method);
        sub->model->set(GRB_IntParam_Threads, solverConfig.threads);
        sub->context.Clear();
        sub->strategy->InitSubProblem(*problemData, static_cast<int>(sgIdx), *sub->model, sub->context);
        subProblems.push_back(std::move(sub));
    }

    const auto subProblemsInitEnd = Tools::Clock::now();
    std::cout << "Lifted ILS operational sub-problems init count=" << subProblems.size()
              << " elapsed_ms=" << Tools::ElapsedMs(subProblemsInitStart, subProblemsInitEnd) << std::endl;

    model->update();
    initialized = true;
    return OK;
}

LiftedILS::CutEval LiftedILS::BuildContinuousCut(const LShapeCutInfo& cutInfo,
    const std::vector<double>& zCurrent, int iter, int subProblemIndex) const
{
    const BendersCutEvaluation cutEvaluation = BuildBendersCutEvaluation(cutInfo, zVars, zCurrent);
    CutEval cut;
    cut.name = "lifted_ils_cut_" + std::to_string(iter) + "_sub_" + std::to_string(subProblemIndex);
    cut.expr = cutEvaluation.expr;
    cut.lhsAtCurrent = cutEvaluation.lhsAtCurrent;
    return cut;
}

Status LiftedILS::EvaluateSubProblems(const std::vector<double>& zValues,
    LShapeCutInfo& cutInfo, double& subObj,
    std::vector<double>& groupSubObjs, std::vector<LShapeCutInfo>& groupCutInfos)
{
    if (subProblems.empty()) {
        std::cerr << "Lifted ILS operational sub-problems are not initialized" << std::endl;
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
        SubProblemEvalResult result = EvaluateStationGroupSubproblem(*problemDataPtr,
            *sub->strategy, *sub->model, sub->context, zValues);

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

Status LiftedILS::ApplyWarmStart()
{
    bestUpperBound = 1e9;
    globalLowerBound = 0.0;
    for (auto& sub : subProblems) {
        sub->strategy->SetLowerBound(0.0);
    }
    return OK;
}

void LiftedILS::UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue)
{
    bestUpperBound = 0.0;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        bestUpperBound += zVars[idx].get(GRB_DoubleAttr_Obj) * zValues[idx];
    }
    bestUpperBound += secondStageValue;
    incumbentZValues = zValues;
    incumbentSecondStageValue = secondStageValue;
}

void LiftedILS::PrintBestSolution() const
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

Status LiftedILS::Solve()
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
            std::cout << "Lifted ILS reached time limit: " << solverConfig.timeLimit << "s" << std::endl;
            return ERROR;
        }

        const auto masterStart = Tools::Clock::now();
        model->optimize();
        const auto masterEnd = Tools::Clock::now();
        const long long masterMs = Tools::ElapsedMs(masterStart, masterEnd);
        int masterStatus = model->get(GRB_IntAttr_Status);
        if (!(masterStatus == GRB_OPTIMAL ||
              (masterStatus == GRB_TIME_LIMIT && model->get(GRB_IntAttr_SolCount) > 0))) {
            std::cerr << "Lifted ILS master problem not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }

        const BendersMasterSolution masterSolution = GetBendersMasterSolution(*model, zVars, theta);
        const std::vector<double>& zValues = masterSolution.values;
        const double fixedCost = masterSolution.firstStageValue;
        const double thetaValue = masterSolution.thetaValue;
        bestLowerBound = std::max(bestLowerBound, model->get(GRB_DoubleAttr_ObjBound));

        double aggregatedQValue = 0.0;
        LShapeCutInfo aggregateCutInfo;
        std::vector<double> groupQValues;
        std::vector<LShapeCutInfo> groupCutInfos;
        const auto integerStart = Tools::Clock::now();
        Status secondStageStatus = EvaluateSubProblems(zValues, aggregateCutInfo, aggregatedQValue,
            groupQValues, groupCutInfos);
        const auto integerEnd = Tools::Clock::now();
        const long long integerMs = Tools::ElapsedMs(integerStart, integerEnd);
        if (secondStageStatus != OK) {
            std::cerr << "Lifted ILS second-stage evaluation failed in iteration " << iter << std::endl;
            return ERROR;
        }

        int feasCount = 0;
        bool mipAnyInfeasible = false;
        std::set<std::string> addedFeasibilityCuts;
        for (size_t idx = 0; idx < groupCutInfos.size(); ++idx) {
            if (groupCutInfos[idx].isOptimalityCut) continue;
            mipAnyInfeasible = true;
            const std::string cutSignature = MakeFeasibilityCutSignature(groupCutInfos[idx]);
            if (addedFeasibilityCuts.insert(cutSignature).second) {
                AddFeasibilityCut(*model, groupCutInfos[idx], zVars,
                    "lifted_ils_feasibility_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                ++feasCount;
            }
        }

        if (mipAnyInfeasible) {
            std::cout << "Lifted ILS iter " << iter << ", feasibility cut x" << feasCount;
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

        const double candidateObjective = fixedCost + aggregatedQValue;
        if (candidateObjective + tolerance < bestUpperBound) {
            UpdateIncumbent(zValues, aggregatedQValue);
        }

        std::cout << "Lifted ILS iter " << iter
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
            std::cout << "Lifted ILS converged." << std::endl;
            std::cout << "Lifted ILS final elapsed time: "
                      << Tools::ElapsedMs(solveStart, solveEnd) << " ms" << std::endl;
            return OK;
        }

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
    std::cout << "Lifted ILS reached max iterations: " << maxIters << std::endl;
    return ERROR;
}
