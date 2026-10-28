#include "algorithm/natural_ils.hpp"

#include "algorithm/scenario_relaxation.hpp"
#include "model/model_helpers.hpp"
#include "model/scenario_model.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace wireless::algorithm {
namespace {

constexpr double kCutTolerance = 1e-6;
constexpr int kMaximumIterations = 5000;

struct ScenarioSubproblem {
    std::unique_ptr<Envr> environment;
    std::unique_ptr<Model> model;
    std::vector<Var> facilityVariables;
};

struct ScenarioEvaluation {
    solver::Status status = solver::Status::Unknown;
    double objective = 0.0;
    double seconds = 0.0;
};

double ElapsedSeconds(const std::chrono::steady_clock::time_point& start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::vector<double> ReadBinaryValues(std::vector<Var>& variables)
{
    std::vector<double> values;
    values.reserve(variables.size());
    for (Var& variable : variables) {
        values.push_back(std::round(variable.Get(COPT_DBLINFO_VALUE)));
    }
    return values;
}

Expr BuildMismatchExpression(const std::vector<Var>& variables,
    const std::vector<double>& point)
{
    Expr mismatch(0.0);
    for (size_t index = 0; index < variables.size(); ++index) {
        if (point.at(index) > 0.5) mismatch += 1.0 - variables.at(index);
        else mismatch += variables.at(index);
    }
    return mismatch;
}

Expr BuildAffineExpression(const std::vector<Var>& variables,
    const ScenarioRelaxationResult& result)
{
    Expr expression(result.constant);
    for (size_t index = 0; index < result.coefficients.size(); ++index) {
        if (std::fabs(result.coefficients.at(index)) > 1e-12) {
            expression += result.coefficients.at(index) * variables.at(index);
        }
    }
    return expression;
}

std::vector<ScenarioRelaxationResult> EvaluateRelaxations(
    std::vector<ScenarioRelaxation>& relaxations,
    const std::vector<double>& facilityValues, const SolverConfig& solverConfig,
    const std::chrono::steady_clock::time_point& start, int workerLimit)
{
    std::vector<ScenarioRelaxationResult> results(relaxations.size());
    std::atomic<size_t> next{0};
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    const size_t workerCount = std::min(relaxations.size(),
        static_cast<size_t>(std::max(1, workerLimit)));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&]() {
            try {
                while (true) {
                    const size_t scenario = next.fetch_add(1);
                    if (scenario >= relaxations.size()) break;
                    const double remaining = solverConfig.timeLimit - ElapsedSeconds(start);
                    if (remaining <= 0.0) {
                        results.at(scenario).status = solver::Status::TimeLimit;
                    } else {
                        results.at(scenario) = relaxations.at(scenario).Evaluate(
                            facilityValues, remaining);
                    }
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(exceptionMutex);
                if (!firstException) firstException = std::current_exception();
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    if (firstException) std::rethrow_exception(firstException);
    return results;
}

ScenarioEvaluation EvaluateScenario(ScenarioSubproblem& subproblem,
    const std::vector<double>& facilityValues, double remainingSeconds)
{
    for (size_t index = 0; index < facilityValues.size(); ++index) {
        Var& variable = subproblem.facilityVariables.at(index);
        variable.Set(COPT_DBLINFO_LB, facilityValues.at(index));
        variable.Set(COPT_DBLINFO_UB, facilityValues.at(index));
    }
    subproblem.model->SetDblParam(COPT_DBLPARAM_TIMELIMIT,
        std::max(0.001, remainingSeconds));
    subproblem.model->Update();
    const solver::Result result = solver::Solve(*subproblem.model);
    return {result.status, result.objective, result.seconds};
}

void PrintIteration(int iteration, int feasibilityCuts, int optimalityCuts,
    double upperBound, double lowerBound, double masterSeconds,
    double rspSeconds, int rspOptimalityCuts, bool rspEnabled,
    double subproblemSeconds, double elapsedSeconds)
{
    std::cout << "Natural ILS iter " << iteration
              << ", feasibility cut x" << feasibilityCuts
              << ", optimality cut x" << optimalityCuts
              << ", UB=";
    if (std::isfinite(upperBound)) std::cout << upperBound;
    else std::cout << "inf";
    std::cout << ", LB=" << lowerBound << ", gap=";
    if (std::isfinite(upperBound)) {
        std::cout << (upperBound - lowerBound) /
            std::max(1.0, std::fabs(upperBound)) * 100.0 << '%';
    } else {
        std::cout << '1';
    }
    std::cout << ", master_ms=" << masterSeconds * 1000.0;
    if (rspEnabled) {
        std::cout << ", rsp_optimality_cut x" << rspOptimalityCuts
                  << ", rsp_ms=" << rspSeconds * 1000.0;
    }
    std::cout << ", subproblem_ms=" << subproblemSeconds * 1000.0
              << ", elapsed_ms=" << elapsedSeconds * 1000.0 << '\n';
}

} // namespace

solver::Result SolveNaturalIls(const InstanceData& instance,
    const RunConfig& runConfig, const SolverConfig& solverConfig)
{
    const auto start = std::chrono::steady_clock::now();
    const std::vector<model::DwcLink> candidates = runConfig.allowWirelessCharging
        ? model::SelectDwcCandidates(instance, runConfig.dwcTopN)
        : std::vector<model::DwcLink>();

    Envr masterEnvironment = solver::CreateEnvironment();
    Model master = masterEnvironment.CreateModel("natural_ils_master");
    std::vector<Var> masterFacilities;
    std::vector<double> facilityCosts;
    Expr masterObjective(0.0);
    for (model::DwcLink candidate : candidates) {
        const double cost = model::DwcFacilityCost(instance, candidate);
        const std::string name = model::DwcVariableName(candidate);
        const Var variable = master.AddVar(0.0, 1.0, 0.0, COPT_BINARY, name.c_str());
        masterFacilities.push_back(variable);
        facilityCosts.push_back(cost);
        masterObjective += cost * variable;
    }

    const Var theta = master.AddVar(0.0, COPT_INFINITY, 0.0, COPT_CONTINUOUS, "theta");
    masterObjective += theta;
    std::vector<Var> scenarioTheta;
    Expr thetaSum(0.0);
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        const std::string name = "natural_theta_sc" + std::to_string(scenario);
        const Var variable = master.AddVar(0.0, COPT_INFINITY, 0.0, COPT_CONTINUOUS,
            name.c_str());
        scenarioTheta.push_back(variable);
        thetaSum += variable;
    }
    master.AddConstr(thetaSum <= theta, "natural_theta_sum");
    master.SetObjective(masterObjective, COPT_MINIMIZE);
    master.Update();
    solver::Configure(master, solverConfig, solverConfig.phase2MasterLog);

    std::vector<ScenarioSubproblem> subproblems;
    subproblems.reserve(instance.scenarios.size());
    for (size_t scenarioIndex = 0; scenarioIndex < instance.scenarios.size(); ++scenarioIndex) {
        ScenarioSubproblem state;
        state.environment = std::make_unique<Envr>(solver::CreateEnvironment());
        state.model = std::make_unique<Model>(state.environment->CreateModel(
            ("natural_scenario_" + std::to_string(scenarioIndex)).c_str()));
        model::FacilityVariables facilities;
        for (model::DwcLink candidate : candidates) {
            const std::string name = model::DwcVariableName(candidate);
            const Var variable = state.model->AddVar(
                0.0, 1.0, 0.0, COPT_CONTINUOUS, name.c_str());
            facilities.emplace(name, variable);
            state.facilityVariables.push_back(variable);
        }
        Expr objective(0.0);
        model::BuildScenarioModel(*state.model, instance,
            instance.scenarios.at(scenarioIndex), scenarioIndex, facilities, objective);
        state.model->SetObjective(objective, COPT_MINIMIZE);
        state.model->Update();
        solver::Configure(*state.model, solverConfig, false);
        state.model->SetIntParam(COPT_INTPARAM_LPMETHOD, 1);
        subproblems.push_back(std::move(state));
    }

    const int relaxationWorkerLimit = runConfig.rspWorkers > 0
        ? runConfig.rspWorkers : std::min(3, solverConfig.threads);
    const int relaxationWorkers = std::min(
        static_cast<int>(instance.scenarios.size()), relaxationWorkerLimit);
    const int relaxationThreads = runConfig.rspThreads > 0
        ? runConfig.rspThreads
        : std::max(1, solverConfig.threads / relaxationWorkers);
    if (relaxationWorkers * relaxationThreads > solverConfig.threads) {
        throw std::invalid_argument(
            "RSP workers multiplied by RSP threads exceeds solver thread budget");
    }
    std::vector<ScenarioRelaxation> relaxedSubproblems;
    if (runConfig.useRsp) {
        relaxedSubproblems.reserve(instance.scenarios.size());
        for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
            relaxedSubproblems.emplace_back(instance, scenario, candidates,
                std::vector<std::vector<size_t>>{}, std::vector<int>{},
                masterFacilities.size(), solverConfig, relaxationThreads, true);
        }
    }

    std::cout << "Natural ILS initialized: master_vars=" << masterFacilities.size()
              << ", scenarios=" << subproblems.size()
              << ", battery_decisions=subproblem, RSP="
              << (runConfig.useRsp ? "on" : "off");
    if (runConfig.useRsp) {
        std::cout << ", rsp_workers=" << relaxationWorkers
                  << ", rsp_threads=" << relaxationThreads;
    }
    std::cout << '\n';

    double upperBound = std::numeric_limits<double>::infinity();
    double lowerBound = -std::numeric_limits<double>::infinity();
    std::vector<double> incumbent;
    double incumbentRecourse = std::numeric_limits<double>::infinity();
    solver::Result finalResult;

    for (int iteration = 0; iteration < kMaximumIterations; ++iteration) {
        const double remaining = solverConfig.timeLimit - ElapsedSeconds(start);
        if (remaining <= 0.0) {
            finalResult.status = solver::Status::TimeLimit;
            break;
        }
        master.SetDblParam(COPT_DBLPARAM_TIMELIMIT, remaining);
        const solver::Result masterResult = solver::Solve(master);
        if (!masterResult.hasSolution) {
            finalResult.status = masterResult.status;
            break;
        }
        lowerBound = std::max(lowerBound, masterResult.bound);
        const std::vector<double> facilityValues = ReadBinaryValues(masterFacilities);
        std::vector<double> thetaValues;
        thetaValues.reserve(scenarioTheta.size());
        for (Var& variable : scenarioTheta) {
            thetaValues.push_back(variable.Get(COPT_DBLINFO_VALUE));
        }

        double rspSeconds = 0.0;
        int rspFeasibilityCuts = 0;
        int rspOptimalityCuts = 0;
        double rspRecourse = 0.0;
        if (runConfig.useRsp) {
            const auto rspStart = std::chrono::steady_clock::now();
            const std::vector<ScenarioRelaxationResult> rspResults =
                EvaluateRelaxations(relaxedSubproblems, facilityValues, solverConfig,
                    start, relaxationWorkers);
            rspSeconds = ElapsedSeconds(rspStart);
            bool rspInfeasible = false;
            for (size_t scenario = 0; scenario < rspResults.size(); ++scenario) {
                const ScenarioRelaxationResult& result = rspResults.at(scenario);
                if (result.status == solver::Status::Infeasible) {
                    if (!result.coefficients.empty()) {
                        master.AddConstr(BuildAffineExpression(masterFacilities, result) >= 0.0,
                            ("natural_rsp_feas_" + std::to_string(iteration) + "_" +
                                std::to_string(scenario)).c_str());
                    } else {
                        master.AddConstr(
                            BuildMismatchExpression(masterFacilities, facilityValues) >= 1.0,
                            ("natural_rsp_fallback_" + std::to_string(iteration) + "_" +
                                std::to_string(scenario)).c_str());
                    }
                    ++rspFeasibilityCuts;
                    rspInfeasible = true;
                } else if (result.status == solver::Status::Optimal) {
                    rspRecourse += result.objective;
                    if (thetaValues.at(scenario) + kCutTolerance < result.objective) {
                        master.AddConstr(scenarioTheta.at(scenario) >=
                            BuildAffineExpression(masterFacilities, result),
                            ("natural_rsp_opt_" + std::to_string(iteration) + "_" +
                                std::to_string(scenario)).c_str());
                        ++rspOptimalityCuts;
                    }
                } else {
                    finalResult.status = result.status;
                    finalResult.hasSolution = std::isfinite(upperBound);
                    finalResult.objective = upperBound;
                    finalResult.bound = lowerBound;
                    finalResult.seconds = ElapsedSeconds(start);
                    return finalResult;
                }
            }
            master.Update();
            if (rspInfeasible) {
                std::cout << "Natural ILS iter " << iteration
                          << ", RSP feasibility cut x" << rspFeasibilityCuts
                          << ", RSP optimality cut x" << rspOptimalityCuts
                          << ", RSP Q(z)=" << rspRecourse
                          << ", UB=";
                if (std::isfinite(upperBound)) std::cout << upperBound;
                else std::cout << "inf";
                std::cout << ", LB=" << lowerBound
                          << ", master_ms=" << masterResult.seconds * 1000.0
                          << ", rsp_ms=" << rspSeconds * 1000.0
                          << ", elapsed_ms=" << ElapsedSeconds(start) * 1000.0 << '\n';
                continue;
            }
        }

        bool infeasible = false;
        int feasibilityCuts = 0;
        int optimalityCuts = 0;
        double recourse = 0.0;
        double subproblemSeconds = 0.0;
        for (size_t scenario = 0; scenario < subproblems.size(); ++scenario) {
            const double subRemaining = solverConfig.timeLimit - ElapsedSeconds(start);
            if (subRemaining <= 0.0) {
                finalResult.status = solver::Status::TimeLimit;
                return finalResult;
            }
            const ScenarioEvaluation evaluation = EvaluateScenario(
                subproblems.at(scenario), facilityValues, subRemaining);
            subproblemSeconds += evaluation.seconds;
            if (evaluation.status == solver::Status::Infeasible) {
                master.AddConstr(BuildMismatchExpression(masterFacilities, facilityValues) >= 1.0,
                    ("natural_feas_" + std::to_string(iteration) + "_" +
                        std::to_string(scenario)).c_str());
                ++feasibilityCuts;
                infeasible = true;
            } else if (evaluation.status == solver::Status::Optimal) {
                recourse += evaluation.objective;
                if (thetaValues.at(scenario) + kCutTolerance < evaluation.objective) {
                    const Expr mismatch = BuildMismatchExpression(masterFacilities, facilityValues);
                    master.AddConstr(scenarioTheta.at(scenario) >=
                        evaluation.objective * (1.0 - mismatch),
                        ("natural_opt_" + std::to_string(iteration) + "_" +
                            std::to_string(scenario)).c_str());
                    ++optimalityCuts;
                }
            } else {
                finalResult.status = evaluation.status;
                return finalResult;
            }
        }
        master.Update();

        double firstStage = 0.0;
        for (size_t index = 0; index < facilityValues.size(); ++index) {
            firstStage += facilityCosts.at(index) * facilityValues.at(index);
        }
        if (!infeasible && firstStage + recourse < upperBound - kCutTolerance) {
            upperBound = firstStage + recourse;
            incumbent = facilityValues;
            incumbentRecourse = recourse;
        }
        PrintIteration(iteration, feasibilityCuts, optimalityCuts, upperBound,
            lowerBound, masterResult.seconds, rspSeconds, rspOptimalityCuts,
            runConfig.useRsp, subproblemSeconds, ElapsedSeconds(start));

        if (std::isfinite(upperBound) &&
            (upperBound - lowerBound) / std::max(1.0, std::fabs(upperBound))
                <= solverConfig.mipGap) {
            finalResult.status = solver::Status::Optimal;
            finalResult.hasSolution = true;
            finalResult.objective = upperBound;
            finalResult.bound = lowerBound;
            finalResult.relativeGap = (upperBound - lowerBound) /
                std::max(1.0, std::fabs(upperBound));
            finalResult.seconds = ElapsedSeconds(start);
            std::cout << "Natural ILS best solution: UB=" << upperBound
                      << ", LB=" << lowerBound << ", Q(z)=" << incumbentRecourse << '\n';
            for (size_t index = 0; index < incumbent.size(); ++index) {
                if (incumbent.at(index) > 0.5) {
                    std::cout << "  " << masterFacilities.at(index).GetName() << '\n';
                }
            }
            std::cout << "Natural ILS converged.\n";
            return finalResult;
        }
    }

    finalResult.hasSolution = std::isfinite(upperBound);
    finalResult.objective = upperBound;
    finalResult.bound = lowerBound;
    finalResult.seconds = ElapsedSeconds(start);
    if (finalResult.hasSolution) {
        finalResult.relativeGap = (upperBound - lowerBound) /
            std::max(1.0, std::fabs(upperBound));
    }
    return finalResult;
}

} // namespace wireless::algorithm
