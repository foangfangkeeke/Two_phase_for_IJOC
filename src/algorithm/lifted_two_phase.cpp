#include "algorithm/lifted_two_phase.hpp"

#include "algorithm/route_relaxation.hpp"
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
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace wireless::algorithm {
namespace {

constexpr int kBatteryStep = 20;
constexpr int kMaximumIterations = 5000;
constexpr double kTolerance = 1e-6;

struct BatteryLevel {
    int capacity = 0;
    size_t index = 0;
    Var variable;
};

struct MasterState {
    std::vector<Var> design;
    std::vector<double> costs;
    std::vector<Var> facilities;
    std::vector<std::vector<std::vector<BatteryLevel>>> batteries;
    std::vector<std::vector<Var>> routeTheta;
    std::vector<Var> scenarioTheta;
    Var theta;
    Constraint routeThetaLink;

    MasterState(Var theta, Constraint routeThetaLink)
        : theta(std::move(theta)), routeThetaLink(std::move(routeThetaLink)) {}
};

struct ExactScenario {
    std::unique_ptr<Envr> environment;
    std::unique_ptr<Model> model;
    std::vector<Var> facilities;
    model::ScenarioVariables variables;
    std::vector<size_t> relevant;
};

double Elapsed(const std::chrono::steady_clock::time_point& start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::vector<RouteRelaxationResult> EvaluateRoutes(
    std::vector<RouteRelaxation>& routes, const std::vector<double>& values,
    const SolverConfig& config, const std::chrono::steady_clock::time_point& start)
{
    std::vector<RouteRelaxationResult> results(routes.size());
    std::atomic<size_t> next{0};
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    const size_t workerCount = std::min(routes.size(),
        static_cast<size_t>(std::max(1, config.threads)));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&]() {
            while (true) {
                const size_t index = next.fetch_add(1);
                if (index >= routes.size()) return;
                try {
                    const double remaining = config.timeLimit - Elapsed(start);
                    if (remaining <= 0.0) {
                        results.at(index).status = solver::Status::TimeLimit;
                    } else {
                        results.at(index) = routes.at(index).Evaluate(values, remaining);
                    }
                } catch (...) {
                    std::lock_guard<std::mutex> lock(exceptionMutex);
                    if (!firstException) firstException = std::current_exception();
                    return;
                }
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    if (firstException) std::rethrow_exception(firstException);
    return results;
}

std::vector<double> Values(std::vector<Var>& variables)
{
    std::vector<double> result;
    result.reserve(variables.size());
    for (Var& variable : variables) {
        result.push_back(std::round(variable.Get(COPT_DBLINFO_VALUE)));
    }
    return result;
}

double DesignCost(const MasterState& state, const std::vector<double>& values)
{
    double result = 0.0;
    for (size_t index = 0; index < values.size(); ++index) {
        result += state.costs.at(index) * values.at(index);
    }
    return result;
}

void PrintBounds(double upperBound, double lowerBound)
{
    std::cout << ", UB=";
    if (!std::isfinite(upperBound)) {
        std::cout << "inf, LB=" << lowerBound << ", gap=1";
        return;
    }
    std::cout << upperBound << ", LB=" << lowerBound << ", gap="
              << (upperBound - lowerBound) /
                    std::max(1.0, std::fabs(upperBound)) * 100.0 << '%';
}

MasterState BuildMaster(Model& model, const InstanceData& instance,
    const std::vector<model::DwcLink>& candidates)
{
    Expr objective(0.0);
    std::vector<Var> design;
    std::vector<double> costs;
    std::vector<Var> facilities;
    for (model::DwcLink candidate : candidates) {
        const double cost = model::DwcFacilityCost(instance, candidate);
        const Var variable = model.AddVar(0.0, 1.0, 0.0, COPT_BINARY,
            model::DwcVariableName(candidate).c_str());
        design.push_back(variable);
        costs.push_back(cost);
        facilities.push_back(variable);
        objective += cost * variable;
    }

    std::vector<std::vector<std::vector<BatteryLevel>>> batteries(
        instance.scenarios.size());
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        const ScheduleData& schedule = instance.scenarios.at(scenario).schedule;
        const std::vector<int> busesByRoute = model::CountBusesByRoute(schedule);
        batteries.at(scenario).resize(schedule.routeNames.size());
        for (size_t route = 0; route < schedule.routeNames.size(); ++route) {
            Expr selectOne(0.0);
            const double unitCost = instance.scenarios.at(scenario).descriptor.probability *
                busesByRoute.at(route) * instance.operation.batteryUnitPrice;
            for (int capacity = static_cast<int>(instance.operation.minimumBatteryCapacity);
                 capacity <= static_cast<int>(instance.operation.maximumBatteryCapacity);
                 capacity += kBatteryStep) {
                const Var variable = model.AddVar(0.0, 1.0, 0.0, COPT_BINARY,
                    ("sc" + std::to_string(scenario) + "_E_r" + std::to_string(route) +
                        "_cap" + std::to_string(capacity)).c_str());
                const size_t index = design.size();
                const double cost = unitCost * capacity;
                design.push_back(variable);
                costs.push_back(cost);
                batteries.at(scenario).at(route).push_back({capacity, index, variable});
                objective += cost * variable;
                selectOne += variable;
            }
            model.AddConstr(selectOne == 1.0,
                ("selectCap_sc" + std::to_string(scenario) + "_r" +
                    std::to_string(route)).c_str());
        }
    }

    const Var theta = model.AddVar(0.0, COPT_INFINITY, 0.0, COPT_CONTINUOUS, "theta");
    objective += theta;
    std::vector<std::vector<Var>> routeTheta(instance.scenarios.size());
    Expr routeSum(0.0);
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        for (size_t route = 0; route < batteries.at(scenario).size(); ++route) {
            const Var variable = model.AddVar(0.0, COPT_INFINITY, 0.0, COPT_CONTINUOUS,
                ("route_theta_sc" + std::to_string(scenario) + "_r" +
                    std::to_string(route)).c_str());
            routeTheta.at(scenario).push_back(variable);
            routeSum += variable;
        }
    }
    const Constraint routeLink = model.AddConstr(routeSum <= theta, "theta_route_sum");
    model.SetObjective(objective, COPT_MINIMIZE);
    model.Update();

    MasterState state(theta, routeLink);
    state.design = std::move(design);
    state.costs = std::move(costs);
    state.facilities = std::move(facilities);
    state.batteries = std::move(batteries);
    state.routeTheta = std::move(routeTheta);
    return state;
}

Expr RouteNoGood(const MasterState& master, const RouteRelaxation& route,
    const std::vector<double>& values)
{
    Expr expression(0.0);
    for (size_t facility : route.RelevantFacilities()) {
        if (values.at(facility) < 0.5) expression += master.design.at(facility);
    }
    int selected = -1;
    for (const BatteryLevelRef& level : route.BatteryLevels()) {
        if (values.at(level.masterIndex) > 0.5) selected = level.capacity;
    }
    for (const BatteryLevelRef& level : route.BatteryLevels()) {
        if (level.capacity > selected) expression += master.design.at(level.masterIndex);
    }
    return expression;
}

solver::Result RunPhaseOne(Model& model, MasterState& master,
    const InstanceData& instance, const std::vector<model::DwcLink>& candidates,
    const SolverConfig& config, const std::chrono::steady_clock::time_point& start)
{
    std::vector<RouteRelaxation> routes;
    for (size_t scenario = 0; scenario < master.batteries.size(); ++scenario) {
        for (size_t route = 0; route < master.batteries.at(scenario).size(); ++route) {
            std::vector<BatteryLevelRef> levels;
            for (const BatteryLevel& level : master.batteries.at(scenario).at(route)) {
                levels.push_back({level.capacity, level.index});
            }
            routes.emplace_back(instance, scenario, route, candidates, levels,
                master.design.size(), config);
        }
    }
    std::cout << "Phase 1 initialized: route_subproblems=" << routes.size() << '\n';

    double upperBound = std::numeric_limits<double>::infinity();
    double lowerBound = -std::numeric_limits<double>::infinity();
    for (int iteration = 0; iteration < kMaximumIterations; ++iteration) {
        double remaining = config.timeLimit - Elapsed(start);
        if (remaining <= 0.0) return {solver::Status::TimeLimit, false, upperBound,
            lowerBound, 0.0, Elapsed(start)};
        model.SetDblParam(COPT_DBLPARAM_TIMELIMIT, remaining);
        const solver::Result masterResult = solver::Solve(model);
        if (!masterResult.hasSolution) return masterResult;
        lowerBound = std::max(lowerBound, masterResult.bound);
        const std::vector<double> values = Values(master.design);
        std::vector<std::vector<double>> thetaValues(master.routeTheta.size());
        for (size_t scenario = 0; scenario < master.routeTheta.size(); ++scenario) {
            for (Var& theta : master.routeTheta.at(scenario)) {
                thetaValues.at(scenario).push_back(theta.Get(COPT_DBLINFO_VALUE));
            }
        }
        const double thetaValue = master.theta.Get(COPT_DBLINFO_VALUE);

        bool infeasible = false;
        int feasibilityCuts = 0;
        int optimalityCuts = 0;
        double recourse = 0.0;
        const auto subproblemStart = std::chrono::steady_clock::now();
        const std::vector<RouteRelaxationResult> routeResults =
            EvaluateRoutes(routes, values, config, start);
        const double subproblemSeconds = Elapsed(subproblemStart);
        for (size_t index = 0; index < routes.size(); ++index) {
            const RouteRelaxationResult& result = routeResults.at(index);
            if (result.status == solver::Status::Infeasible) {
                if (result.coefficients.empty()) {
                    model.AddConstr(RouteNoGood(master, routes.at(index), values) >= 1.0,
                        ("p1_feas_" + std::to_string(iteration) + "_" +
                            std::to_string(index)).c_str());
                } else {
                    Expr cut(result.constant);
                    for (size_t variable = 0; variable < result.coefficients.size(); ++variable) {
                        const double coefficient = result.coefficients.at(variable);
                        if (std::fabs(coefficient) > 1e-12) {
                            cut += coefficient * master.design.at(variable);
                        }
                    }
                    model.AddConstr(cut >= 0.0,
                        ("p1_farkas_" + std::to_string(iteration) + "_" +
                            std::to_string(index)).c_str());
                }
                ++feasibilityCuts;
                infeasible = true;
            } else if (result.status == solver::Status::Optimal) {
                recourse += result.objective;
                const size_t scenario = routes.at(index).ScenarioIndex();
                const size_t route = routes.at(index).RouteIndex();
                if (thetaValues.at(scenario).at(route) + kTolerance < result.objective) {
                    Expr cut(result.constant);
                    for (size_t variable = 0; variable < result.coefficients.size(); ++variable) {
                        const double coefficient = result.coefficients.at(variable);
                        if (std::fabs(coefficient) > 1e-12) {
                            cut += coefficient * master.design.at(variable);
                        }
                    }
                    model.AddConstr(master.routeTheta.at(scenario).at(route) >= cut,
                        ("p1_opt_" + std::to_string(iteration) + "_" +
                            std::to_string(index)).c_str());
                    ++optimalityCuts;
                }
            } else {
                solver::Result failed;
                failed.status = result.status;
                return failed;
            }
        }
        model.Update();
        if (!infeasible) upperBound = std::min(upperBound, DesignCost(master, values) + recourse);
        std::cout << "Phase 1 iter " << iteration;
        if (infeasible) std::cout << ", feasibility cut x" << feasibilityCuts;
        else std::cout << ", theta=" << thetaValue << ", Q(z)=" << recourse
                       << ", optimality cut x" << optimalityCuts;
        PrintBounds(upperBound, lowerBound);
        std::cout << ", master_ms=" << masterResult.seconds * 1000.0
                  << ", subproblem_ms=" << subproblemSeconds * 1000.0
                  << ", elapsed_ms=" << Elapsed(start) * 1000.0 << '\n';

        if (std::isfinite(upperBound) && (upperBound - lowerBound) /
                std::max(1.0, std::fabs(upperBound)) <= config.mipGap) {
            solver::Result result;
            result.status = solver::Status::Optimal;
            result.hasSolution = true;
            result.objective = upperBound;
            result.bound = lowerBound;
            result.seconds = Elapsed(start);
            return result;
        }
    }
    solver::Result result;
    result.status = solver::Status::Unknown;
    return result;
}

std::set<model::DwcLink> UsedLinks(const ScenarioData& scenario)
{
    std::set<model::DwcLink> links;
    for (const auto& stations : scenario.schedule.stationsByTrip) {
        for (size_t index = 0; index + 1 < stations.size(); ++index) {
            links.emplace(stations.at(index), stations.at(index + 1));
        }
    }
    return links;
}

ExactScenario BuildExact(const InstanceData& instance, size_t scenario,
    const std::vector<model::DwcLink>& candidates, const MasterState& master,
    const SolverConfig& config)
{
    ExactScenario result;
    result.environment = std::make_unique<Envr>();
    result.model = std::make_unique<Model>(result.environment->CreateModel(
        ("phase2_scenario_" + std::to_string(scenario)).c_str()));
    model::FacilityVariables facilities;
    for (model::DwcLink candidate : candidates) {
        const std::string name = model::DwcVariableName(candidate);
        const Var variable = result.model->AddVar(0.0, 1.0, 0.0, COPT_CONTINUOUS, name.c_str());
        facilities.emplace(name, variable);
        result.facilities.push_back(variable);
    }
    Expr objective(0.0);
    result.variables = model::BuildScenarioModel(*result.model, instance,
        instance.scenarios.at(scenario), scenario, facilities, objective, {false, true});
    for (auto& route : result.variables.batteryChoicesByRoute) {
        for (Var& choice : route) choice.SetType(COPT_CONTINUOUS);
    }
    result.model->SetObjective(objective, COPT_MINIMIZE);
    result.model->Update();
    solver::Configure(*result.model, config, false);
    result.model->SetIntParam(COPT_INTPARAM_LPMETHOD, 1);

    const std::set<model::DwcLink> used = UsedLinks(instance.scenarios.at(scenario));
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (used.count(candidates.at(index)) != 0) result.relevant.push_back(index);
    }
    for (const auto& route : master.batteries.at(scenario)) {
        for (const BatteryLevel& level : route) result.relevant.push_back(level.index);
    }
    return result;
}

solver::Result EvaluateExact(ExactScenario& subproblem, const MasterState& master,
    size_t scenario, const std::vector<double>& values, double remaining)
{
    for (size_t facility = 0; facility < subproblem.facilities.size(); ++facility) {
        subproblem.facilities.at(facility).Set(COPT_DBLINFO_LB, values.at(facility));
        subproblem.facilities.at(facility).Set(COPT_DBLINFO_UB, values.at(facility));
    }
    for (size_t route = 0; route < subproblem.variables.batteryChoicesByRoute.size(); ++route) {
        for (size_t level = 0;
             level < subproblem.variables.batteryChoicesByRoute.at(route).size(); ++level) {
            const size_t index = master.batteries.at(scenario).at(route).at(level).index;
            subproblem.variables.batteryChoicesByRoute.at(route).at(level).Set(
                COPT_DBLINFO_LB, values.at(index));
            subproblem.variables.batteryChoicesByRoute.at(route).at(level).Set(
                COPT_DBLINFO_UB, values.at(index));
        }
    }
    subproblem.model->SetDblParam(COPT_DBLPARAM_TIMELIMIT, std::max(0.001, remaining));
    subproblem.model->Update();
    return solver::Solve(*subproblem.model);
}

Expr Mismatch(const MasterState& master, const std::vector<double>& values,
    const std::vector<size_t>& relevant)
{
    Expr result(0.0);
    for (size_t index : relevant) {
        result += values.at(index) > 0.5
            ? 1.0 - master.design.at(index) : master.design.at(index);
    }
    return result;
}

Expr MonotoneImprovements(const MasterState& master, size_t scenario,
    const std::vector<double>& values, const std::vector<size_t>& relevant)
{
    const std::set<size_t> relevantSet(relevant.begin(), relevant.end());
    Expr result(0.0);
    for (size_t facility = 0; facility < master.facilities.size(); ++facility) {
        if (relevantSet.count(facility) != 0 && values.at(facility) < 0.5) {
            result += master.design.at(facility);
        }
    }
    for (const auto& route : master.batteries.at(scenario)) {
        int selectedCapacity = -1;
        for (const BatteryLevel& level : route) {
            if (values.at(level.index) > 0.5) selectedCapacity = level.capacity;
        }
        for (const BatteryLevel& level : route) {
            if (level.capacity > selectedCapacity) result += level.variable;
        }
    }
    return result;
}

} // namespace

solver::Result SolveFramework(const InstanceData& instance,
    const RunConfig& runConfig, const SolverConfig& solverConfig, bool monotoneCuts)
{
    if (runConfig.useRsp) throw std::invalid_argument("Lifted Two-Phase does not include RSP");
    const auto start = std::chrono::steady_clock::now();
    const std::vector<model::DwcLink> candidates = runConfig.allowWirelessCharging
        ? model::SelectDwcCandidates(instance, runConfig.dwcTopN)
        : std::vector<model::DwcLink>();
    Envr environment;
    Model model = environment.CreateModel(monotoneCuts ? "full_master" : "lifted_two_phase_master");
    MasterState master = BuildMaster(model, instance, candidates);
    solver::Configure(model, solverConfig, solverConfig.phase2MasterLog);

    const solver::Result phaseOne = RunPhaseOne(model, master, instance, candidates,
        solverConfig, start);
    if (phaseOne.status != solver::Status::Optimal) return phaseOne;
    std::cout << "Phase 1 completed: UB=" << phaseOne.objective
              << ", LB=" << phaseOne.bound
              << ", elapsed_ms=" << Elapsed(start) * 1000.0 << '\n';

    master.routeThetaLink.Remove();
    Expr scenarioSum(0.0);
    for (size_t scenario = 0; scenario < master.routeTheta.size(); ++scenario) {
        const Var theta = model.AddVar(0.0, COPT_INFINITY, 0.0, COPT_CONTINUOUS,
            ("scenario_theta_p2_" + std::to_string(scenario)).c_str());
        master.scenarioTheta.push_back(theta);
        scenarioSum += theta;
        Expr routeSum(0.0);
        for (Var& routeTheta : master.routeTheta.at(scenario)) routeSum += routeTheta;
        model.AddConstr(routeSum <= theta,
            ("theta_route_to_scenario_" + std::to_string(scenario)).c_str());
    }
    model.AddConstr(scenarioSum <= master.theta, "theta_scenario_sum_p2");
    model.Update();
    const double targetGap = runConfig.phase2Gap >= 0.0
        ? runConfig.phase2Gap : solverConfig.mipGap;
    model.SetDblParam(COPT_DBLPARAM_RELGAP, targetGap);

    std::vector<ExactScenario> subproblems;
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        subproblems.push_back(BuildExact(instance, scenario, candidates, master, solverConfig));
    }
    std::cout << "Phase 2 initialized: scenarios=" << subproblems.size() << ", RSP=off\n";

    double upperBound = std::numeric_limits<double>::infinity();
    double lowerBound = phaseOne.bound;
    for (int iteration = 0; iteration < kMaximumIterations; ++iteration) {
        double remaining = solverConfig.timeLimit - Elapsed(start);
        if (remaining <= 0.0) break;
        model.SetDblParam(COPT_DBLPARAM_TIMELIMIT, remaining);
        const solver::Result masterResult = solver::Solve(model);
        if (!masterResult.hasSolution) return masterResult;
        lowerBound = std::max(lowerBound, masterResult.bound);
        const std::vector<double> values = Values(master.design);
        const double thetaValue = master.theta.Get(COPT_DBLINFO_VALUE);
        std::vector<double> thetaValues;
        for (Var& theta : master.scenarioTheta) thetaValues.push_back(theta.Get(COPT_DBLINFO_VALUE));

        bool infeasible = false;
        int feasibilityCuts = 0;
        int optimalityCuts = 0;
        double recourse = 0.0;
        double subproblemSeconds = 0.0;
        for (size_t scenario = 0; scenario < subproblems.size(); ++scenario) {
            remaining = solverConfig.timeLimit - Elapsed(start);
            const solver::Result result = EvaluateExact(subproblems.at(scenario), master,
                scenario, values, remaining);
            subproblemSeconds += result.seconds;
            const Expr mismatch = Mismatch(master, values, subproblems.at(scenario).relevant);
            const Expr improvements = MonotoneImprovements(master, scenario, values,
                subproblems.at(scenario).relevant);
            if (result.status == solver::Status::Infeasible) {
                model.AddConstr((monotoneCuts ? improvements : mismatch) >= 1.0,
                    ("p2_feas_" + std::to_string(iteration) + "_" +
                        std::to_string(scenario)).c_str());
                ++feasibilityCuts;
                infeasible = true;
            } else if (result.status == solver::Status::Optimal) {
                recourse += result.objective;
                if (thetaValues.at(scenario) + kTolerance < result.objective) {
                    const Expr optimalityRhs = monotoneCuts
                        ? result.objective * (1.0 - improvements)
                        : result.objective * (1.0 - mismatch);
                    model.AddConstr(master.scenarioTheta.at(scenario) >= optimalityRhs,
                        ("p2_opt_" + std::to_string(iteration) + "_" +
                            std::to_string(scenario)).c_str());
                    ++optimalityCuts;
                }
            } else return result;
        }
        model.Update();
        if (!infeasible) upperBound = std::min(upperBound, DesignCost(master, values) + recourse);
        std::cout << "Phase 2 iter " << iteration;
        if (infeasible) std::cout << ", feasibility cut x" << feasibilityCuts;
        else std::cout << ", theta=" << thetaValue << ", Q(z)=" << recourse
                       << ", optimality cut x" << optimalityCuts;
        PrintBounds(upperBound, lowerBound);
        std::cout << ", master_ms=" << masterResult.seconds * 1000.0
                  << ", subproblem_ms=" << subproblemSeconds * 1000.0
                  << ", elapsed_ms=" << Elapsed(start) * 1000.0 << '\n';

        if (std::isfinite(upperBound) && (upperBound - lowerBound) /
                std::max(1.0, std::fabs(upperBound)) <= targetGap) {
            solver::Result result;
            result.status = solver::Status::Optimal;
            result.hasSolution = true;
            result.objective = upperBound;
            result.bound = lowerBound;
            result.relativeGap = (upperBound - lowerBound) /
                std::max(1.0, std::fabs(upperBound));
            result.seconds = Elapsed(start);
            std::cout << (monotoneCuts ? "Full" : "Lifted Two-Phase") << " converged.\n";
            return result;
        }
    }
    solver::Result result;
    result.status = solver::Status::TimeLimit;
    result.hasSolution = std::isfinite(upperBound);
    result.objective = upperBound;
    result.bound = lowerBound;
    result.seconds = Elapsed(start);
    return result;
}

solver::Result SolveLiftedTwoPhase(const InstanceData& instance,
    const RunConfig& runConfig, const SolverConfig& solverConfig)
{
    return SolveFramework(instance, runConfig, solverConfig, false);
}

solver::Result SolveFull(const InstanceData& instance,
    const RunConfig& runConfig, const SolverConfig& solverConfig)
{
    return SolveFramework(instance, runConfig, solverConfig, true);
}

} // namespace wireless::algorithm
