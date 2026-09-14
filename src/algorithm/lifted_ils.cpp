#include "lifted_ils.hpp"
#include "lshape_monotonicity_cut.hpp"
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

struct LiftedILS::ScenarioSubProblemState {
    int scenarioSubproblemId = 0;
    int scenarioIndex = 0;
    std::unique_ptr<ISubProblem_LShape> strategy;
    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    LShapeSubProblemContext context;
};

LiftedILS::LiftedILS(ProblemType problemType, std::string dataFolder,
    int maxIters, double tol, bool useMonotoneCuts, bool useRspCuts)
    : problemType(problemType),
      dataFolder(std::move(dataFolder)),
      maxIters(maxIters),
      tolerance(tol),
      useMonotoneCuts(useMonotoneCuts),
      useRspCuts(useRspCuts),
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
    model->set(GRB_IntParam_Presolve, solverConfig.presolve);
    model->set(GRB_IntParam_Method, solverConfig.method);
    model->set(GRB_IntParam_Threads, ParallelConfig::MasterThreads());

    const auto& scenarioSubproblems = problemData->getData<std::vector<ScenarioSubproblemInfo>>("wireless_scenario_subproblems");
    scenarioThetaVars.clear();
    scenarioThetaVars.reserve(scenarioSubproblems.size());
    GRBLinExpr thetaSum = 0.0;
    for (size_t subIdx = 0; subIdx < scenarioSubproblems.size(); ++subIdx) {
        GRBVar t = model->addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
            "theta_scenario" + std::to_string(subIdx));
        scenarioThetaVars.push_back(t);
        thetaSum += t;
    }
    model->addConstr(thetaSum <= theta, "theta_multi_sum");

    const auto dataInitEnd = Tools::Clock::now();
    std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
              << " data/master initialization elapsed_ms="
              << Tools::ElapsedMs(dataInitStart, dataInitEnd) << std::endl;

    const auto subProblemsInitStart = Tools::Clock::now();
    subProblems.clear();

    for (size_t subIdx = 0; subIdx < scenarioSubproblems.size(); ++subIdx) {
        const ScenarioSubproblemInfo& scenarioInfo = scenarioSubproblems[subIdx];

        auto sub = std::make_unique<ScenarioSubProblemState>();
        sub->scenarioSubproblemId = scenarioInfo.scenarioSubproblemId;
        sub->scenarioIndex = scenarioInfo.scenarioIndex;
        sub->strategy = std::get<1>(it->second)();
        static_cast<WirelessScenarioSubProblem*>(sub->strategy.get())->SetScenarioSubproblemInfo(scenarioInfo);
        sub->env = std::make_unique<GRBEnv>(true);
        sub->env->set(GRB_IntParam_OutputFlag, 0);
        sub->env->start();
        sub->model = std::make_unique<GRBModel>(*sub->env);
        sub->model->set(GRB_IntParam_OutputFlag, 0);
        sub->model->set(GRB_IntParam_Presolve, solverConfig.presolve);
        sub->model->set(GRB_IntParam_Method, solverConfig.method);
        sub->model->set(GRB_IntParam_Threads, ParallelConfig::Phase2SubProblemThreads());
        sub->context.Clear();
        sub->strategy->InitSubProblem(*problemData, static_cast<int>(subIdx), *sub->model, sub->context);
        subProblems.push_back(std::move(sub));
    }

    const auto subProblemsInitEnd = Tools::Clock::now();
    std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
              << " operational sub-problems init count=" << subProblems.size()
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
    std::vector<double>& scenarioSubObjs, std::vector<LShapeCutInfo>& scenarioCutInfos,
    bool relaxed)
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
    scenarioSubObjs.assign(subProblems.size(), 0.0);
    scenarioCutInfos.assign(subProblems.size(), LShapeCutInfo{});

    const ProblemData* problemDataPtr = problemData.get();
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
        std::cerr << "Lifted ILS operational sub-problem failed: " << e.what() << std::endl;
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

Status LiftedILS::ApplyWarmStart()
{
    bestUpperBound = std::numeric_limits<double>::infinity();
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
            const std::string name = zVars[idx].get(GRB_StringAttr_VarName);
            if (name.rfind("w_", 0) != 0) continue;
            std::cout << "  " << name << std::endl;
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

    std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
              << " starting Benders, RSP=" << (useRspCuts ? "on" : "off") << std::endl;

    model->addConstr(theta >= globalLowerBound, "theta_global_lb");
    model->update();

    for (int iter = 0; iter < maxIters; ++iter) {
        const double elapsedSec = Tools::ElapsedMs(algorithmStart, Tools::Clock::now()) / 1000.0;
        if (elapsedSec >= solverConfig.timeLimit) {
            std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                      << " reached time limit: " << solverConfig.timeLimit << "s" << std::endl;
            return ERROR;
        }

        const auto masterStart = Tools::Clock::now();
        model->optimize();
        const auto masterEnd = Tools::Clock::now();
        const long long masterMs = Tools::ElapsedMs(masterStart, masterEnd);
        int masterStatus = model->get(GRB_IntAttr_Status);
        const int masterSolCount = model->get(GRB_IntAttr_SolCount);
        if (!(masterStatus == GRB_OPTIMAL ||
              (masterStatus == GRB_TIME_LIMIT && masterSolCount > 0))) {
            std::cerr << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                      << " master problem not solved, status: " << masterStatus << std::endl;
            return ERROR;
        }
        if (masterSolCount <= 0) {
            std::cerr << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                      << " master has no incumbent, status: " << masterStatus << std::endl;
            return ERROR;
        }
        bestLowerBound = std::max(bestLowerBound, model->get(GRB_DoubleAttr_ObjBound));

        const BendersMasterSolution masterSolution = GetBendersMasterSolution(*model, zVars, theta);
        const std::vector<double>& zValues = masterSolution.values;
        const double fixedCost = masterSolution.firstStageValue;
        const double thetaValue = masterSolution.thetaValue;

        const auto& masterVars = problemData->getData<std::vector<ProblemDataVar>>("masterVars");
        const auto& scenarioSubproblems = problemData->getData<std::vector<ScenarioSubproblemInfo>>(
            "wireless_scenario_subproblems");
        const auto& facilityVarNames = problemData->getData<
            std::vector<std::vector<std::vector<std::string>>>>("wireless_route_facility_var_names");

        if (useRspCuts) {
            double relaxedQValue = 0.0;
            LShapeCutInfo relaxedAggregateCutInfo;
            std::vector<double> relaxedScenarioQValues;
            std::vector<LShapeCutInfo> relaxedScenarioCutInfos;
            const auto rspStart = Tools::Clock::now();
            Status relaxedStatus = EvaluateSubProblems(zValues, relaxedAggregateCutInfo, relaxedQValue,
                relaxedScenarioQValues, relaxedScenarioCutInfos, true);
            const auto rspEnd = Tools::Clock::now();
            if (relaxedStatus != OK) {
                std::cerr << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                          << " RSP evaluation failed in iteration " << iter << std::endl;
                return ERROR;
            }

            int rspFeasCount = 0;
            int rspOptCount = 0;
            bool rspAnyInfeasible = false;
            std::set<std::string> addedRSPFeasibilityCuts;
            for (size_t idx = 0; idx < relaxedScenarioCutInfos.size(); ++idx) {
                if (!relaxedScenarioCutInfos[idx].isOptimalityCut) {
                    rspAnyInfeasible = true;
                    const std::string cutSignature = MakeFeasibilityCutSignature(relaxedScenarioCutInfos[idx]);
                    if (addedRSPFeasibilityCuts.insert(cutSignature).second) {
                        AddFeasibilityCut(*model, relaxedScenarioCutInfos[idx], zVars,
                            "lifted_ils_rsp_feasibility_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                        ++rspFeasCount;
                    }
                    continue;
                }

                if (scenarioThetaVars[idx].get(GRB_DoubleAttr_X) + tolerance < relaxedScenarioQValues[idx]) {
                    const CutEval cutDual = BuildContinuousCut(
                        relaxedScenarioCutInfos[idx], zValues, iter, static_cast<int>(idx));
                    model->addConstr(cutDual.expr <= scenarioThetaVars[idx],
                        "lifted_ils_rsp_opt_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                    ++rspOptCount;
                }
            }

            if (rspAnyInfeasible) {
                std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                          << " iter " << iter << ", RSP feasibility cut x" << rspFeasCount
                          << ", RSP optimality cut x" << rspOptCount;
                PrintBounds(bestUpperBound, bestLowerBound);
                model->update();
                std::cout << ", master_ms=" << masterMs
                          << ", rsp_ms=" << Tools::ElapsedMs(rspStart, rspEnd)
                          << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, Tools::Clock::now()) << std::endl;
                continue;
            }
            if (rspOptCount > 0) model->update();
        }

        double aggregatedQValue = 0.0;
        LShapeCutInfo aggregateCutInfo;
        std::vector<double> scenarioQValues;
        std::vector<LShapeCutInfo> scenarioCutInfos;
        const auto integerStart = Tools::Clock::now();
        Status secondStageStatus = EvaluateSubProblems(zValues, aggregateCutInfo, aggregatedQValue,
            scenarioQValues, scenarioCutInfos, false);
        const auto integerEnd = Tools::Clock::now();
        const long long integerMs = Tools::ElapsedMs(integerStart, integerEnd);
        if (secondStageStatus != OK) {
            std::cerr << "Lifted ILS second-stage evaluation failed in iteration " << iter << std::endl;
            return ERROR;
        }

        int feasCount = 0;
        bool mipAnyInfeasible = false;
        std::set<std::string> addedFeasibilityCuts;
        for (size_t idx = 0; idx < scenarioCutInfos.size(); ++idx) {
            if (scenarioCutInfos[idx].isOptimalityCut) continue;
            mipAnyInfeasible = true;
            const std::string cutSignature = MakeFeasibilityCutSignature(scenarioCutInfos[idx]);
            if (addedFeasibilityCuts.insert(cutSignature).second) {
                AddFeasibilityCut(*model, scenarioCutInfos[idx], zVars,
                    "lifted_ils_feasibility_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                ++feasCount;
            }
            if (useMonotoneCuts) {
                const ScenarioSubproblemInfo& scenarioInfo = scenarioSubproblems[idx];
                const int scIdx = scenarioInfo.scenarioIndex;
                std::vector<std::string> scenarioFacilityNames;
                for (int rid : scenarioInfo.routeIds) {
                    const auto& routeFacilityNames = facilityVarNames.at(static_cast<size_t>(scIdx))
                        .at(static_cast<size_t>(rid));
                    scenarioFacilityNames.insert(scenarioFacilityNames.end(),
                        routeFacilityNames.begin(), routeFacilityNames.end());
                }
                const BendersCutInfo noGood = BuildScenarioStrengthenedNoGoodCut(
                    masterVars, zValues, scIdx, scenarioInfo.routeIds, scenarioFacilityNames);
                const std::string noGoodSignature = MakeFeasibilityCutSignature(noGood);
                if (addedFeasibilityCuts.insert(noGoodSignature).second) {
                    AddFeasibilityCut(*model, noGood, zVars,
                        "lifted_monotone_nogood_" + std::to_string(iter) + "_sub_" + std::to_string(idx));
                    ++feasCount;
                }
            }
        }

        if (mipAnyInfeasible) {
            std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                      << " iter " << iter << ", feasibility cut x" << feasCount;
            PrintBounds(bestUpperBound, bestLowerBound);
            model->update();
            std::cout << ", master_ms=" << masterMs << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, Tools::Clock::now()) << std::endl;
            continue;
        }

        const double candidateObjective = fixedCost + aggregatedQValue;
        if (candidateObjective + tolerance < bestUpperBound) {
            UpdateIncumbent(zValues, aggregatedQValue);
        }

        std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                  << " iter " << iter
                  << ", theta=" << thetaValue << ", Q(z)=" << aggregatedQValue;
        PrintBounds(bestUpperBound, bestLowerBound);

        const double gapDenom = std::max(1.0, std::fabs(bestUpperBound));
        const double relGap = std::isfinite(bestUpperBound)
            ? (bestUpperBound - bestLowerBound) / gapDenom
            : 1.0;
        if (std::isfinite(bestUpperBound) && relGap <= solverConfig.mipGap) {
            const auto iterEnd = Tools::Clock::now();
            std::cout << ", master_ms=" << masterMs << ", integer_ms=" << integerMs
                      << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
            PrintBestSolution();
            const auto solveEnd = Tools::Clock::now();
            std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                      << " converged." << std::endl;
            std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
                      << " final elapsed time: "
                      << Tools::ElapsedMs(solveStart, solveEnd) << " ms" << std::endl;
            return OK;
        }

        for (size_t idx = 0; idx < scenarioCutInfos.size(); ++idx) {
            if (!scenarioCutInfos[idx].isOptimalityCut) continue;
            LShapeCutInfo optCutInfo = scenarioCutInfos[idx];
            if (useMonotoneCuts) {
                const ScenarioSubproblemInfo& scenarioInfo = scenarioSubproblems[idx];
                const int scIdx = scenarioInfo.scenarioIndex;
                std::vector<std::string> scenarioFacilityNames;
                for (int rid : scenarioInfo.routeIds) {
                    const auto& routeFacilityNames = facilityVarNames.at(static_cast<size_t>(scIdx))
                        .at(static_cast<size_t>(rid));
                    scenarioFacilityNames.insert(scenarioFacilityNames.end(),
                        routeFacilityNames.begin(), routeFacilityNames.end());
                }
                optCutInfo = BuildMonotonicityStrengthenedOptimalityCut(
                    masterVars, zValues, scIdx, scenarioInfo.routeIds, scenarioFacilityNames,
                    scenarioQValues[idx], globalLowerBound);
            }
            const CutEval cutDual = BuildContinuousCut(optCutInfo, zValues, iter, static_cast<int>(idx));
            model->addConstr(cutDual.expr <= scenarioThetaVars[idx], cutDual.name);
        }

        model->update();
        const auto iterEnd = Tools::Clock::now();
        std::cout << ", master_ms=" << masterMs << ", integer_ms=" << integerMs
                  << ", elapsed_ms=" << Tools::ElapsedMs(solveStart, iterEnd) << std::endl;
    }

    PrintBestSolution();
    std::cout << (useMonotoneCuts ? "Lifted monotone ILS" : "Lifted ILS")
              << " reached max iterations: " << maxIters << std::endl;
    return ERROR;
}
