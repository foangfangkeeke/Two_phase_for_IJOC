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
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr bool kMultiCut = true;
constexpr double kSparseCoeffTol = 1e-12;
constexpr double kCutKeyTol = 1e-9;

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

long long QuantizeCutValue(double value)
{
    return static_cast<long long>(std::llround(value / kCutKeyTol));
}

std::vector<int> ExtractBinaryCutTerms(const LShapeCutInfo& cutInfo)
{
    std::vector<int> terms;
    for (size_t idx = 0; idx < cutInfo.yCoeffs.size(); ++idx) {
        if (IsRelevantCoeff(cutInfo.yCoeffs[idx])) {
            terms.push_back(static_cast<int>(idx));
        }
    }
    return terms;
}

double ExtractMonotoneDelta(const LShapeCutInfo& cutInfo)
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
    int scenarioIndex = -1;
    int routeId = -1;
    long long qValue = 0;
    long long delta = 0;
    std::vector<int> terms;

    bool operator<(const MonotoneOptKey& other) const
    {
        return std::tie(scenarioIndex, routeId, qValue, delta, terms)
            < std::tie(other.scenarioIndex, other.routeId, other.qValue, other.delta, other.terms);
    }
};

MonotoneFeasKey BuildMonotoneFeasKey(const LShapeCutInfo& cutInfo)
{
    return MonotoneFeasKey{
        cutInfo.sense,
        QuantizeCutValue(cutInfo.rhs),
        ExtractBinaryCutTerms(cutInfo)
    };
}

MonotoneOptKey BuildMonotoneOptKey(
    int scenarioIndex, int routeId, const LShapeCutInfo& cutInfo)
{
    return MonotoneOptKey{
        scenarioIndex,
        routeId,
        QuantizeCutValue(cutInfo.constant),
        QuantizeCutValue(ExtractMonotoneDelta(cutInfo)),
        ExtractBinaryCutTerms(cutInfo)
    };
}

bool IsFacilityMasterVar(const std::string& name)
{
    return name.rfind("w_station_", 0) == 0 || name.rfind("w_link_", 0) == 0;
}

bool TryParseCapacityVar(const std::string& name, int& scenarioIndex, int& routeId, int& capacity)
{
    if (name.rfind("sc", 0) != 0) {
        return false;
    }

    const size_t ePos = name.find("_E_r");
    const size_t capPos = name.find("_cap");
    if (ePos == std::string::npos || capPos == std::string::npos || capPos <= ePos + 4) {
        return false;
    }

    scenarioIndex = std::stoi(name.substr(2, ePos - 2));
    routeId = std::stoi(name.substr(ePos + 4, capPos - (ePos + 4)));
    capacity = std::stoi(name.substr(capPos + 4));
    return true;
}

LShapeCutInfo BuildRouteStrengthenedNoGoodCut(
    const std::vector<ProblemDataVar>& masterVars, const std::vector<double>& zValues,
    int scenarioIndex, int routeId, const std::vector<std::string>& routeFacilityVarNames)
{
    std::unordered_set<std::string> routeFacilityVarNameSet(
        routeFacilityVarNames.begin(), routeFacilityVarNames.end());

    int selectedCapacity = -1;
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        int varScenario = -1;
        int varRoute = -1;
        int capacity = -1;
        if (TryParseCapacityVar(masterVars[idx].name, varScenario, varRoute, capacity)
            && varScenario == scenarioIndex && varRoute == routeId && zValues[idx] > 0.5) {
            selectedCapacity = capacity;
            break;
        }
    }

    LShapeCutInfo cut;
    cut.isOptimalityCut = false;
    cut.sense = '>';
    cut.rhs = 1.0;
    cut.constant = 0.0;
    cut.yCoeffs.assign(zValues.size(), 0.0);

    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        const std::string& name = masterVars[idx].name;
        if (IsFacilityMasterVar(name)) {
            if (routeFacilityVarNameSet.count(name) > 0 && zValues[idx] <= 0.5) {
                cut.yCoeffs[idx] = 1.0;
            }
            continue;
        }

        int varScenario = -1;
        int varRoute = -1;
        int capacity = -1;
        if (TryParseCapacityVar(name, varScenario, varRoute, capacity)
            && varScenario == scenarioIndex && varRoute == routeId
            && selectedCapacity >= 0 && capacity > selectedCapacity) {
            cut.yCoeffs[idx] = 1.0;
        }
    }

    return cut;
}

LShapeCutInfo BuildRouteMonotonicityStrengthenedOptimalityCut(
    const std::vector<ProblemDataVar>& masterVars, const std::vector<double>& zValues,
    int scenarioIndex, int routeId, const std::vector<std::string>& routeFacilityVarNames,
    double subObj, double lowerBound)
{
    std::unordered_set<std::string> routeFacilityVarNameSet(
        routeFacilityVarNames.begin(), routeFacilityVarNames.end());

    int selectedCapacity = -1;
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        int varScenario = -1;
        int varRoute = -1;
        int capacity = -1;
        if (TryParseCapacityVar(masterVars[idx].name, varScenario, varRoute, capacity)
            && varScenario == scenarioIndex && varRoute == routeId && zValues[idx] > 0.5) {
            selectedCapacity = capacity;
            break;
        }
    }

    const double delta = std::max(0.0, subObj - lowerBound);
    LShapeCutInfo cut;
    cut.isOptimalityCut = true;
    cut.sense = '>';
    cut.rhs = 0.0;
    cut.constant = subObj;
    cut.yCoeffs.assign(zValues.size(), 0.0);

    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        const std::string& name = masterVars[idx].name;
        if (IsFacilityMasterVar(name)) {
            if (routeFacilityVarNameSet.count(name) > 0 && zValues[idx] <= 0.5) {
                cut.yCoeffs[idx] = -delta;
            }
            continue;
        }

        int varScenario = -1;
        int varRoute = -1;
        int capacity = -1;
        if (TryParseCapacityVar(name, varScenario, varRoute, capacity)
            && varScenario == scenarioIndex && varRoute == routeId
            && selectedCapacity >= 0 && capacity > selectedCapacity) {
            cut.yCoeffs[idx] = -delta;
        }
    }

    return cut;
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
    model->set(GRB_DoubleParam_MIPGap, 0.0);
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
    std::vector<double>& routeSubObjs, std::vector<LShapeCutInfo>& routeCutInfos)
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
    routeSubObjs.assign(subProblems.size(), 0.0);
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

        routeSubObjs[idx] = result.subObj;
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
    bestUpperBound = 1e9;
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

    std::set<MonotoneFeasKey> monotoneFeasPool;
    std::set<MonotoneOptKey> monotoneOptPool;

    for (int iter = 0; iter < maxIters; ++iter) {
        int monotoneFeasDedupSkip = 0;
        int monotoneOptDedupSkip = 0;
        const double elapsedSec = Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) / 1000.0;
        if (elapsedSec >= solverConfig.timeLimit) {
            std::cout << "Relaxed Route reached time limit: " << solverConfig.timeLimit << "s"
                      << ", elapsed_ms=" << Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) << std::endl;
            return ERROR;
        }

        const auto masterStart = Tools::Clock::now();
        model->optimize();
        const auto masterEnd = Tools::Clock::now();
        const long long masterMs = Tools::ElapsedMs(masterStart, masterEnd);
        int masterStatus = model->get(GRB_IntAttr_Status);
        if (!(masterStatus == GRB_OPTIMAL ||
              (masterStatus == GRB_TIME_LIMIT && model->get(GRB_IntAttr_SolCount) > 0))) {
            std::cerr << "Relaxed Route master problem not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }

        const BendersMasterSolution masterSolution = GetBendersMasterSolution(*model, zVars, theta);
        const std::vector<double>& zValues = masterSolution.values;
        const double fixedCost = masterSolution.firstStageValue;
        const double thetaValue = masterSolution.thetaValue;
        bestLowerBound = std::max(bestLowerBound, model->get(GRB_DoubleAttr_ObjBound));

        double aggregatedQValue = 0.0;
        LShapeCutInfo aggregateCutInfo;
        std::vector<double> routeQValues;
        std::vector<LShapeCutInfo> routeCutInfos;
        const auto integerStart = Tools::Clock::now();
        Status secondStageStatus = EvaluateSubProblems(zValues, aggregateCutInfo, aggregatedQValue,
            routeQValues, routeCutInfos);
        const auto integerEnd = Tools::Clock::now();
        const long long integerMs = Tools::ElapsedMs(integerStart, integerEnd);
        if (secondStageStatus != OK) {
            std::cerr << "Relaxed Route second-stage evaluation failed in iteration " << iter << std::endl;
            return ERROR;
        }

        if (!aggregateCutInfo.isOptimalityCut) {
            int feasCount = 0;
            const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
            const auto& routeFacilityVarNames = problemData->getData<std::vector<std::vector<std::vector<std::string>>>>(
                "wireless_route_facility_var_names");
            for (size_t idx = 0; idx < routeCutInfos.size(); ++idx) {
                if (routeCutInfos[idx].isOptimalityCut) {
                    continue;
                }
                AddFeasibilityCut(*model, routeCutInfos[idx], zVars,
                    "relaxed_route_feasibility_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                ++feasCount;

                if (useStrengthenedCuts_) {
                    const int scenarioIndex = subProblems[idx]->scenarioIndex;
                    const int routeId = subProblems[idx]->routeId;
                    const LShapeCutInfo noGoodCut = BuildRouteStrengthenedNoGoodCut(
                        masterVars, zValues, scenarioIndex, routeId,
                        routeFacilityVarNames.at(static_cast<size_t>(scenarioIndex)).at(static_cast<size_t>(routeId)));
                    const MonotoneFeasKey noGoodKey = BuildMonotoneFeasKey(noGoodCut);
                    if (monotoneFeasPool.insert(noGoodKey).second) {
                        AddFeasibilityCut(*model, noGoodCut, zVars,
                            "relaxed_route_strengthened_nogood_" + std::to_string(iter)
                            + "_sub_" + std::to_string(idx));
                        ++feasCount;
                    } else {
                        ++monotoneFeasDedupSkip;
                    }
                }
            }

            std::cout << "Relaxed Route iter " << iter << ", feasibility cut x" << feasCount;
            if (monotoneFeasDedupSkip > 0) {
                std::cout << ", monotone_feas_dedup_skip x" << monotoneFeasDedupSkip;
            }
            if (std::isfinite(bestUpperBound)) {
                std::cout << ", UB=" << bestUpperBound << ", LB=" << bestLowerBound;
                const double denom = std::max(1.0, std::fabs(bestUpperBound));
                std::cout << ", gap=" << (bestUpperBound - bestLowerBound) / denom * 100.0 << "%";
            }
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
        if (std::isfinite(bestUpperBound)) {
            std::cout << ", UB=" << bestUpperBound << ", LB=" << bestLowerBound;
            const double denom = std::max(1.0, std::fabs(bestUpperBound));
            std::cout << ", gap=" << (bestUpperBound - bestLowerBound) / denom * 100.0 << "%";
        }

        const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
        const double relGap = (bestUpperBound - bestLowerBound) / gapDenom;
        if (relGap <= solverConfig.mipGap) {
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

        const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
        const auto& routeFacilityVarNames = problemData->getData<std::vector<std::vector<std::vector<std::string>>>>(
            "wireless_route_facility_var_names");
        for (size_t idx = 0; idx < routeCutInfos.size(); ++idx) {
            const CutEval cutDual = BuildContinuousCut(routeCutInfos[idx], zValues, iter, static_cast<int>(idx));
            const int scenarioIndex = subProblems[idx]->scenarioIndex;
            const int routeId = subProblems[idx]->routeId;
            model->addConstr(cutDual.expr <= routeThetaVars[scenarioIndex][routeId], cutDual.name);
            if (useStrengthenedCuts_) {
                const LShapeCutInfo strengthenedCut = BuildRouteMonotonicityStrengthenedOptimalityCut(
                    masterVars, zValues, scenarioIndex, routeId,
                    routeFacilityVarNames.at(static_cast<size_t>(scenarioIndex)).at(static_cast<size_t>(routeId)),
                    routeQValues[idx], globalLowerBound);
                const CutEval strengthenedCutEval = BuildContinuousCut(
                    strengthenedCut, zValues, iter, static_cast<int>(idx));
                const MonotoneOptKey strengthenedKey = BuildMonotoneOptKey(
                    scenarioIndex, routeId, strengthenedCut);
                if (monotoneOptPool.insert(strengthenedKey).second) {
                    model->addConstr(strengthenedCutEval.expr <= routeThetaVars[scenarioIndex][routeId],
                        strengthenedCutEval.name + "_strengthened");
                } else {
                    ++monotoneOptDedupSkip;
                }
            }
        }

        model->update();
        {
            const auto iterEnd = Tools::Clock::now();
            if (monotoneOptDedupSkip > 0) {
                std::cout << ", monotone_opt_dedup_skip x" << monotoneOptDedupSkip;
            }
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
