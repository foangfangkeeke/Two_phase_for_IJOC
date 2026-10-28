#include "algorithm/feasibility_diagnostics.hpp"

#include "model/scenario_model.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace wireless::algorithm {
namespace {

struct ScenarioDiagnostic {
    std::unique_ptr<Envr> environment;
    std::unique_ptr<Model> model;
};

double Elapsed(const std::chrono::steady_clock::time_point& start)
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
}

ScenarioDiagnostic BuildDiagnostic(const InstanceData& instance, size_t scenario,
    const SolverConfig& solverConfig)
{
    ScenarioDiagnostic result;
    result.environment = std::make_unique<Envr>(solver::CreateEnvironment());
    result.model = std::make_unique<Model>(result.environment->CreateModel(
        ("no_dwc_feasibility_scenario_" + std::to_string(scenario)).c_str()));

    model::FacilityVariables facilities;
    Expr ignoredObjective(0.0);
    model::ScenarioModelOptions options{false, true};
    options.maximumBatteryCapacities.assign(
        instance.scenarios.at(scenario).schedule.routeNames.size(),
        static_cast<int>(instance.operation.maximumBatteryCapacity));
    model::ScenarioVariables variables = model::BuildScenarioModel(*result.model,
        instance, instance.scenarios.at(scenario), scenario, facilities,
        ignoredObjective, options);

    for (std::vector<Var>& route : variables.batteryChoicesByRoute) {
        for (size_t level = 0; level < route.size(); ++level) {
            const double fixedValue = level + 1 == route.size() ? 1.0 : 0.0;
            route.at(level).Set(COPT_DBLINFO_LB, fixedValue);
            route.at(level).Set(COPT_DBLINFO_UB, fixedValue);
        }
    }
    result.model->SetObjective(Expr(0.0), COPT_MINIMIZE);
    result.model->Update();
    solver::Configure(*result.model, solverConfig, false);
    return result;
}

solver::Status AggregateUnknownStatus(const std::vector<solver::Result>& results)
{
    for (const solver::Result& result : results) {
        if (result.status == solver::Status::Numerical) {
            return solver::Status::Numerical;
        }
    }
    for (const solver::Result& result : results) {
        if (result.status == solver::Status::TimeLimit) {
            return solver::Status::TimeLimit;
        }
    }
    for (const solver::Result& result : results) {
        if (result.status == solver::Status::Interrupted) {
            return solver::Status::Interrupted;
        }
    }
    return solver::Status::Unknown;
}

} // namespace

solver::Result DiagnoseNoDwcFeasibility(const InstanceData& instance,
    const RunConfig& runConfig, const SolverConfig& solverConfig)
{
    const auto start = std::chrono::steady_clock::now();
    const int workerLimit = runConfig.rspWorkers > 0
        ? runConfig.rspWorkers : std::min(3, solverConfig.threads);
    const int workers = std::min(
        static_cast<int>(instance.scenarios.size()), workerLimit);
    const int threads = runConfig.rspThreads > 0
        ? runConfig.rspThreads : std::max(1, solverConfig.threads / workers);
    if (workers * threads > solverConfig.threads) {
        throw std::invalid_argument(
            "Scenario workers multiplied by scenario threads exceeds solver thread budget");
    }

    SolverConfig scenarioSolverConfig = solverConfig;
    scenarioSolverConfig.threads = threads;
    std::vector<ScenarioDiagnostic> diagnostics;
    diagnostics.reserve(instance.scenarios.size());
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        diagnostics.push_back(BuildDiagnostic(
            instance, scenario, scenarioSolverConfig));
    }

    std::cout << "No-DWC zero-objective feasibility initialized: scenarios="
              << diagnostics.size()
              << ", maximum_battery_capacity="
              << instance.operation.maximumBatteryCapacity
              << ", scenario_workers=" << workers
              << ", scenario_threads=" << threads << '\n';

    std::vector<solver::Result> results(diagnostics.size());
    std::atomic<size_t> next{0};
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    std::vector<std::thread> workerThreads;
    workerThreads.reserve(static_cast<size_t>(workers));
    for (int worker = 0; worker < workers; ++worker) {
        workerThreads.emplace_back([&]() {
            while (true) {
                const size_t scenario = next.fetch_add(1);
                if (scenario >= diagnostics.size()) return;
                try {
                    const double remaining = solverConfig.timeLimit - Elapsed(start);
                    if (remaining <= 0.0) {
                        results.at(scenario).status = solver::Status::TimeLimit;
                        continue;
                    }
                    diagnostics.at(scenario).model->SetDblParam(
                        COPT_DBLPARAM_TIMELIMIT, remaining);
                    results.at(scenario) = solver::Solve(
                        *diagnostics.at(scenario).model);
                } catch (...) {
                    std::lock_guard<std::mutex> lock(exceptionMutex);
                    if (!firstException) firstException = std::current_exception();
                    return;
                }
            }
        });
    }
    for (std::thread& worker : workerThreads) worker.join();
    if (firstException) std::rethrow_exception(firstException);

    bool allFeasible = true;
    bool anyInfeasible = false;
    for (size_t scenario = 0; scenario < results.size(); ++scenario) {
        const solver::Result& result = results.at(scenario);
        allFeasible = allFeasible && result.hasSolution;
        anyInfeasible = anyInfeasible ||
            result.status == solver::Status::Infeasible;
        std::cout << "No-DWC scenario=" << scenario
                  << ", folder=" << instance.scenarios.at(scenario).descriptor.folder
                  << ", status=" << solver::ToString(result.status)
                  << ", has_solution=" << (result.hasSolution ? 1 : 0)
                  << ", solve_ms=" << result.seconds * 1000.0 << '\n';
    }

    solver::Result aggregate;
    aggregate.hasSolution = allFeasible;
    aggregate.seconds = Elapsed(start);
    if (allFeasible) aggregate.status = solver::Status::Optimal;
    else if (anyInfeasible) aggregate.status = solver::Status::Infeasible;
    else aggregate.status = AggregateUnknownStatus(results);
    std::cout << "No-DWC feasibility result="
              << (allFeasible ? "FEASIBLE" : anyInfeasible ? "INFEASIBLE" : "UNKNOWN")
              << ", status=" << solver::ToString(aggregate.status)
              << ", elapsed_ms=" << aggregate.seconds * 1000.0 << '\n';
    return aggregate;
}

} // namespace wireless::algorithm
