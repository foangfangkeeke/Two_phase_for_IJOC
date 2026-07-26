#include "lifted_two_phase_ils.hpp"
#include "lshape_monotonicity_cut.hpp"
#include "lshape_relaxed.hpp"
#include "parallel_utils.hpp"
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
constexpr double kSparseCoeffTol = 1e-12;

bool IsRelevantCoeff(double coeff)
{
    return std::fabs(coeff) > kSparseCoeffTol;
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

    scenarioThetaVars.clear();
    scenarioThetaVars.reserve(scenarioSubproblems.size());
    GRBLinExpr scenarioThetaSum = 0.0;
    for (size_t idx = 0; idx < scenarioSubproblems.size(); ++idx) {
        GRBVar gTheta = model->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
            "scenario_theta_p2_" + std::to_string(idx));
        scenarioThetaVars.push_back(gTheta);
        scenarioThetaSum += gTheta;
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
    bestUpperBound = 1e9;
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
    std::cout << "Phase 2 MIP start applied, starting Benders..." << std::endl;

    model->addConstr(theta >= globalLowerBound, "theta_global_lb");
    model->update();

    // ── Phase 2: MIP verification with classic cuts ──
    for (int iter = 0; iter < maxIters; ++iter) {
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

        const auto masterStart = Tools::Clock::now();
        model->optimize();
        const auto masterEnd = Tools::Clock::now();
        const long long masterMs = Tools::ElapsedMs(masterStart, masterEnd);
        int masterStatus = model->get(GRB_IntAttr_Status);
        if (!(masterStatus == GRB_OPTIMAL ||
              (masterStatus == GRB_TIME_LIMIT && model->get(GRB_IntAttr_SolCount) > 0))) {
            std::cerr << "Phase 2 master not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }

        const BendersMasterSolution masterSolution = GetBendersMasterSolution(*model, zVars, theta);
        const std::vector<double>& zValues = masterSolution.values;
        const double fixedCost = masterSolution.firstStageValue;
        const double thetaValue = masterSolution.thetaValue;
        bestLowerBound = std::max(bestLowerBound, model->get(GRB_DoubleAttr_ObjBound));

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
            const std::string cs = MakeFeasibilityCutSignature(scenarioCutInfos[idx]);
            if (addedMIPFeasibilityCuts.insert(cs).second) {
                AddFeasibilityCut(*model, scenarioCutInfos[idx], zVars,
                    "p2_feas_" + std::to_string(iter) + "_" + std::to_string(idx));
                ++mipFeasCount;
            }
        }

        if (mipAnyInfeasible) {
            std::cout << "Phase 2 iter " << iter << ", feasibility cut x" << mipFeasCount;
            if (std::isfinite(bestUpperBound)) {
                const double denom = std::max(1.0, std::fabs(bestUpperBound));
                std::cout << ", UB=" << bestUpperBound << ", LB=" << bestLowerBound
                          << ", gap=" << (bestUpperBound - bestLowerBound) / denom * 100.0 << "%";
            }
            model->update();
            std::cout << ", master_ms=" << masterMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start, Tools::Clock::now()) << std::endl;
            continue;
        }

        const double candidateObjective = fixedCost + aggregatedQValue;
        if (candidateObjective + tolerance < bestUpperBound) {
            UpdateIncumbent(zValues, aggregatedQValue);
        }

        std::cout << "Phase 2 iter " << iter << ", theta=" << thetaValue
                  << ", Q(z)=" << aggregatedQValue;
        if (std::isfinite(bestUpperBound)) {
            const double denom = std::max(1.0, std::fabs(bestUpperBound));
            std::cout << ", UB=" << bestUpperBound << ", LB=" << bestLowerBound
                      << ", gap=" << (bestUpperBound - bestLowerBound) / denom * 100.0 << "%";
        }

        const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
        const double relGap = (bestUpperBound - bestLowerBound) / gapDenom;
        if (relGap <= solverConfig.mipGap) {
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start, iterEnd) << std::endl;
            PrintBestSolution();
            {
                const long long phase2Ms = Tools::ElapsedMs(phase2Start, Tools::Clock::now());
                std::cout << "Phase 2 converged." << std::endl;
                std::cout << "Phase 2 final elapsed time: " << phase2Ms << " ms" << std::endl;
                std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                          << (phase1ElapsedMs + phase2Ms) << " ms" << std::endl;
            }
            return OK;
        }

        // Classic LP dual continuous optimality cuts (vs. strengthened in full)
        for (size_t idx = 0; idx < scenarioCutInfos.size(); ++idx) {
            if (!scenarioCutInfos[idx].isOptimalityCut) continue;
            const CutEval cutDual = BuildContinuousCut(scenarioCutInfos[idx], zValues, iter, static_cast<int>(idx));
            model->addConstr(cutDual.expr <= scenarioThetaVars[idx], cutDual.name);
        }

        model->update();
        std::cout << ", master_ms=" << masterMs
                  << ", elapsed_ms=" << Tools::ElapsedMs(phase2Start, Tools::Clock::now()) << std::endl;
    }

    PrintBestSolution();
    {
        const long long phase2Ms = Tools::ElapsedMs(phase2Start, Tools::Clock::now());
        std::cout << "Phase 2 reached max iterations: " << maxIters << std::endl;
        std::cout << "Phase 2 elapsed time: " << phase2Ms << " ms" << std::endl;
        std::cout << "Total elapsed time (Phase 1 + Phase 2): "
                  << (phase1ElapsedMs + phase2Ms) << " ms" << std::endl;
    }
    return ERROR;
}
