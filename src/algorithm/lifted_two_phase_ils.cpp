#include "lifted_two_phase_ils.hpp"
#include "lshape_monotonicity_cut.hpp"
#include "lshape_relaxed.hpp"
#include "parallel_utils.hpp"
#include "runtime_options.hpp"
#include "wireless_reform.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {
constexpr double kSparseCoeffTol = 1e-12;
constexpr double kCutKeyTol = 1e-9;

namespace fs = std::filesystem;

bool IsRelevantCoeff(double coeff)
{
    return std::fabs(coeff) > kSparseCoeffTol;
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
    throw std::invalid_argument("Unsupported feasibility cut sense");
}

bool MeetsOuterGap(double bestUpperBound, double bestLowerBound, const SolverConfig& solverConfig)
{
    if (!std::isfinite(bestUpperBound)) return false;
    const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
    const double relGap = (bestUpperBound - bestLowerBound) / gapDenom;
    return relGap <= solverConfig.mipGap;
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

bool StartsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool TryParseIntAfter(const std::string& value, const std::string& marker, size_t startPos, int& out)
{
    const size_t markerPos = value.find(marker, startPos);
    if (markerPos == std::string::npos) return false;
    size_t pos = markerPos + marker.size();
    size_t end = pos;
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) {
        ++end;
    }
    if (end == pos) return false;
    out = std::stoi(value.substr(pos, end - pos));
    return true;
}

bool ParseBatteryVarName(const std::string& name, int& scenarioIndex, int& routeId, int& capacity)
{
    if (!StartsWith(name, "sc")) return false;
    size_t pos = 2;
    size_t end = pos;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) ++end;
    if (end == pos || name.compare(end, 4, "_E_r") != 0) return false;
    scenarioIndex = std::stoi(name.substr(pos, end - pos));

    pos = end + 4;
    end = pos;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) ++end;
    if (end == pos || name.compare(end, 4, "_cap") != 0) return false;
    routeId = std::stoi(name.substr(pos, end - pos));

    pos = end + 4;
    end = pos;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) ++end;
    if (end == pos) return false;
    capacity = std::stoi(name.substr(pos, end - pos));
    return true;
}

struct FullSolutionSummary {
    double facilityCost = 0.0;
    double stationFacilityCost = 0.0;
    double linkFacilityCost = 0.0;
    double batteryCost = 0.0;
    int selectedStationCount = 0;
    int selectedLinkCount = 0;
    double expectedFastChargeKwh = 0.0;
    double expectedStaticWirelessKwh = 0.0;
    double expectedDynamicWirelessKwh = 0.0;
    double expectedNightChargeKwh = 0.0;
    double expectedTotalChargeKwh = 0.0;
    double expectedOperationalCost = 0.0;
};

const std::map<ProblemType, std::tuple<DataInitFactory, SubFactory>> kStrategyMap = {
    {
        WIRELESS_CAPACITY_REFORM,
        std::make_tuple(
            [](const std::string& dataFolder) {
                return std::make_unique<WirelessReformDataInit>(dataFolder);
            },
            []() { return std::make_unique<WirelessScenarioSubProblem>(); })
    }
};

SubProblemEvalResult EvaluateScenarioSubproblem(const ProblemData& problemData,
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

struct LiftedTwoPhaseILS::ScenarioSubProblemState {
    int scenarioSubproblemId = 0;
    int scenarioIndex = 0;
    ScenarioSubproblemInfo scenarioInfo;
    std::unique_ptr<ISubProblem_LShape> strategy;
    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    LShapeSubProblemContext context;
};

LiftedTwoPhaseILS::LiftedTwoPhaseILS(ProblemType problemType, std::string dataFolder,
    int maxIters, double tol, bool useRspCuts, bool useMonotoneCuts)
    : problemType(problemType),
      dataFolder(std::move(dataFolder)),
      maxIters(maxIters),
      tolerance(tol),
      useRspCuts(useRspCuts),
      useMonotoneCuts(useMonotoneCuts),
      globalLowerBound(0.0),
      bestUpperBound(std::numeric_limits<double>::infinity()),
      bestLowerBound(-std::numeric_limits<double>::infinity()),
      incumbentSecondStageValue(std::numeric_limits<double>::infinity())
{}

LiftedTwoPhaseILS::~LiftedTwoPhaseILS() = default;

Status LiftedTwoPhaseILS::Initialize()
{
    if (initialized) return OK;

    fullAlgorithmStart = Tools::Clock::now();

    // ── Phase 1: operational relaxation ──
    LShapeRelaxed phase1(problemType, dataFolder, maxIters, tolerance);
    phase1.SetSolverConfig(solverConfig);
    phase1.SetQuiet(true);

    Status s = phase1.Initialize();
    if (s != OK) { std::cerr << "Phase 1 init failed" << std::endl; return s; }

    s = phase1.Solve();
    if (s != OK) { std::cerr << "Phase 1 solve failed" << std::endl; return s; }
    const auto phase1End = Tools::Clock::now();
    phase1ElapsedMs = Tools::ElapsedMs(fullAlgorithmStart, phase1End);
    std::cout << "Phase 1 elapsed time: " << phase1ElapsedMs << " ms" << std::endl;
    if (useMonotoneCuts) {
        const RuntimeOptions& runtimeOptions = GetRuntimeOptions();
        const std::filesystem::path debugDir = std::filesystem::path("results")
            / "full_phase1_plugin_debug"
            / (dataFolder + "_sc" + std::to_string(runtimeOptions.scenarioCount)
                + "_cap" + std::to_string(runtimeOptions.chargerCapacity));
        phase1.DebugExportPhase1PlugInOccupancy(debugDir.string());
    }
    if (phase1ElapsedMs / 1000.0 >= solverConfig.timeLimit) {
        std::cout << "Two-phase reached time limit after Phase 1; skip Phase 2 initialization." << std::endl;
        std::cout << "Total elapsed time (Phase 1 + Phase 2): " << phase1ElapsedMs << " ms" << std::endl;
        return ERROR;
    }

    // Capture Phase 1 state
    phase1ZValues = phase1.GetIncumbentZValues();
    env = phase1.ReleaseEnv();
    model = phase1.GetModel();
    problemDataOwned = phase1.ReleaseProblemData();
    problemData = problemDataOwned.get();
    zVars = phase1.GetZVars();
    theta = model->getVarByName("theta");

    if (!model || !problemData) {
        std::cerr << "Phase 1 did not produce a valid model" << std::endl;
        return ERROR;
    }

    bestLowerBound = phase1.GetBestLowerBound();
    bestUpperBound = std::numeric_limits<double>::infinity();

    std::cout << std::endl << "-- Phase 2: MIP verification (reusing Phase 1 master + all cuts) --" << std::endl;

    // ── Build scenario subproblems ──
    const auto& scenarioSubproblems = problemData->getData<std::vector<ScenarioSubproblemInfo>>(
        "wireless_scenario_subproblems");

    if (scenarioSubproblems.empty()) {
        std::cerr << "No scenario subproblems found" << std::endl;
        return ERROR;
    }

    RemovePhase1GlobalRouteThetaLink(*model);

    scenarioThetaVars.clear();
    scenarioThetaVars.reserve(scenarioSubproblems.size());
    GRBLinExpr scenarioThetaSum = 0.0;
    for (size_t idx = 0; idx < scenarioSubproblems.size(); ++idx) {
        GRBVar gTheta = model->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
            "scenario_theta_p2_" + std::to_string(idx));
        scenarioThetaVars.push_back(gTheta);
        scenarioThetaSum += gTheta;
        AddScenarioRouteThetaLink(*model, scenarioSubproblems[idx], gTheta, idx);
    }
    model->addConstr(scenarioThetaSum <= theta, "theta_scenario_sum_p2");

    subProblems.clear();
    subProblems.reserve(scenarioSubproblems.size());

    phase2Start = Tools::Clock::now();

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
        subProblems.push_back(std::move(sub));
    }

    // Bounded parallel init
    {
        const ProblemData* pd = problemData;
        try {
            RunBoundedIndexedTasks(subProblems.size(),
                ParallelConfig::Phase2SubProblemConcurrency(),
                [pd, this](size_t idx) {
                    auto* sub = subProblems[idx].get();
                    sub->strategy->InitSubProblem(*pd, static_cast<int>(idx),
                        *sub->model, sub->context);
                    return OK;
                });
        }
        catch (const std::exception& e) {
            std::cerr << "Phase 2 sub-problem init failed: " << e.what() << std::endl;
            return ERROR;
        }
        for (size_t idx = 0; idx < subProblems.size(); ++idx) {
            std::cout << "  done, cols=" << subProblems[idx]->model->get(GRB_IntAttr_NumVars)
                      << " rows=" << subProblems[idx]->model->get(GRB_IntAttr_NumConstrs) << std::endl;
        }
    }

    const auto phase2InitEnd = Tools::Clock::now();
    std::cout << "Phase 2 init elapsed_ms=" << Tools::ElapsedMs(phase2Start, phase2InitEnd)
              << " subProblems=" << subProblems.size() << std::endl;

    model->update();
    initialized = true;
    return OK;
}

LiftedTwoPhaseILS::CutEval LiftedTwoPhaseILS::BuildContinuousCut(const LShapeCutInfo& cutInfo,
    const std::vector<double>& zCurrent, int iter, int subProblemIndex) const
{
    const BendersCutEvaluation cutEvaluation = BuildBendersCutEvaluation(cutInfo, zVars, zCurrent);
    CutEval cut;
    cut.name = "lifted_two_phase_cut_" + std::to_string(iter) + "_sub_" + std::to_string(subProblemIndex);
    cut.expr = cutEvaluation.expr;
    cut.lhsAtCurrent = cutEvaluation.lhsAtCurrent;
    return cut;
}

Status LiftedTwoPhaseILS::EvaluateSubProblems(const std::vector<double>& zValues,
    LShapeCutInfo& cutInfo, double& subObj,
    std::vector<double>& scenarioSubObjs, std::vector<LShapeCutInfo>& scenarioCutInfos,
    bool relaxed)
{
    if (subProblems.empty()) {
        std::cerr << "Lifted two-phase ILS operational sub-problems are not initialized" << std::endl;
        return ERROR;
    }

    cutInfo = LShapeCutInfo{};
    cutInfo.isOptimalityCut = true;
    cutInfo.sense = '>';
    cutInfo.rhs = 0.0;
    cutInfo.yCoeffs.assign(zValues.size(), 0.0);
    cutInfo.constant = 0.0;
    subObj = 0.0;
    scenarioSubObjs.assign(subProblems.size(), 0.0);
    scenarioCutInfos.assign(subProblems.size(), LShapeCutInfo{});

    const ProblemData* problemDataPtr = problemData;
    std::vector<SubProblemEvalResult> results;
    try {
        results = RunBoundedIndexedTasks(subProblems.size(),
            ParallelConfig::Phase2SubProblemConcurrency(),
            [problemDataPtr, &zValues, relaxed, this](size_t idx) {
                auto& sub = subProblems[idx];
                return EvaluateScenarioSubproblem(*problemDataPtr,
                    *sub->strategy, *sub->model, sub->context, zValues, relaxed);
            });
    }
    catch (const std::exception& e) {
        std::cerr << "Lifted two-phase ILS operational sub-problem failed: " << e.what() << std::endl;
        return ERROR;
    }

    for (size_t idx = 0; idx < results.size(); ++idx) {
        const SubProblemEvalResult& result = results[idx];
        if (result.status != OK) return result.status;

        scenarioSubObjs[idx] = result.subObj;
        scenarioCutInfos[idx] = result.cutInfo;

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

Status LiftedTwoPhaseILS::ApplyWarmStart()
{
    bestUpperBound = std::numeric_limits<double>::infinity();
    globalLowerBound = 0.0;
    for (auto& sub : subProblems) {
        sub->strategy->SetLowerBound(0.0);
    }
    return OK;
}

void LiftedTwoPhaseILS::UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue)
{
    bestUpperBound = 0.0;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        bestUpperBound += zVars[idx].get(GRB_DoubleAttr_Obj) * zValues[idx];
    }
    bestUpperBound += secondStageValue;
    incumbentZValues = zValues;
    incumbentSecondStageValue = secondStageValue;
}

void LiftedTwoPhaseILS::PrintBestSolution() const
{
    std::cout << "UB: " << bestUpperBound
              << ", LB: " << bestLowerBound << std::endl;
    if (!std::isfinite(bestUpperBound)) return;
    std::cout << "Active master variables:" << std::endl;
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        if (incumbentZValues[idx] > 0.5) {
            const std::string name = zVars[idx].get(GRB_StringAttr_VarName);
            if (name.rfind("w_", 0) != 0) continue;
            std::cout << "  " << name << std::endl;
        }
    }
    std::cout << "Q(z): " << incumbentSecondStageValue << std::endl;
}

Status LiftedTwoPhaseILS::Solve()
{
    for (size_t idx = 0; idx < zVars.size(); ++idx) {
        zVars[idx].set(GRB_DoubleAttr_Start, phase1ZValues[idx]);
    }
    model->update();
    std::cout << "Phase 2 MIP start applied, starting Benders, RSP="
              << (useRspCuts ? "on" : "off")
              << ", monotone_cuts=" << (useMonotoneCuts ? "on" : "off") << std::endl;

    const auto& scenarioSubproblems = problemData->getData<std::vector<ScenarioSubproblemInfo>>(
        "wireless_scenario_subproblems");
    const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
    const auto& facilityVarNames = problemData->getData<std::vector<std::vector<std::vector<std::string>>>>(
        "wireless_route_facility_var_names");
    std::set<MonotoneFeasKey> monotoneFeasPool;
    std::set<MonotoneOptKey> monotoneOptPool;

    // ── Phase 2: MIP verification with classic cuts ──
    for (int iter = 0; iter < maxIters; ++iter) {
        int monotoneFeasDedupSkip = 0;
        int monotoneOptDedupSkip = 0;
        const double elapsedSec = Tools::ElapsedMs(fullAlgorithmStart, Tools::Clock::now()) / 1000.0;
        if (elapsedSec >= solverConfig.timeLimit) {
            std::cout << "Phase 2 reached time limit: " << solverConfig.timeLimit << "s" << std::endl;
            {
                const long long phase2Ms = Tools::ElapsedMs(phase2Start, Tools::Clock::now());
                std::cout << "Phase 2 elapsed time: " << phase2Ms << " ms" << std::endl;
                std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                          << (phase1ElapsedMs + phase2Ms) << " ms" << std::endl;
            }
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
            std::cerr << "Phase 2 master not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }
        if (masterSolCount <= 0) {
            std::cerr << "Phase 2 master has no incumbent, status: " << masterStatus << std::endl;
            return ERROR;
        }
        const double masterBound = model->get(GRB_DoubleAttr_ObjBound);
        bestLowerBound = std::max(bestLowerBound, masterBound);

        const BendersMasterSolution masterSolution = GetBendersMasterSolution(*model, zVars, theta);
        const std::vector<double>& zValues = masterSolution.values;
        const double fixedCost = masterSolution.firstStageValue;
        const double thetaValue = masterSolution.thetaValue;

        if (useRspCuts) {
            double relaxedQValue = 0.0;
            LShapeCutInfo relaxedAggregateCutInfo;
            std::vector<double> relaxedScenarioQValues;
            std::vector<LShapeCutInfo> relaxedScenarioCutInfos;
            const auto rspStart = Tools::Clock::now();
            Status relaxedStatus = EvaluateSubProblems(zValues, relaxedAggregateCutInfo, relaxedQValue,
                relaxedScenarioQValues, relaxedScenarioCutInfos, true);
            if (relaxedStatus != OK) {
                std::cerr << "Phase 2 relaxed sub-problem evaluation failed iter " << iter << std::endl;
                return ERROR;
            }

            int rspFeasCount = 0;
            int rspOptCount = 0;
            bool rspAnyInfeasible = false;
            std::set<std::string> addedRSPFeasibilityCuts;
            for (size_t idx = 0; idx < relaxedScenarioCutInfos.size(); ++idx) {
                if (!relaxedScenarioCutInfos[idx].isOptimalityCut) {
                    rspAnyInfeasible = true;
                    const std::string cs = MakeFeasibilityCutSignature(relaxedScenarioCutInfos[idx]);
                    if (addedRSPFeasibilityCuts.insert(cs).second) {
                        AddFeasibilityCut(*model, relaxedScenarioCutInfos[idx], zVars,
                            "p2_rsp_feas_" + std::to_string(iter) + "_" + std::to_string(idx));
                        ++rspFeasCount;
                    }
                    continue;
                }

                if (scenarioThetaVars[idx].get(GRB_DoubleAttr_X) + tolerance < relaxedScenarioQValues[idx]) {
                    const CutEval cutDual = BuildContinuousCut(
                        relaxedScenarioCutInfos[idx], zValues, iter, static_cast<int>(idx));
                    model->addConstr(cutDual.expr <= scenarioThetaVars[idx],
                        "p2_rsp_opt_" + std::to_string(iter) + "_" + std::to_string(idx));
                    ++rspOptCount;
                }
            }

            if (rspAnyInfeasible) {
                model->update();
                std::cout << "Phase 2 iter " << iter << ", RSP feasibility cut x" << rspFeasCount
                          << ", RSP optimality cut x" << rspOptCount;
                PrintBounds(bestUpperBound, bestLowerBound);
                std::cout << ", master_ms=" << masterMs
                          << ", rsp_eval_ms=" << Tools::ElapsedMs(rspStart, Tools::Clock::now())
                          << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start, Tools::Clock::now()) << std::endl;
                if (std::isfinite(bestUpperBound)) {
                    const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
                    const double relGap = (bestUpperBound - bestLowerBound) / gapDenom;
                    if (relGap <= solverConfig.mipGap) {
                        PrintBestSolution();
                        const long long phase2Ms = Tools::ElapsedMs(phase2Start, Tools::Clock::now());
                        phase2ElapsedMs = phase2Ms;
                        std::cout << "Phase 2 converged." << std::endl;
                        std::cout << "Phase 2 final elapsed time: " << phase2Ms << " ms" << std::endl;
                        std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                                  << (phase1ElapsedMs + phase2Ms) << " ms" << std::endl;
                        ExportIncumbentSolution();
                        return OK;
                    }
                }
                continue;
            }
            if (rspOptCount > 0) model->update();
        } else {
            std::cout << "Phase 2 iter " << iter << " RSP disabled, skip LP relaxation" << std::endl;
        }

        double aggregatedQValue = 0.0;
        LShapeCutInfo aggregateCutInfo;
        std::vector<double> scenarioQValues;
        std::vector<LShapeCutInfo> scenarioCutInfos;
        Status secondStageStatus = EvaluateSubProblems(zValues, aggregateCutInfo, aggregatedQValue,
            scenarioQValues, scenarioCutInfos, false);
        if (secondStageStatus != OK) {
            std::cerr << "Phase 2 sub-problem evaluation failed iter " << iter << std::endl;
            return ERROR;
        }

        int mipFeasCount = 0;
        bool mipAnyInfeasible = false;
        std::set<std::string> addedMIPFeasibilityCuts;
        for (size_t idx = 0; idx < scenarioCutInfos.size(); ++idx) {
            if (scenarioCutInfos[idx].isOptimalityCut) continue;
            mipAnyInfeasible = true;
            if (useMonotoneCuts) {
                const ScenarioSubproblemInfo& scenarioInfo = scenarioSubproblems[idx];
                const int scIdx = scenarioInfo.scenarioIndex;
                std::vector<std::string> scenarioFacilityNames;
                for (int rid : scenarioInfo.routeIds) {
                    const auto& routeFacilityNames = facilityVarNames[scIdx][rid];
                    scenarioFacilityNames.insert(scenarioFacilityNames.end(),
                        routeFacilityNames.begin(), routeFacilityNames.end());
                }
                const BendersCutInfo noGood = BuildScenarioStrengthenedNoGoodCut(
                    masterVars, zValues, scIdx, scenarioInfo.routeIds, scenarioFacilityNames);
                const MonotoneFeasKey noGoodKey = BuildMonotoneFeasKey(noGood);
                if (monotoneFeasPool.insert(noGoodKey).second) {
                    AddFeasibilityCut(*model, noGood, zVars,
                        "p2_sfeas_" + std::to_string(iter) + "_" + std::to_string(idx));
                    ++mipFeasCount;
                } else {
                    ++monotoneFeasDedupSkip;
                }
            } else {
                const std::string cs = MakeFeasibilityCutSignature(scenarioCutInfos[idx]);
                if (addedMIPFeasibilityCuts.insert(cs).second) {
                    AddFeasibilityCut(*model, scenarioCutInfos[idx], zVars,
                        "p2_feas_" + std::to_string(iter) + "_" + std::to_string(idx));
                    ++mipFeasCount;
                }
            }
        }

        if (mipAnyInfeasible) {
            std::cout << "Phase 2 iter " << iter
                      << (useMonotoneCuts ? ", MIP infeasible, strengthened feasibility cut x"
                                          : ", feasibility cut x")
                      << mipFeasCount;
            if (monotoneFeasDedupSkip > 0) {
                std::cout << ", monotone_feas_dedup_skip x" << monotoneFeasDedupSkip;
            }
            PrintBounds(bestUpperBound, bestLowerBound);
            model->update();
            std::cout << ", master_ms=" << masterMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start, Tools::Clock::now()) << std::endl;
            if (MeetsOuterGap(bestUpperBound, bestLowerBound, solverConfig)) {
                PrintBestSolution();
                const long long phase2Ms = Tools::ElapsedMs(phase2Start, Tools::Clock::now());
                phase2ElapsedMs = phase2Ms;
                std::cout << "Phase 2 converged." << std::endl;
                std::cout << "Phase 2 final elapsed time: " << phase2Ms << " ms" << std::endl;
                std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                          << (phase1ElapsedMs + phase2Ms) << " ms" << std::endl;
                ExportIncumbentSolution();
                return OK;
            }
            continue;
        }

        const double candidateObjective = fixedCost + aggregatedQValue;
        if (candidateObjective + tolerance < bestUpperBound) {
            UpdateIncumbent(zValues, aggregatedQValue);
        }

        std::cout << "Phase 2 iter " << iter << ", theta=" << thetaValue
                  << ", Q(z)=" << aggregatedQValue;
        PrintBounds(bestUpperBound, bestLowerBound);

        const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
        const double relGap = std::isfinite(bestUpperBound)
            ? (bestUpperBound - bestLowerBound) / gapDenom
            : 1.0;
        if (std::isfinite(bestUpperBound) && relGap <= solverConfig.mipGap) {
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start, iterEnd) << std::endl;
            PrintBestSolution();
            {
                const long long phase2Ms = Tools::ElapsedMs(phase2Start, Tools::Clock::now());
                phase2ElapsedMs = phase2Ms;
                std::cout << "Phase 2 converged." << std::endl;
                std::cout << "Phase 2 final elapsed time: " << phase2Ms << " ms" << std::endl;
                std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                          << (phase1ElapsedMs + phase2Ms) << " ms" << std::endl;
            }
            ExportIncumbentSolution();
            return OK;
        }

        // Exact MIP optimality cuts: ordinary pointwise cuts or the monotone component.
        for (size_t idx = 0; idx < scenarioCutInfos.size(); ++idx) {
            if (!scenarioCutInfos[idx].isOptimalityCut) continue;
            LShapeCutInfo optCutInfo = scenarioCutInfos[idx];
            if (useMonotoneCuts) {
                const ScenarioSubproblemInfo& scenarioInfo = scenarioSubproblems[idx];
                const int scIdx = scenarioInfo.scenarioIndex;
                std::vector<std::string> scenarioFacilityNames;
                for (int rid : scenarioInfo.routeIds) {
                    const auto& routeFacilityNames = facilityVarNames[scIdx][rid];
                    scenarioFacilityNames.insert(scenarioFacilityNames.end(),
                        routeFacilityNames.begin(), routeFacilityNames.end());
                }
                optCutInfo = BuildMonotonicityStrengthenedOptimalityCut(
                    masterVars, zValues, scIdx, scenarioInfo.routeIds, scenarioFacilityNames,
                    scenarioQValues[idx], globalLowerBound);
                const MonotoneOptKey optKey = BuildMonotoneOptKey(idx, optCutInfo);
                if (!monotoneOptPool.insert(optKey).second) {
                    ++monotoneOptDedupSkip;
                    continue;
                }
            }
            const CutEval cutDual = BuildContinuousCut(optCutInfo, zValues, iter, static_cast<int>(idx));
            model->addConstr(cutDual.expr <= scenarioThetaVars[idx], cutDual.name);
        }

        model->update();
        if (monotoneOptDedupSkip > 0) {
            std::cout << ", monotone_opt_dedup_skip x" << monotoneOptDedupSkip;
        }
        std::cout << ", master_ms=" << masterMs
                  << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start, Tools::Clock::now()) << std::endl;
    }

    PrintBestSolution();
    {
        const long long phase2Ms = Tools::ElapsedMs(phase2Start, Tools::Clock::now());
        phase2ElapsedMs = phase2Ms;
        std::cout << "Phase 2 reached max iterations: " << maxIters << std::endl;
        std::cout << "Phase 2 elapsed time: " << phase2Ms << " ms" << std::endl;
        std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                  << (phase1ElapsedMs + phase2Ms) << " ms" << std::endl;
    }
    return ERROR;
}

void LiftedTwoPhaseILS::ExportIncumbentSolution() const
{
    if (!exportSolution && !exportSummary) return;
    if (!problemData || incumbentZValues.empty()) return;

    const fs::path exportDir = exportDirectory.empty()
        ? fs::path("results") / dataFolder / (useMonotoneCuts ? "full_solution" : "two_phase_solution")
        : fs::path(exportDirectory);
    fs::create_directories(exportDir);

    const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
    const auto& scenarioSubproblems = problemData->getData<std::vector<ScenarioSubproblemInfo>>(
        "wireless_scenario_subproblems");
    const auto& scenarioProbabilities = problemData->getData<std::vector<double>>(
        "wireless_scenario_probabilities");
    const auto& existingChargerStations = problemData->getData<std::vector<int>>(
        "wireless_existing_charger_stations");
    const std::unordered_set<int> existingChargerStationSet(
        existingChargerStations.begin(), existingChargerStations.end());

    FullSolutionSummary summary;

    if (exportSolution) {
        std::ofstream out(exportDir / "solution_metadata.csv");
        out << std::fixed << std::setprecision(6);
        out << "metric,value\n";
        out << "best_upper_bound," << bestUpperBound << '\n';
        out << "best_lower_bound," << bestLowerBound << '\n';
        out << "phase1_elapsed_ms," << phase1ElapsedMs << '\n';
        out << "phase2_elapsed_ms," << phase2ElapsedMs << '\n';
        out << "total_elapsed_ms," << (phase1ElapsedMs + phase2ElapsedMs) << '\n';
    }

    if (exportSolution) {
        std::ofstream out(exportDir / "master_variables.csv");
        out << std::fixed << std::setprecision(6);
        out << "index,var_name,value,obj,lb,ub,vtype\n";
        for (size_t idx = 0; idx < masterVars.size(); ++idx) {
            const double value = incumbentZValues[idx];
            out << idx << ',' << CsvEscape(masterVars[idx].name) << ',' << value << ','
                << masterVars[idx].obj << ',' << masterVars[idx].lb << ','
                << masterVars[idx].ub << ',' << masterVars[idx].type << '\n';
        }
    }

    if (exportSummary) {
        std::ofstream out(exportDir / "selected_facilities.csv");
        out << std::fixed << std::setprecision(6);
        out << "facility_type,facility_id,from_node,to_node,var_name,selected,unit_cost,cost\n";

        std::ofstream batteryOut(exportDir / "route_battery_summary.csv");
        batteryOut << std::fixed << std::setprecision(6);
        batteryOut << "scenario_index,route_id,probability,battery_capacity,var_name,selected,cost\n";

        for (size_t idx = 0; idx < masterVars.size(); ++idx) {
            const std::string& name = masterVars[idx].name;
            const double value = incumbentZValues[idx];
            const double cost = masterVars[idx].obj * value;

            if (StartsWith(name, "w_station_s")) {
                if (value > 0.5) {
                    ++summary.selectedStationCount;
                    summary.facilityCost += cost;
                    summary.stationFacilityCost += cost;
                    const std::string stationId = name.substr(std::string("w_station_s").size());
                    out << "station," << stationId << ",,," << CsvEscape(name) << ','
                        << value << ',' << masterVars[idx].obj << ',' << cost << '\n';
                }
                continue;
            }

            if (StartsWith(name, "w_link_l")) {
                if (value > 0.5) {
                    ++summary.selectedLinkCount;
                    summary.facilityCost += cost;
                    summary.linkFacilityCost += cost;
                    const size_t prefixLen = std::string("w_link_l").size();
                    const size_t toPos = name.find("To", prefixLen);
                    const std::string fromNode = name.substr(prefixLen, toPos - prefixLen);
                    const std::string toNode = name.substr(toPos + 2);
                    out << "link,," << fromNode << ',' << toNode << ',' << CsvEscape(name) << ','
                        << value << ',' << masterVars[idx].obj << ',' << cost << '\n';
                }
                continue;
            }

            int scenarioIndex = 0;
            int routeId = 0;
            int capacity = 0;
            if (ParseBatteryVarName(name, scenarioIndex, routeId, capacity)) {
                summary.batteryCost += cost;
                if (value > 0.5) {
                    const double probability =
                        scenarioIndex >= 0 && static_cast<size_t>(scenarioIndex) < scenarioProbabilities.size()
                        ? scenarioProbabilities[static_cast<size_t>(scenarioIndex)]
                        : 0.0;
                    batteryOut << scenarioIndex << ',' << routeId << ',' << probability << ','
                               << capacity << ',' << CsvEscape(name) << ',' << value << ','
                               << cost << '\n';
                }
            }
        }
    }

    std::ofstream statusOut;
    std::ofstream varOut;
    std::ofstream scenarioOut;
    if (exportSolution) {
        statusOut.open(exportDir / "subproblem_status.csv");
        statusOut << std::fixed << std::setprecision(6);
        statusOut << "subproblem_index,scenario_index,status,obj\n";

        varOut.open(exportDir / "subproblem_variables.csv");
        varOut << std::fixed << std::setprecision(6);
        varOut << "subproblem_index,scenario_index,var_index,var_name,value,obj,lb,ub,vtype\n";
    }
    if (exportSummary) {
        scenarioOut.open(exportDir / "scenario_summary.csv");
        scenarioOut << std::fixed << std::setprecision(6);
        scenarioOut << "subproblem_index,scenario_index,probability,status,q_weighted,"
                    << "q_unweighted,fast_charge_kwh,expected_fast_charge_kwh,"
                    << "static_wireless_charge_kwh,expected_static_wireless_charge_kwh,"
                    << "dynamic_wireless_charge_kwh,expected_dynamic_wireless_charge_kwh,"
                    << "night_charge_kwh,expected_night_charge_kwh,total_charge_kwh,"
                    << "expected_total_charge_kwh\n";
    }

    for (size_t idx = 0; idx < subProblems.size(); ++idx) {
        auto* sub = subProblems[idx].get();
        sub->strategy->UpdateSubProblem(*problemData, *sub->model, sub->context, incumbentZValues);
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
        const double probability =
            scenarioIndex >= 0 && static_cast<size_t>(scenarioIndex) < scenarioProbabilities.size()
            ? scenarioProbabilities[static_cast<size_t>(scenarioIndex)]
            : 0.0;

        if (exportSolution) {
            statusOut << idx << ',' << scenarioIndex << ',' << status << ',' << obj << '\n';
        }

        if (status != GRB_OPTIMAL) {
            if (exportSolution) {
                std::cout << "Solution export skipped variables for scenario subproblem " << idx
                          << " because status=" << status << std::endl;
            }
            if (exportSummary) {
                scenarioOut << idx << ',' << scenarioIndex << ',' << probability << ',' << status
                            << ',' << obj << ",0,0,0,0,0,0,0,0,0,0,0\n";
            }
            continue;
        }

        double fastChargeKwh = 0.0;
        double staticWirelessChargeKwh = 0.0;
        double dynamicWirelessChargeKwh = 0.0;
        double nightChargeKwh = 0.0;

        const int numVars = sub->model->get(GRB_IntAttr_NumVars);
        GRBVar* vars = sub->model->getVars();
        for (int vIdx = 0; vIdx < numVars; ++vIdx) {
            const std::string name = vars[vIdx].get(GRB_StringAttr_VarName);
            const double value = vars[vIdx].get(GRB_DoubleAttr_X);
            const double objCoeff = vars[vIdx].get(GRB_DoubleAttr_Obj);
            const double lb = vars[vIdx].get(GRB_DoubleAttr_LB);
            const double ub = vars[vIdx].get(GRB_DoubleAttr_UB);
            const char vtype = vars[vIdx].get(GRB_CharAttr_VType);

            if (exportSolution) {
                varOut << idx << ',' << scenarioIndex << ',' << vIdx << ','
                       << CsvEscape(name) << ',' << value << ',' << objCoeff << ','
                       << lb << ',' << ub << ',' << vtype << '\n';
            }

            if (!exportSummary || value <= 1e-7) continue;
            const size_t firstUnderscore = name.find('_');
            const std::string localName = firstUnderscore == std::string::npos
                ? name : name.substr(firstUnderscore + 1);
            if (StartsWith(localName, "c_Night_")) {
                nightChargeKwh += value;
            } else if (StartsWith(localName, "c_")) {
                if (localName.find("To_") != std::string::npos) {
                    dynamicWirelessChargeKwh += value;
                } else {
                    int station = -1;
                    if (TryParseIntAfter(localName, "_s", 0, station)
                        && existingChargerStationSet.count(station) > 0) {
                        fastChargeKwh += value;
                    } else {
                        staticWirelessChargeKwh += value;
                    }
                }
            }
        }
        delete[] vars;

        if (exportSummary) {
            const double totalChargeKwh = fastChargeKwh + staticWirelessChargeKwh
                + dynamicWirelessChargeKwh + nightChargeKwh;
            const double qUnweighted = probability > 0.0 ? obj / probability : 0.0;
            summary.expectedOperationalCost += obj;
            summary.expectedFastChargeKwh += probability * fastChargeKwh;
            summary.expectedStaticWirelessKwh += probability * staticWirelessChargeKwh;
            summary.expectedDynamicWirelessKwh += probability * dynamicWirelessChargeKwh;
            summary.expectedNightChargeKwh += probability * nightChargeKwh;
            summary.expectedTotalChargeKwh += probability * totalChargeKwh;

            scenarioOut << idx << ',' << scenarioIndex << ',' << probability << ',' << status
                        << ',' << obj << ',' << qUnweighted << ','
                        << fastChargeKwh << ',' << probability * fastChargeKwh << ','
                        << staticWirelessChargeKwh << ',' << probability * staticWirelessChargeKwh << ','
                        << dynamicWirelessChargeKwh << ',' << probability * dynamicWirelessChargeKwh << ','
                        << nightChargeKwh << ',' << probability * nightChargeKwh << ','
                        << totalChargeKwh << ',' << probability * totalChargeKwh << '\n';
        }
    }

    if (exportSummary) {
        const RuntimeOptions& runtimeOptions = GetRuntimeOptions();
        const double fixedCost = summary.facilityCost + summary.batteryCost;
        const double finalGap = std::isfinite(bestUpperBound)
            ? (bestUpperBound - bestLowerBound) / std::max(1.0, std::fabs(bestUpperBound))
            : 1.0;

        std::ofstream out(exportDir / "solution_summary.csv");
        out << std::fixed << std::setprecision(6);
        out << "metric,value\n";
        out << "algorithm," << (useMonotoneCuts ? "full" : "lifted-two-phase") << '\n';
        out << "data_folder," << CsvEscape(dataFolder) << '\n';
        out << "scenario_count," << runtimeOptions.scenarioCount << '\n';
        out << "cap," << runtimeOptions.chargerCapacity << '\n';
        out << "facility_cost_scale," << runtimeOptions.facilityCostScale << '\n';
        out << "energy_scale," << runtimeOptions.energyScale << '\n';
        out << "rsp," << (useRspCuts ? 1 : 0) << '\n';
        out << "monotone_cuts," << (useMonotoneCuts ? 1 : 0) << '\n';
        out << "best_upper_bound," << bestUpperBound << '\n';
        out << "best_lower_bound," << bestLowerBound << '\n';
        out << "final_gap," << finalGap << '\n';
        out << "facility_cost," << summary.facilityCost << '\n';
        out << "station_facility_cost," << summary.stationFacilityCost << '\n';
        out << "link_facility_cost," << summary.linkFacilityCost << '\n';
        out << "expected_battery_cost," << summary.batteryCost << '\n';
        out << "fixed_cost," << fixedCost << '\n';
        out << "expected_operational_cost," << summary.expectedOperationalCost << '\n';
        out << "selected_station_count," << summary.selectedStationCount << '\n';
        out << "selected_link_count," << summary.selectedLinkCount << '\n';
        out << "expected_fast_charge_kwh," << summary.expectedFastChargeKwh << '\n';
        out << "expected_static_wireless_kwh," << summary.expectedStaticWirelessKwh << '\n';
        out << "expected_dynamic_wireless_kwh," << summary.expectedDynamicWirelessKwh << '\n';
        out << "expected_night_charge_kwh," << summary.expectedNightChargeKwh << '\n';
        out << "expected_total_charge_kwh," << summary.expectedTotalChargeKwh << '\n';
    }

    std::cout << "Solution export written to " << exportDir.string() << std::endl;
}
