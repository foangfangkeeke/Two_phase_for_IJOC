#include "lshape_relaxed.hpp"

#include "parallel_utils.hpp"
#include "wireless_reform.hpp"
#include "wireless_reform_route.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {
constexpr bool kMultiCut = true;
constexpr double kSparseCoeffTol = 1e-12;

bool IsRelevantCoeff(double coeff)
{
    return std::fabs(coeff) > kSparseCoeffTol;
}

const char* WarmStartStrategyName()
{
    return "zero lower bound";
}

struct SubProblemEvalResult {
    Status status = ERROR;
    LShapeCutInfo cutInfo;
    double subObj = 0.0;
};

SubProblemEvalResult EvaluateRouteSubproblem(const ProblemData& problemData,
    ISubProblem_LShape& strategy, GRBModel& subModel,
    LShapeSubProblemContext& context, const std::vector<double>& zValues)
{
    SubProblemEvalResult result;
    strategy.UpdateSubProblem(problemData, subModel, context, zValues);
    result.status = strategy.SolveSubProblem(problemData, subModel, context, zValues,
        result.cutInfo, result.subObj);
    return result;
}

void AddFeasibilityCut(GRBModel& model, const LShapeCutInfo& cutInfo,
    const std::vector<GRBVar>& masterVars, const std::string& cutName)
{
    GRBLinExpr cutExpr = cutInfo.constant;
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        const double coeff = cutInfo.yCoeffs[idx];
        if (IsRelevantCoeff(coeff)) {
            cutExpr += coeff * masterVars[idx];
        }
    }

    if (cutInfo.sense == '>') {
        model.addConstr(cutExpr >= cutInfo.rhs, cutName);
        return;
    }

    if (cutInfo.sense == '<') {
        model.addConstr(cutExpr <= cutInfo.rhs, cutName);
        return;
    }

    throw std::invalid_argument("Unsupported feasibility cut sense in relaxed route framework");
}

void PrintBounds(double bestUpperBound, double bestLowerBound)
{
    std::cout << ", UB=";
    if (std::isfinite(bestUpperBound)) {
        const double denom = std::max(1.0, std::fabs(bestUpperBound));
        std::cout << bestUpperBound << ", LB=" << bestLowerBound
                  << ", gap=" << (bestUpperBound - bestLowerBound) / denom * 100.0 << "%";
    } else {
        std::cout << "inf, LB=" << bestLowerBound << ", gap=1";
    }
}

} // namespace

struct LShapeRelaxed::RouteSubProblemState {
    int scenarioIndex = 0;
    int routeId = 0;
    std::unique_ptr<ISubProblem_LShape> strategy;
    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    LShapeSubProblemContext context;
};

LShapeRelaxed::LShapeRelaxed(ProblemType problemType, std::string dataFolder, int maxIters, double tol)
    : problemType(problemType), dataFolder(std::move(dataFolder)), maxIters(maxIters), tolerance(tol),
      globalLowerBound(0.0), bestUpperBound(std::numeric_limits<double>::infinity()),
      bestLowerBound(-std::numeric_limits<double>::infinity()),
      incumbentSecondStageValue(std::numeric_limits<double>::infinity())
{}

Status LShapeRelaxed::Initialize()
{
    env = std::make_unique<GRBEnv>(true);
    env->set(GRB_IntParam_OutputFlag, 0);
    env->set("LogFile", "gurobi_log.txt");
    env->start();
    model = std::make_shared<GRBModel>(*env);
    model->set(GRB_IntParam_OutputFlag, 0);

    if (problemType != WIRELESS_CAPACITY_REFORM) {
        throw std::invalid_argument("Unsupported problem type for relaxed route framework");
    }

    dataIniter = std::make_unique<WirelessReformDataInit>(dataFolder);
    problemData = std::make_unique<ProblemData>();
    const auto dataInitStart = Tools::Clock::now();
    dataIniter->DataInit(*problemData);
    const std::vector<ProblemDataConstr> masterConstrs = dataIniter->ConstrInit(*problemData);
    const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
    InitializeBendersMasterModel(*model, masterVars, masterConstrs, zVars, theta, true);
    model->set(GRB_DoubleParam_MIPGap, solverConfig.mipGap);
    model->set(GRB_IntParam_Threads, ParallelConfig::MasterThreads());
    const auto dataInitEnd = Tools::Clock::now();
    std::cout << "Relaxed Route data/master initialization elapsed_ms="
              << Tools::ElapsedMs(dataInitStart, dataInitEnd) << std::endl;

    const int scenarioCount = problemData->getData<int>("wireless_scenario_count");
    const auto& routeCounts = problemData->getData<std::vector<int>>("wireless_route_counts");
    const auto& routeIdByEBsPerScenario = problemData->getData<std::vector<std::vector<int>>>("wireless_routeIdByEBs");

    routeThetaVars.clear();
    routeThetaVars.resize(static_cast<size_t>(scenarioCount));
    subProblems.clear();

    const auto subProblemsInitStart = Tools::Clock::now();
    GRBLinExpr routeThetaSum = 0.0;

    for (int scenarioIndex = 0; scenarioIndex < scenarioCount; ++scenarioIndex) {
        const int routeCount = routeCounts[scenarioIndex];
        routeThetaVars[scenarioIndex].resize(static_cast<size_t>(routeCount));

        std::unordered_map<int, std::vector<int>> routeToEbIndices;
        const auto& routeIdByEBs = routeIdByEBsPerScenario[scenarioIndex];
        for (size_t ebIdx = 0; ebIdx < routeIdByEBs.size(); ++ebIdx) {
            routeToEbIndices[routeIdByEBs[ebIdx]].push_back(static_cast<int>(ebIdx));
        }

        for (int routeId = 0; routeId < routeCount; ++routeId) {
            auto it = routeToEbIndices.find(routeId);
            if (it == routeToEbIndices.end()) {
                continue;
            }

            if constexpr (kMultiCut) {
                GRBVar routeTheta = model->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
                    "theta_scenario_" + std::to_string(scenarioIndex) + "_route_" + std::to_string(routeId));
                routeThetaVars[scenarioIndex][routeId] = routeTheta;
                routeThetaSum += routeTheta;
            }

            RouteInfo routeInfo;
            routeInfo.routeId = routeId;
            routeInfo.ebIndices = it->second;
            routeInfo.numEBs = static_cast<int>(routeInfo.ebIndices.size());

            auto sub = std::make_unique<RouteSubProblemState>();
            sub->scenarioIndex = scenarioIndex;
            sub->routeId = routeId;
            sub->strategy = std::make_unique<RouteSubProblem>();
            static_cast<RouteSubProblem*>(sub->strategy.get())->SetRouteInfo(routeInfo);
            sub->env = std::make_unique<GRBEnv>(true);
            sub->env->set(GRB_IntParam_OutputFlag, 0);
            sub->env->start();
            sub->model = std::make_unique<GRBModel>(*sub->env);
            sub->model->set(GRB_IntParam_OutputFlag, 0);
            sub->model->set(GRB_DoubleParam_MIPGap, 0.0);
            sub->model->set(GRB_IntParam_Threads, ParallelConfig::Phase1SubProblemThreads());
            sub->context.Clear();
            sub->strategy->InitSubProblem(*problemData, scenarioIndex, *sub->model, sub->context);
            subProblems.push_back(std::move(sub));
        }
    }

    if constexpr (kMultiCut) {
        model->addConstr(routeThetaSum <= theta, "theta_route_sum");
    }

    const auto subProblemsInitEnd = Tools::Clock::now();
    std::cout << "Relaxed Route sub-problems init count=" << subProblems.size()
              << " elapsed_ms=" << Tools::ElapsedMs(subProblemsInitStart, subProblemsInitEnd) << std::endl;

    model->update();
    initialized = true;
    return OK;
}

LShapeRelaxed::CutEval LShapeRelaxed::BuildContinuousCut(const LShapeCutInfo& cutInfo,
    const std::vector<double>& zCurrent, int iter, int subProblemIndex) const
{
    const BendersCutEvaluation cutEvaluation = BuildBendersCutEvaluation(cutInfo, zVars, zCurrent);

    CutEval cut;
    cut.name = "relaxed_route_cut_continuous_" + std::to_string(iter) + "_sub_" + std::to_string(subProblemIndex);
    cut.expr = cutEvaluation.expr;
    cut.lhsAtCurrent = cutEvaluation.lhsAtCurrent;
    return cut;
}

Status LShapeRelaxed::EvaluateSubProblems(const std::vector<double>& zValues,
    LShapeCutInfo& cutInfo, double& subObj,
    std::vector<LShapeCutInfo>& routeCutInfos)
{
    if (subProblems.empty()) {
        std::cerr << "Relaxed Route sub-problems are not initialized" << std::endl;
        return ERROR;
    }

    cutInfo = LShapeCutInfo{};
    cutInfo.isOptimalityCut = true;
    cutInfo.sense = '>';
    cutInfo.rhs = 0.0;
    cutInfo.yCoeffs.assign(zValues.size(), 0.0);
    cutInfo.constant = 0.0;
    subObj = 0.0;
    routeCutInfos.assign(subProblems.size(), LShapeCutInfo{});

    const ProblemData* problemDataPtr = problemData.get();
    std::vector<SubProblemEvalResult> results;
    try {
        results = RunBoundedIndexedTasks(subProblems.size(),
            ParallelConfig::Phase1SubProblemConcurrency(),
            [problemDataPtr, &zValues, this](size_t idx) {
                auto* sub = subProblems[idx].get();
            return EvaluateRouteSubproblem(*problemDataPtr, *sub->strategy, *sub->model,
                sub->context, zValues);
            });
    }
    catch (const std::exception& e) {
        std::cerr << "Relaxed Route sub-problem failed: " << e.what() << std::endl;
        return ERROR;
    }

    for (size_t idx = 0; idx < results.size(); ++idx) {
        const SubProblemEvalResult& result = results[idx];
        if (result.status != OK) return result.status;

        routeCutInfos[idx] = result.cutInfo;

        if (!result.cutInfo.isOptimalityCut) {
            if (cutInfo.isOptimalityCut) cutInfo = result.cutInfo;
            continue;
        }

        subObj += result.subObj;
        cutInfo.constant += result.cutInfo.constant;
        for (size_t coeffIdx = 0; coeffIdx < zValues.size(); ++coeffIdx)
            cutInfo.yCoeffs[coeffIdx] += result.cutInfo.yCoeffs[coeffIdx];
    }

    return OK;
}

Status LShapeRelaxed::ApplyWarmStart()
{
    bestUpperBound = std::numeric_limits<double>::infinity();
    globalLowerBound = 0.0;
    for (auto& sub : subProblems) {
        sub->strategy->SetLowerBound(0.0);
    }
    return OK;
}

Status LShapeRelaxed::Solve()
{
    algorithmStart = Tools::Clock::now();
    const auto solveStart = algorithmStart;

    Status warmStartStatus = ApplyWarmStart();
    if (warmStartStatus != OK) {
        return warmStartStatus;
    }
    std::cout << "Relaxed Route warm start strategy=" << WarmStartStrategyName()
              << ", elapsed_ms=" << Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) << std::endl;

    model->addConstr(theta >= globalLowerBound, "theta_global_lb");
    model->update();

    for (int iter = 0; iter < maxIters; ++iter) {
        const double elapsedSec = Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) / 1000.0;
        if (elapsedSec >= solverConfig.timeLimit) {
            std::cout << "Relaxed Route reached time limit: " << solverConfig.timeLimit << "s"
                      << ", elapsed_ms=" << Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) << std::endl;
            return ERROR;
        }

        const auto masterStart = Tools::Clock::now();
        model->set(GRB_DoubleParam_MIPGap, solverConfig.mipGap);
        model->optimize();
        const auto masterEnd = Tools::Clock::now();
        const long long masterMs = Tools::ElapsedMs(masterStart, masterEnd);
        int masterStatus = model->get(GRB_IntAttr_Status);
        const int masterSolCount = model->get(GRB_IntAttr_SolCount);
        if (!(masterStatus == GRB_OPTIMAL ||
              (masterStatus == GRB_TIME_LIMIT && masterSolCount > 0))) {
            std::cerr << "Relaxed Route master problem not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }
        if (masterSolCount <= 0) {
            std::cerr << "Relaxed Route master has no incumbent, status: " << masterStatus << std::endl;
            return ERROR;
        }

        const BendersMasterSolution masterSolution = GetBendersMasterSolution(*model, zVars, theta);
        const std::vector<double>& zValues = masterSolution.values;
        const double fixedCost = masterSolution.firstStageValue;
        const double thetaValue = masterSolution.thetaValue;
        bestLowerBound = std::max(bestLowerBound, model->get(GRB_DoubleAttr_ObjBound));

        double aggregatedQValue = 0.0;
        LShapeCutInfo aggregateCutInfo;
        std::vector<LShapeCutInfo> routeCutInfos;
        const auto integerStart = Tools::Clock::now();
        Status secondStageStatus = EvaluateSubProblems(zValues, aggregateCutInfo, aggregatedQValue,
            routeCutInfos);
        const auto integerEnd = Tools::Clock::now();
        const long long integerMs = Tools::ElapsedMs(integerStart, integerEnd);
        if (secondStageStatus != OK) {
            std::cerr << "Relaxed Route second-stage evaluation failed in iteration " << iter << std::endl;
            return ERROR;
        }

        if (!aggregateCutInfo.isOptimalityCut) {
            int feasCount = 0;
            for (size_t idx = 0; idx < routeCutInfos.size(); ++idx) {
                if (routeCutInfos[idx].isOptimalityCut) {
                    continue;
                }
                AddFeasibilityCut(*model, routeCutInfos[idx], zVars,
                    "relaxed_route_feasibility_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                ++feasCount;
            }

            std::cout << "Relaxed Route iter " << iter << ", feasibility cut x" << feasCount;
            PrintBounds(bestUpperBound, bestLowerBound);
            std::cout << ", master_ms=" << masterMs
                      << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, Tools::Clock::now()) << std::endl;

            model->update();
            continue;
        }

        const double candidateObjective = fixedCost + aggregatedQValue;
        if (candidateObjective + tolerance < bestUpperBound) {
            UpdateIncumbent(zValues, aggregatedQValue);
        }

        std::cout << "Relaxed Route iter " << iter << ", theta=" << thetaValue << ", Q(z)=" << aggregatedQValue;
        PrintBounds(bestUpperBound, bestLowerBound);

        const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
        const double relGap = std::isfinite(bestUpperBound)
            ? (bestUpperBound - bestLowerBound) / gapDenom
            : 1.0;
        if (std::isfinite(bestUpperBound) && relGap <= solverConfig.mipGap) {
            std::cout << ", master_ms=" << masterMs
                      << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, Tools::Clock::now()) << std::endl;
            if (!quiet_) {
                PrintBestSolution();
                std::cout << "Relaxed Route converged." << std::endl;
                const auto solveEnd = Tools::Clock::now();
                std::cout << "Relaxed Route final elapsed time: " << Tools::ElapsedMs(solveStart, solveEnd) << " ms" << std::endl;
            }
            return OK;
        }

        for (size_t idx = 0; idx < routeCutInfos.size(); ++idx) {
            const CutEval cutDual = BuildContinuousCut(routeCutInfos[idx], zValues, iter, static_cast<int>(idx));
            const int scenarioIndex = subProblems[idx]->scenarioIndex;
            const int routeId = subProblems[idx]->routeId;
            model->addConstr(cutDual.expr <= routeThetaVars[scenarioIndex][routeId], cutDual.name);
        }

        model->update();
        {
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs
                      << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
        }
    }

    {
        const auto solveEnd = Tools::Clock::now();
        std::cout << "Relaxed Route reached max iterations: " << maxIters
                  << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, solveEnd) << std::endl;
    }
    return ERROR;
}

void LShapeRelaxed::UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue)
{
    incumbentZValues = zValues;
    incumbentSecondStageValue = secondStageValue;

    double fixedCost = 0.0;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        fixedCost += zVars[idx].get(GRB_DoubleAttr_Obj) * zValues[idx];
    }
    bestUpperBound = fixedCost + secondStageValue;
}

void LShapeRelaxed::PrintBestSolution() const
{
    if (incumbentZValues.size() != zVars.size() || quiet_) {
        return;
    }

    std::cout << "Active master variables:" << std::endl;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        if (incumbentZValues[idx] <= 0.5) {
            continue;
        }
        const std::string name = zVars[idx].get(GRB_StringAttr_VarName);
        if (name.rfind("w_", 0) != 0) continue;
        std::cout << name << std::endl;
    }
    std::cout << "Q(z): " << incumbentSecondStageValue << std::endl;
    std::cout << "UB: " << bestUpperBound << ", LB: " << bestLowerBound << std::endl;
}

LShapeRelaxed::~LShapeRelaxed() = default;
