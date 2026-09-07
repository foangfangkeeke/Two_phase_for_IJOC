#include "lshape_relaxed.hpp"

#include "parallel_utils.hpp"
#include "wireless_reform.hpp"
#include "wireless_reform_route.hpp"
#include "wireless_reform_types.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr bool kMultiCut = true;
constexpr double kSparseCoeffTol = 1e-12;
constexpr double kPhase1DebugChargeTol = 1e-7;

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

struct ParsedStationChargeVar {
    int scenario = -1;
    int eb = -1;
    int trip = -1;
    int station = -1;
    int stationIdx = -1;
};

bool ParseStationChargeVarName(const std::string& name, ParsedStationChargeVar& parsed)
{
    if (name.find("To_n") != std::string::npos || name.find("c_Night") != std::string::npos) {
        return false;
    }

    int consumed = 0;
    const int matched = std::sscanf(name.c_str(), "sc%d_c_b%d_n%d_s%d_i%d%n",
        &parsed.scenario, &parsed.eb, &parsed.trip, &parsed.station, &parsed.stationIdx, &consumed);
    return matched == 5 && consumed == static_cast<int>(name.size());
}

std::vector<std::string> LoadDebugScenarioFolders(const std::string& dataFolder)
{
    const std::filesystem::path scenarioCfg = ResolveDataFolderPath(dataFolder) / "scenarioCfg.txt";
    std::ifstream in(scenarioCfg);
    std::vector<std::string> scenarios;
    bool inRows = false;
    std::string line;
    while (std::getline(in, line)) {
        line = Tools::Trim(line);
        if (line.empty()) continue;
        if (line == "# scenario_folder,probability") {
            inRows = true;
            continue;
        }
        if (!inRows || line[0] == '#') continue;
        scenarios.push_back(Tools::SplitAndTrim(line, ',', true).at(0));
    }
    return scenarios;
}

std::unordered_set<int> LoadDebugPlugInStations(const std::string& dataFolder)
{
    const std::filesystem::path globalCfg = ResolveDataFolderPath(dataFolder) / "globalCfg.txt";
    std::ifstream in(globalCfg);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(Tools::Trim(line));
    }

    std::unordered_set<int> stations;
    for (size_t idx = 0; idx < lines.size(); ++idx) {
        if (lines[idx] != "# ChargerNums") continue;
        const int count = std::stoi(lines.at(idx + 1));
        for (int offset = 0; offset < count; ++offset) {
            const auto tokens = Tools::SplitAndTrim(lines.at(idx + 2 + static_cast<size_t>(offset)), ',', true);
            stations.insert(std::stoi(tokens.at(0)));
        }
        break;
    }
    return stations;
}

double LoadDebugFastChargeRate(const std::string& dataFolder)
{
    const std::filesystem::path globalCfg = ResolveDataFolderPath(dataFolder) / "globalCfg.txt";
    std::ifstream in(globalCfg);
    std::vector<std::vector<std::string>> rows;
    std::string line;
    while (std::getline(in, line)) {
        line = Tools::Trim(line);
        if (line.empty() || line[0] == '#') continue;
        rows.push_back(Tools::SplitAndTrim(line, ',', true));
    }
    return std::stod(rows.at(4).at(0));
}

std::vector<std::vector<int>> LoadDebugIntTable(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::vector<std::vector<int>> rows;
    std::string line;
    while (std::getline(in, line)) {
        line = Tools::Trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<int> row;
        for (const auto& token : Tools::SplitAndTrim(line, ',', true)) {
            row.push_back(std::stoi(token));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

struct DebugScenarioTables {
    std::vector<std::vector<int>> stations;
    std::vector<std::vector<int>> arrTimes;
    std::vector<std::vector<int>> depTimes;
};

DebugScenarioTables LoadDebugScenarioTables(const std::string& dataFolder, const std::string& scenarioFolder)
{
    const std::filesystem::path root = ResolveDataFolderPath(dataFolder) / scenarioFolder;
    return {
        LoadDebugIntTable(root / "tripStations.txt"),
        LoadDebugIntTable(root / "tripArrTimes.txt"),
        LoadDebugIntTable(root / "tripDepTimes.txt")
    };
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

void LShapeRelaxed::DebugExportPhase1PlugInOccupancy(const std::string& outputDir)
{
    if (incumbentZValues.empty()) {
        std::cout << "Phase 1 plug-in occupancy debug skipped: no incumbent z values" << std::endl;
        return;
    }

    LShapeCutInfo aggregateCutInfo;
    double aggregatedQValue = 0.0;
    std::vector<LShapeCutInfo> routeCutInfos;
    std::cout << "Phase 1 plug-in occupancy debug: resolving route LP subproblems and reading subproblem solutions" << std::endl;
    const Status status = EvaluateSubProblems(incumbentZValues, aggregateCutInfo, aggregatedQValue, routeCutInfos);
    if (status != OK) {
        std::cout << "Phase 1 plug-in occupancy debug skipped: route subproblem resolve failed" << std::endl;
        return;
    }

    const std::vector<std::string> scenarioFolders = LoadDebugScenarioFolders(dataFolder);
    const std::unordered_set<int> plugInStations = LoadDebugPlugInStations(dataFolder);
    const double fastRate = LoadDebugFastChargeRate(dataFolder);
    std::vector<DebugScenarioTables> scenarioTables;
    scenarioTables.reserve(scenarioFolders.size());
    for (const std::string& folder : scenarioFolders) {
        scenarioTables.push_back(LoadDebugScenarioTables(dataFolder, folder));
    }

    const std::filesystem::path dir(outputDir);
    std::filesystem::create_directories(dir);
    std::ofstream eventsOut(dir / "phase1_plugin_charge_events.csv");
    std::ofstream timeOut(dir / "phase1_plugin_station_time_occupancy.csv");

    eventsOut << "scenario_index,scenario_folder,route_id,eb,trip,station,station_idx,"
              << "arr_time,dep_time,window_minutes,charge,fast_rate,occupied_minutes_equiv,"
              << "positive_charge,var_name\n";
    timeOut << "scenario_index,scenario_folder,route_id,station,time,"
            << "available_windows,positive_charge_windows,charge_evenly_allocated\n";

    using TimeKey = std::tuple<int, int, int, int>;
    struct TimeAgg {
        int availableWindows = 0;
        int positiveChargeWindows = 0;
        double chargeEvenlyAllocated = 0.0;
    };
    std::map<TimeKey, TimeAgg> timeAgg;

    int exportedEvents = 0;
    int positiveEvents = 0;
    double totalCharge = 0.0;

    for (const auto& sub : subProblems) {
        const int scenarioIndex = sub->scenarioIndex;
        const int routeId = sub->routeId;
        if (scenarioIndex < 0 || static_cast<size_t>(scenarioIndex) >= scenarioTables.size()) {
            continue;
        }

        GRBModel& subModel = *sub->model;
        const int numVars = subModel.get(GRB_IntAttr_NumVars);
        GRBVar* vars = subModel.getVars();
        const DebugScenarioTables& tables = scenarioTables[static_cast<size_t>(scenarioIndex)];
        const std::string& scenarioFolder = scenarioFolders[static_cast<size_t>(scenarioIndex)];

        for (int varIdx = 0; varIdx < numVars; ++varIdx) {
            const std::string varName = vars[varIdx].get(GRB_StringAttr_VarName);
            ParsedStationChargeVar parsed;
            if (!ParseStationChargeVarName(varName, parsed)) {
                continue;
            }
            if (plugInStations.count(parsed.station) == 0 || parsed.stationIdx != 0) {
                continue;
            }
            if (parsed.trip < 0 || static_cast<size_t>(parsed.trip) >= tables.stations.size()) {
                continue;
            }
            if (parsed.stationIdx < 0 ||
                static_cast<size_t>(parsed.stationIdx) >= tables.stations[static_cast<size_t>(parsed.trip)].size()) {
                continue;
            }

            const int arrTime = tables.arrTimes[static_cast<size_t>(parsed.trip)][static_cast<size_t>(parsed.stationIdx)];
            const int depTime = tables.depTimes[static_cast<size_t>(parsed.trip)][static_cast<size_t>(parsed.stationIdx)];
            const int windowMinutes = depTime - arrTime;
            const double charge = vars[varIdx].get(GRB_DoubleAttr_X);
            const double occupiedMinutesEquiv = fastRate > 0.0 ? charge / fastRate : 0.0;
            const bool positiveCharge = charge > kPhase1DebugChargeTol;

            eventsOut << scenarioIndex << ',' << scenarioFolder << ',' << routeId << ','
                      << parsed.eb << ',' << parsed.trip << ',' << parsed.station << ','
                      << parsed.stationIdx << ',' << arrTime << ',' << depTime << ','
                      << windowMinutes << ',' << std::setprecision(12) << charge << ','
                      << fastRate << ',' << occupiedMinutesEquiv << ','
                      << (positiveCharge ? 1 : 0) << ',' << varName << '\n';

            ++exportedEvents;
            if (positiveCharge) {
                ++positiveEvents;
                totalCharge += charge;
            }

            for (int time = arrTime; time < depTime; ++time) {
                TimeAgg& agg = timeAgg[TimeKey{scenarioIndex, routeId, parsed.station, time}];
                ++agg.availableWindows;
                if (positiveCharge) {
                    ++agg.positiveChargeWindows;
                    if (windowMinutes > 0) {
                        agg.chargeEvenlyAllocated += charge / static_cast<double>(windowMinutes);
                    }
                }
            }
        }
        delete[] vars;
    }

    for (const auto& [key, agg] : timeAgg) {
        const auto& [scenarioIndex, routeId, station, time] = key;
        const std::string& scenarioFolder = scenarioFolders[static_cast<size_t>(scenarioIndex)];
        timeOut << scenarioIndex << ',' << scenarioFolder << ',' << routeId << ','
                << station << ',' << time << ',' << agg.availableWindows << ','
                << agg.positiveChargeWindows << ',' << std::setprecision(12)
                << agg.chargeEvenlyAllocated << '\n';
    }

    std::cout << "Phase 1 plug-in occupancy debug exported to " << dir.string()
              << ": events=" << exportedEvents
              << ", positive_events=" << positiveEvents
              << ", positive_charge=" << totalCharge << std::endl;
}

LShapeRelaxed::~LShapeRelaxed() = default;
