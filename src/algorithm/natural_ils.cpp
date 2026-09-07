#include "natural_ils.hpp"

#include "wireless.hpp"
#include "parallel_utils.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
constexpr double kSparseCoeffTol = 1e-12;

bool IsRelevantCoeff(double coeff)
{
    return std::fabs(coeff) > kSparseCoeffTol;
}

using DataInitFactory = std::function<std::unique_ptr<IDataInitializationStrategy_IntegerLShaped>(const std::string&)>;
using SubFactory = std::function<std::unique_ptr<ISubProblemStrategy_IntegerLShaped>()>;

struct SubProblemEvalResult {
    Status status = ERROR;
    IntegerLShapedCutInfo cutInfo;
    double subObj = 0.0;
};

static const std::map<ProblemType, std::tuple<DataInitFactory, SubFactory>> kStrategyMap = {
    {
        WIRELESS_CHARGING_NO_PLUGIN,
        std::make_tuple(
            [](const std::string& dataFolder) {
                return std::make_unique<WirelessChargingDataInitializationStrategy_LShaped>(dataFolder);
            },
            []() { return std::make_unique<WirelessChargingSubProblemStrategy_LShaped>(); })
    }
};

SubProblemEvalResult EvaluateScenarioSubproblem(const ProblemData& problemData,
    ISubProblemStrategy_IntegerLShaped& strategy, GRBModel& subModel,
    IntegerLShapedSubProblemContext& context, const std::vector<double>& zValues, bool relaxed)
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

void AddFeasibilityCut(GRBModel& model, const IntegerLShapedCutInfo& cutInfo,
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

    throw std::invalid_argument("Unsupported feasibility cut sense in integer L-shaped framework");
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

struct IntegerLShaped::ScenarioSubProblem {
    int scenarioIndex = 0;
    std::unique_ptr<ISubProblemStrategy_IntegerLShaped> strategy;
    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    IntegerLShapedSubProblemContext context;
};

IntegerLShaped::IntegerLShaped(ProblemType problemType, std::string dataFolder,
    int maxIters, double tol, bool useRspCuts)
    : problemType(problemType), dataFolder(dataFolder), maxIters(maxIters), tolerance(tol),
        useRspCuts(useRspCuts), globalLowerBound(0.0),
        bestUpperBound(std::numeric_limits<double>::infinity()),
        bestLowerBound(-std::numeric_limits<double>::infinity()),
        incumbentSecondStageValue(std::numeric_limits<double>::infinity())
{}

Status IntegerLShaped::Initialize()
{
    env = std::make_unique<GRBEnv>(true);
    env->set(GRB_IntParam_OutputFlag, 0);
    env->set("LogFile", "gurobi_log.txt");
    env->start();
    model = std::make_shared<GRBModel>(*env);
    model->set(GRB_IntParam_OutputFlag, 0);

    auto it = kStrategyMap.find(problemType);
    if (it == kStrategyMap.end()) {
        throw std::invalid_argument("Unsupported problem type for integer L-shaped framework");
    }

    dataIniter = std::get<0>(it->second)(dataFolder);
    subProblemStrategy = std::get<1>(it->second)();
    if (!subProblemStrategy) {
        throw std::invalid_argument("Sub-problem strategy cannot be null");
    }

    problemData = std::make_unique<ProblemData>();
    const auto dataInitStart = Tools::Clock::now();
    dataIniter->DataInit(*problemData);
    const std::vector<ProblemDataConstr> masterConstrs = dataIniter->ConstrInit(*problemData);
    const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
    InitializeBendersMasterModel(*model, masterVars, masterConstrs, zVars, theta, true);
    model->set(GRB_DoubleParam_MIPGap, solverConfig.mipGap);
    model->set(GRB_IntParam_Threads, ParallelConfig::MasterThreads());
    const auto dataInitEnd = Tools::Clock::now();
    std::cout << "Natural ILS data/master initialization elapsed_ms="
              << Tools::ElapsedMs(dataInitStart, dataInitEnd) << std::endl;

    subProblems.clear();
    const int scenarioCount = subProblemStrategy->ScenarioCount(*problemData);
    if (scenarioCount <= 0) {
        throw std::runtime_error("Natural ILS requires at least one scenario sub-problem.");
    }

    const auto subProblemsInitStart = Tools::Clock::now();
    subProblems.reserve(static_cast<size_t>(scenarioCount));
    for (int scenarioIndex = 0; scenarioIndex < scenarioCount; ++scenarioIndex) {
        auto sub = std::make_unique<ScenarioSubProblem>();
        sub->scenarioIndex = scenarioIndex;
        sub->strategy = subProblemStrategy->Clone();
        sub->env = std::make_unique<GRBEnv>(true);
        sub->env->set(GRB_IntParam_OutputFlag, 0);
        sub->env->start();
        sub->model = std::make_unique<GRBModel>(*sub->env);
        sub->model->set(GRB_IntParam_OutputFlag, 0);
        sub->model->set(GRB_DoubleParam_MIPGap, 0.0);
        sub->model->set(GRB_IntParam_Threads, ParallelConfig::Phase2SubProblemThreads());
        sub->context.Clear();
        sub->strategy->InitSubProblem(*problemData, scenarioIndex, *sub->model, sub->context);
        subProblems.push_back(std::move(sub));
    }
    const auto subProblemsInitEnd = Tools::Clock::now();
    std::cout << "Natural ILS sub-problems initialization elapsed_ms="
              << Tools::ElapsedMs(subProblemsInitStart, subProblemsInitEnd) << std::endl;

    model->update();

    initialized = true;
    return OK;
}

IntegerLShaped::CutEval IntegerLShaped::BuildImprovedCut(const std::vector<double>& zValues,
    const std::vector<double>& zCurrent, double delta, double lowerBound, int iter) const
{
    CutEval cut;
    cut.name = "integer_l_shaped_cut_improved_" + std::to_string(iter);
    cut.expr = lowerBound + delta;
    cut.lhsAtCurrent = lowerBound + delta;

    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        if (zValues[idx] > 0.5) {
            continue;
        }
        cut.expr += -delta * zVars[idx];
        cut.lhsAtCurrent += -delta * zCurrent[idx];
    }
    return cut;
}

IntegerLShaped::CutEval IntegerLShaped::BuildPriorityCut(const std::vector<double>& zValues,
    const std::vector<double>& zCurrent, double delta, double lowerBound, int iter) const
{
    CutEval cut;
    cut.name = "integer_l_shaped_cut_priority_" + std::to_string(iter);

    double maxObj = 0.0;
    for (const auto& zVar : zVars) {
        maxObj = std::max(maxObj, std::max(0.0, zVar.get(GRB_DoubleAttr_Obj)));
    }
    if (maxObj < 1e-9) {
        maxObj = 1.0;
    }

    std::vector<double> xi(zVars.size(), 1.0);
    double sumXiStar = 0.0;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        xi[idx] = 1.0 + std::max(0.0, zVars[idx].get(GRB_DoubleAttr_Obj)) / maxObj;
        if (zValues[idx] > 0.5) {
            sumXiStar += xi[idx];
        }
    }

    cut.expr = lowerBound + delta * (1.0 - sumXiStar);
    cut.lhsAtCurrent = lowerBound + delta * (1.0 - sumXiStar);

    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        const double coeff = (zValues[idx] > 0.5) ? delta * xi[idx] : -delta * xi[idx];
        cut.expr += coeff * zVars[idx];
        cut.lhsAtCurrent += coeff * zCurrent[idx];
    }

    return cut;
}

IntegerLShaped::CutEval IntegerLShaped::BuildClassicCut(const std::vector<double>& zValues,
    const std::vector<double>& zCurrent, double delta, double lowerBound, int iter) const
{
    CutEval cut;
    cut.name = "integer_l_shaped_cut_classic_" + std::to_string(iter);

    cut.expr = lowerBound + delta;
    cut.lhsAtCurrent = lowerBound + delta;

    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        if (zValues[idx] > 0.5) {
            continue;
        }
        cut.expr += -delta * zVars[idx];
        cut.lhsAtCurrent += -delta * zCurrent[idx];
    }

    return cut;
}

IntegerLShaped::CutEval IntegerLShaped::BuildContinuousCut(const IntegerLShapedCutInfo& cutInfo,
    const std::vector<double>& zCurrent, int iter, int scenarioIndex) const
{
    const BendersCutEvaluation cutEvaluation = BuildBendersCutEvaluation(cutInfo, zVars, zCurrent);

    CutEval cut;
    cut.name = "integer_l_shaped_cut_continuous_" + std::to_string(iter);
    if (scenarioIndex >= 0) {
        cut.name += "_scenario_" + std::to_string(scenarioIndex);
    }
    cut.expr = cutEvaluation.expr;
    cut.lhsAtCurrent = cutEvaluation.lhsAtCurrent;
    return cut;
}

Status IntegerLShaped::EvaluateSubProblems(const std::vector<double>& zValues, bool relaxed,
    IntegerLShapedCutInfo& cutInfo, double& subObj, std::vector<double>* scenarioSubObjs,
    std::vector<IntegerLShapedCutInfo>* scenarioCutInfos)
{
    if (subProblems.empty()) {
        std::cerr << "Natural ILS sub-problems are not initialized" << std::endl;
        return ERROR;
    }

    const ProblemData* problemDataPtr = problemData.get();
    std::vector<SubProblemEvalResult> results;
    try {
        results = RunBoundedIndexedTasks(subProblems.size(),
            ParallelConfig::Phase2SubProblemConcurrency(),
            [problemDataPtr, &zValues, relaxed, this](size_t idx) {
                auto* sub = subProblems[idx].get();
            return EvaluateScenarioSubproblem(*problemDataPtr, *sub->strategy, *sub->model,
                sub->context, zValues, relaxed);
            });
    }
    catch (const std::exception& e) {
        std::cerr << "Natural ILS sub-problem failed: " << e.what() << std::endl;
        return ERROR;
    }

    cutInfo = IntegerLShapedCutInfo{};
    cutInfo.isOptimalityCut = true;
    cutInfo.sense = '>';
    cutInfo.rhs = 0.0;
    cutInfo.yCoeffs.assign(zValues.size(), 0.0);
    cutInfo.constant = 0.0;
    subObj = 0.0;
    if (scenarioSubObjs != nullptr) {
        scenarioSubObjs->assign(subProblems.size(), 0.0);
    }
    if (scenarioCutInfos != nullptr) {
        scenarioCutInfos->assign(subProblems.size(), IntegerLShapedCutInfo{});
    }

    for (size_t scenarioResultIdx = 0; scenarioResultIdx < results.size(); ++scenarioResultIdx) {
        const SubProblemEvalResult& result = results[scenarioResultIdx];
        if (result.status != OK) {
            return result.status;
        }

        if (scenarioSubObjs != nullptr) {
            (*scenarioSubObjs)[scenarioResultIdx] = result.subObj;
        }
        if (scenarioCutInfos != nullptr) {
            (*scenarioCutInfos)[scenarioResultIdx] = result.cutInfo;
        }
        if (!result.cutInfo.isOptimalityCut) {
            if (scenarioSubObjs != nullptr && scenarioCutInfos == nullptr) {
                throw std::runtime_error("Natural ILS scenario lower bounds require optimal sub-problem results");
            }
            cutInfo = result.cutInfo;
            subObj = result.subObj;
            return OK;
        }

        if (result.cutInfo.yCoeffs.size() != zValues.size()) {
            std::cerr << "Natural ILS cut size mismatch" << std::endl;
            return ERROR;
        }

        subObj += result.subObj;
        cutInfo.constant += result.cutInfo.constant;
        for (size_t idx = 0; idx < zValues.size(); ++idx) {
            cutInfo.yCoeffs[idx] += result.cutInfo.yCoeffs[idx];
        }
    }

    return OK;
}

Status IntegerLShaped::ApplyWarmStart()
{
    double warmL = 0.0;
    std::vector<double> scenarioLs(subProblems.size(), 0.0);
    bestUpperBound = std::numeric_limits<double>::infinity();

    globalLowerBound = warmL;
    for (size_t idx = 0; idx < subProblems.size(); ++idx) {
        subProblems[idx]->strategy->SetLowerBound(scenarioLs[idx]);
    }

    return OK;
}

Status IntegerLShaped::Solve()
{
    algorithmStart = Tools::Clock::now();
    const auto solveStart = algorithmStart;

    Status warmStartStatus = ApplyWarmStart();
    if (warmStartStatus != OK) {
        return warmStartStatus;
    }
    std::cout << "Natural ILS warm start strategy=zero lower bound"
              << ", RSP=" << (useRspCuts ? "on" : "off")
              << ", elapsed_ms=" << Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) << std::endl;

    model->addConstr(theta >= globalLowerBound, "theta_global_lb");
    model->update();

    for (int iter = 0; iter < maxIters; ++iter) {
        const double elapsedSec = Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) / 1000.0;
        if (elapsedSec >= solverConfig.timeLimit) {
            std::cout << "Natural ILS reached time limit: " << solverConfig.timeLimit << "s"
                      << ", elapsed_ms=" << Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) << std::endl;
            return ERROR;
        }

        model->set(GRB_DoubleParam_MIPGap, solverConfig.mipGap);
        const auto masterStart = Tools::Clock::now();
        model->optimize();
        const auto masterEnd = Tools::Clock::now();
        const long long masterMs = Tools::ElapsedMs(masterStart, masterEnd);
        int masterStatus = model->get(GRB_IntAttr_Status);
        const int masterSolCount = model->get(GRB_IntAttr_SolCount);
        if (!(masterStatus == GRB_OPTIMAL ||
              (masterStatus == GRB_TIME_LIMIT && masterSolCount > 0))) {
            std::cerr << "Natural ILS master problem not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }
        if (masterSolCount <= 0) {
            std::cerr << "Natural ILS master has no incumbent, status: " << masterStatus << std::endl;
            return ERROR;
        }
        bestLowerBound = std::max(bestLowerBound, model->get(GRB_DoubleAttr_ObjBound));

        const BendersMasterSolution masterSolution = GetBendersMasterSolution(*model, zVars, theta);
        const std::vector<double>& zValues = masterSolution.values;
        const double fixedCost = masterSolution.firstStageValue;
        const double thetaValue = masterSolution.thetaValue;

        if (useRspCuts) {
            double relaxedQValue = 0.0;
            IntegerLShapedCutInfo relaxedCutInfo;
            const auto rspStart = Tools::Clock::now();
            Status relaxedStatus = EvaluateSubProblems(zValues, true, relaxedCutInfo, relaxedQValue);
            const auto rspEnd = Tools::Clock::now();
            const long long rspMs = Tools::ElapsedMs(rspStart, rspEnd);
            if (relaxedStatus != OK) {
                std::cerr << "Natural ILS RSP evaluation failed in iteration " << iter << std::endl;
                return ERROR;
            }

            if (!relaxedCutInfo.isOptimalityCut) {
                AddFeasibilityCut(*model, relaxedCutInfo, zVars,
                    "integer_l_shaped_rsp_feasibility_" + std::to_string(iter));

                std::cout << "Natural ILS iter " << iter << ", RSP feasibility cut";
                PrintBounds(bestUpperBound, bestLowerBound);

                model->update();
                const auto iterEnd = Tools::Clock::now();
                std::cout << ", master_ms=" << masterMs
                          << ", rsp_ms=" << rspMs
                          << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
                continue;
            }

            if (thetaValue + tolerance < relaxedQValue) {
                const CutEval relaxedCut = BuildContinuousCut(relaxedCutInfo, zValues, iter);
                model->addConstr(relaxedCut.expr <= theta,
                    "integer_l_shaped_rsp_opt_" + std::to_string(iter));
                model->update();
            }
        }

        double aggregatedQValue = 0.0;
        IntegerLShapedCutInfo integerCutInfo;
        const auto integerStart = Tools::Clock::now();
        Status secondStageStatus = EvaluateSubProblems(zValues, false, integerCutInfo, aggregatedQValue);
        const auto integerEnd = Tools::Clock::now();
        const long long integerMs = Tools::ElapsedMs(integerStart, integerEnd);
        if (secondStageStatus != OK) {
            std::cerr << "Natural ILS second-stage evaluation failed in iteration " << iter << std::endl;
            return ERROR;
        }

        if (!integerCutInfo.isOptimalityCut) {
            AddFeasibilityCut(*model, integerCutInfo, zVars, "integer_l_shaped_feasibility_" + std::to_string(iter));

            std::cout << "Natural ILS iter " << iter << ", feasibility cut";
            PrintBounds(bestUpperBound, bestLowerBound);

            model->update();
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs

                      << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
            continue;
        }

        const double candidateObjective = fixedCost + aggregatedQValue;
        if (candidateObjective + tolerance < bestUpperBound) {
            UpdateIncumbent(zValues, aggregatedQValue);
        }

        double delta = std::max(0.0, aggregatedQValue - globalLowerBound);

        std::cout << "Natural ILS iter " << iter << ", theta=" << thetaValue << ", Q(z)=" << aggregatedQValue;
        PrintBounds(bestUpperBound, bestLowerBound);

        const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
        const double relGap = std::isfinite(bestUpperBound)
            ? (bestUpperBound - bestLowerBound) / gapDenom
            : 1.0;
        if (std::isfinite(bestUpperBound) && relGap <= solverConfig.mipGap) {
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs

                      << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
            PrintBestSolution();
            std::cout << "Natural ILS converged." << std::endl;
            const auto solveEnd = Tools::Clock::now();
            std::cout << "Natural ILS final elapsed time: " << Tools::ElapsedMs(solveStart, solveEnd) << " ms" << std::endl;
            return OK;
        }

        const CutEval cutPriority = BuildPriorityCut(zValues, zValues, delta, globalLowerBound, iter);
        model->addConstr(cutPriority.expr <= theta, cutPriority.name);

        model->update();
        const auto iterEnd = Tools::Clock::now();
        std::cout << ", master_ms=" << masterMs

                  << ", integer_ms=" << integerMs
                  << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
    }

    const auto solveEnd = Tools::Clock::now();
    std::cout << "Natural ILS reached max iterations: " << maxIters
              << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, solveEnd) << std::endl;
    return ERROR;
}

void IntegerLShaped::UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue)
{
    incumbentZValues = zValues;
    incumbentSecondStageValue = secondStageValue;

    double fixedCost = 0.0;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        fixedCost += zVars[idx].get(GRB_DoubleAttr_Obj) * zValues[idx];
    }
    bestUpperBound = fixedCost + secondStageValue;
}

void IntegerLShaped::PrintBestSolution() const
{
    if (incumbentZValues.size() != zVars.size()) {
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

IntegerLShaped::~IntegerLShaped() = default;
