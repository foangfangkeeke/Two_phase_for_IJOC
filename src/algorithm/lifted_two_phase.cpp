#include "algorithm/lifted_two_phase.hpp"

#include "algorithm/route_relaxation.hpp"
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
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace wireless::algorithm {
namespace {

constexpr int kMaximumIterations = 5000;
constexpr double kTolerance = 1e-6;
constexpr double kRspCertificateTolerance = 1e-8;
constexpr double kPhaseTwoTargetGap = 0.01;

using BatteryCapacityLimits = std::vector<std::vector<int>>;

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
    struct OperatingCost {
        double daytime = 0.0;
        double overnight = 0.0;
    };

    std::unique_ptr<Envr> environment;
    std::unique_ptr<Model> model;
    std::unique_ptr<Expr> objective;
    std::vector<Var> facilities;
    model::ScenarioVariables variables;
    std::vector<size_t> relevant;
    std::map<std::string, solver::Result> resultCache;
    std::map<std::string, solver::Result> feasibilityCache;
    std::map<std::string, OperatingCost> operatingCostCache;
    double daytimeCostMultiplier = 0.0;
    double overnightCostMultiplier = 0.0;
};

std::string ExactCacheKey(const ExactScenario& subproblem,
    const std::vector<double>& values);

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

std::vector<ScenarioRelaxationResult> EvaluateScenarios(
    std::vector<ScenarioRelaxation>& scenarios, const std::vector<double>& values,
    const SolverConfig& config, const std::chrono::steady_clock::time_point& start,
    int workerLimit)
{
    std::vector<ScenarioRelaxationResult> results(scenarios.size());
    std::atomic<size_t> next{0};
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    const size_t workerCount = std::min(scenarios.size(),
        static_cast<size_t>(std::max(1, workerLimit)));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&]() {
            while (true) {
                const size_t index = next.fetch_add(1);
                if (index >= scenarios.size()) return;
                try {
                    const double remaining = config.timeLimit - Elapsed(start);
                    if (remaining <= 0.0) {
                        results.at(index).status = solver::Status::TimeLimit;
                    } else {
                        results.at(index) = scenarios.at(index).Evaluate(values, remaining);
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

struct CostBreakdown {
    double facilities = 0.0;
    double batteries = 0.0;
    double daytimeEnergy = 0.0;
    double overnightEnergy = 0.0;
};

CostBreakdown IncumbentCostBreakdown(const MasterState& master,
    const std::vector<double>& values, const std::vector<ExactScenario>& subproblems)
{
    CostBreakdown result;
    for (size_t index = 0; index < values.size(); ++index) {
        const double cost = master.costs.at(index) * values.at(index);
        if (index < master.facilities.size()) result.facilities += cost;
        else result.batteries += cost;
    }
    for (const ExactScenario& subproblem : subproblems) {
        const std::string key = ExactCacheKey(subproblem, values);
        const ExactScenario::OperatingCost& operatingCost =
            subproblem.operatingCostCache.at(key);
        result.daytimeEnergy += operatingCost.daytime;
        result.overnightEnergy += operatingCost.overnight;
    }
    return result;
}

void PrintCostBreakdown(const char* source, const MasterState& master,
    const std::vector<double>& values, const std::vector<ExactScenario>& subproblems)
{
    const CostBreakdown costs = IncumbentCostBreakdown(master, values, subproblems);
    const double total = costs.facilities + costs.batteries +
        costs.daytimeEnergy + costs.overnightEnergy;
    const auto percentage = [total](double value) {
        return total > 0.0 ? 100.0 * value / total : 0.0;
    };
    std::cout << "Incumbent cost distribution: source=" << source
              << ", facility=" << costs.facilities
              << " (" << percentage(costs.facilities) << "%)"
              << ", battery=" << costs.batteries
              << " (" << percentage(costs.batteries) << "%)"
              << ", daytime_energy=" << costs.daytimeEnergy
              << " (" << percentage(costs.daytimeEnergy) << "%)"
              << ", overnight_energy=" << costs.overnightEnergy
              << " (" << percentage(costs.overnightEnergy) << "%)"
              << ", total=" << total << '\n';
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

BatteryCapacityLimits ComputeBatteryCapacityLimits(const InstanceData& instance)
{
    BatteryCapacityLimits limits;
    limits.reserve(instance.scenarios.size());
    const double usableSoc = instance.operation.maximumSoc -
        instance.operation.minimumSoc;
    for (const ScenarioData& scenario : instance.scenarios) {
        std::vector<double> maximumEnergy(scenario.schedule.routeNames.size(), 0.0);
        for (size_t bus = 0; bus < scenario.schedule.busIds.size(); ++bus) {
            double distance = scenario.network.startDistanceByBus.at(bus) +
                scenario.network.endDistanceByBus.at(bus);
            for (int trip : scenario.schedule.tripIdsByBus.at(bus)) {
                const auto& stations = scenario.schedule.stationsByTrip.at(
                    static_cast<size_t>(trip));
                for (size_t stop = 0; stop + 1 < stations.size(); ++stop) {
                    distance += scenario.network.linkDistances.at(stations.at(stop)).at(
                        stations.at(stop + 1));
                }
            }
            const size_t route = static_cast<size_t>(
                scenario.schedule.routeByBus.at(bus));
            maximumEnergy.at(route) = std::max(maximumEnergy.at(route),
                distance * instance.operation.energyPerKm);
        }

        std::vector<int> scenarioLimits;
        scenarioLimits.reserve(maximumEnergy.size());
        for (double energy : maximumEnergy) {
            const double requiredCapacity = energy / usableSoc;
            int limit = static_cast<int>(instance.operation.maximumBatteryCapacity);
            for (int capacity : BatteryCapacityLevels(instance.operation,
                     static_cast<int>(instance.operation.maximumBatteryCapacity))) {
                if (capacity + kTolerance >= requiredCapacity) {
                    limit = capacity;
                    break;
                }
            }
            scenarioLimits.push_back(limit);
        }
        limits.push_back(std::move(scenarioLimits));
    }
    return limits;
}

MasterState BuildMaster(Model& model, const InstanceData& instance,
    const std::vector<model::DwcLink>& candidates,
    const BatteryCapacityLimits& batteryCapacityLimits, bool useValidInequality)
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
        const ScenarioData& scenarioData = instance.scenarios.at(scenario);
        const ScheduleData& schedule = scenarioData.schedule;
        const std::vector<int> busesByRoute = model::CountBusesByRoute(schedule);
        const std::vector<model::MaximumTripEnergyChain> energyChains =
            model::BuildMaximumTripEnergyChains(instance, scenarioData, candidates);
        batteries.at(scenario).resize(schedule.routeNames.size());
        for (size_t route = 0; route < schedule.routeNames.size(); ++route) {
            Expr selectOne(0.0);
            Expr availableEnergy(energyChains.at(route).plugInOpportunity);
            const double unitCost = instance.scenarios.at(scenario).descriptor.probability *
                busesByRoute.at(route) * instance.operation.batteryUnitPrice;
            for (int capacity : BatteryCapacityLevels(instance.operation,
                     batteryCapacityLimits.at(scenario).at(route))) {
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
                availableEnergy +=
                    (instance.operation.maximumSoc - instance.operation.minimumSoc) *
                    capacity * variable;
            }
            model.AddConstr(selectOne == 1.0,
                ("selectCap_sc" + std::to_string(scenario) + "_r" +
                    std::to_string(route)).c_str());
            if (useValidInequality) {
                for (size_t facility = 0; facility < facilities.size(); ++facility) {
                    availableEnergy += energyChains.at(route).dwcOpportunities.at(facility) *
                        facilities.at(facility);
                }
                // Usable initial energy plus optimistic charging must cover the duty chain.
                model.AddConstr(availableEnergy >= energyChains.at(route).consumption,
                    ("maxChargeEnergy_sc" + std::to_string(scenario) + "_r" +
                        std::to_string(route)).c_str());
            }
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
    result.daytimeCostMultiplier = instance.scenarios.at(scenario).descriptor.probability *
        instance.operation.dayEnergyPrice;
    result.overnightCostMultiplier = instance.scenarios.at(scenario).descriptor.probability *
        instance.operation.nightEnergyPrice;
    result.environment = std::make_unique<Envr>(solver::CreateEnvironment());
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
    model::ScenarioModelOptions options{false, true};
    for (const auto& route : master.batteries.at(scenario)) {
        options.maximumBatteryCapacities.push_back(route.back().capacity);
    }
    result.variables = model::BuildScenarioModel(*result.model, instance,
        instance.scenarios.at(scenario), scenario, facilities, objective, options);
    for (auto& route : result.variables.batteryChoicesByRoute) {
        for (Var& choice : route) choice.SetType(COPT_CONTINUOUS);
    }
    result.objective = std::make_unique<Expr>(objective);
    result.model->SetObjective(*result.objective, COPT_MINIMIZE);
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

void FixExactResources(ExactScenario& subproblem, const MasterState& master,
    size_t scenario, const std::vector<double>& values)
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
    subproblem.model->Update();
}

std::string ExactCacheKey(const ExactScenario& subproblem,
    const std::vector<double>& values)
{
    std::string cacheKey;
    cacheKey.reserve(subproblem.relevant.size());
    for (size_t index : subproblem.relevant) {
        cacheKey.push_back(values.at(index) > 0.5 ? '1' : '0');
    }
    return cacheKey;
}

solver::Result EvaluateExact(ExactScenario& subproblem, const MasterState& master,
    size_t scenario, const std::vector<double>& values, double remaining)
{
    std::string cacheKey = ExactCacheKey(subproblem, values);
    const auto cached = subproblem.resultCache.find(cacheKey);
    if (cached != subproblem.resultCache.end()) {
        solver::Result result = cached->second;
        result.seconds = 0.0;
        return result;
    }

    FixExactResources(subproblem, master, scenario, values);
    subproblem.model->SetDblParam(COPT_DBLPARAM_TIMELIMIT, std::max(0.001, remaining));
    const solver::Result result = solver::Solve(*subproblem.model);
    if (result.status == solver::Status::Optimal ||
        result.status == solver::Status::Infeasible) {
        subproblem.resultCache.emplace(cacheKey, result);
        if (result.status == solver::Status::Optimal) {
            ExactScenario::OperatingCost operatingCost;
            for (Var& variable : subproblem.variables.daytimeCharge) {
                operatingCost.daytime += variable.Get(COPT_DBLINFO_VALUE) *
                    subproblem.daytimeCostMultiplier;
            }
            for (Var& variable : subproblem.variables.overnightCharge) {
                operatingCost.overnight += variable.Get(COPT_DBLINFO_VALUE) *
                    subproblem.overnightCostMultiplier;
            }
            subproblem.operatingCostCache.emplace(std::move(cacheKey), operatingCost);
        }
    }
    return result;
}

std::vector<solver::Result> EvaluateExactScenarios(
    std::vector<ExactScenario>& subproblems, const MasterState& master,
    const std::vector<double>& values, const SolverConfig& config,
    const std::chrono::steady_clock::time_point& start, int workerLimit)
{
    std::vector<solver::Result> results(subproblems.size());
    std::atomic<size_t> next{0};
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    const size_t workerCount = std::min(subproblems.size(),
        static_cast<size_t>(std::max(1, workerLimit)));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&]() {
            while (true) {
                const size_t scenario = next.fetch_add(1);
                if (scenario >= subproblems.size()) return;
                try {
                    const double remaining = config.timeLimit - Elapsed(start);
                    if (remaining <= 0.0) {
                        results.at(scenario).status = solver::Status::TimeLimit;
                    } else {
                        results.at(scenario) = EvaluateExact(subproblems.at(scenario),
                            master, scenario, values, remaining);
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

solver::Result EvaluateExactFeasibility(ExactScenario& subproblem,
    const MasterState& master, size_t scenario, const std::vector<double>& values,
    double remaining)
{
    const std::string cacheKey = ExactCacheKey(subproblem, values);
    const auto exact = subproblem.resultCache.find(cacheKey);
    if (exact != subproblem.resultCache.end()) {
        solver::Result result = exact->second;
        result.seconds = 0.0;
        return result;
    }
    const auto cached = subproblem.feasibilityCache.find(cacheKey);
    if (cached != subproblem.feasibilityCache.end()) {
        solver::Result result = cached->second;
        result.seconds = 0.0;
        return result;
    }

    FixExactResources(subproblem, master, scenario, values);
    subproblem.model->SetObjective(Expr(0.0), COPT_MINIMIZE);
    subproblem.model->SetDblParam(COPT_DBLPARAM_TIMELIMIT, std::max(0.001, remaining));
    subproblem.model->Update();
    solver::Result result;
    try {
        result = solver::Solve(*subproblem.model);
    } catch (...) {
        subproblem.model->SetObjective(*subproblem.objective, COPT_MINIMIZE);
        subproblem.model->Update();
        throw;
    }
    subproblem.model->SetObjective(*subproblem.objective, COPT_MINIMIZE);
    subproblem.model->Update();
    if (result.hasSolution || result.status == solver::Status::Infeasible) {
        subproblem.feasibilityCache.emplace(cacheKey, result);
    }
    return result;
}

size_t SelectedBatteryLevel(const std::vector<BatteryLevel>& levels,
    const std::vector<double>& values)
{
    for (size_t level = 0; level < levels.size(); ++level) {
        if (values.at(levels.at(level).index) > 0.5) return level;
    }
    throw std::runtime_error("No battery level selected");
}

void SelectBatteryLevel(const std::vector<BatteryLevel>& levels, size_t selected,
    std::vector<double>& values)
{
    for (size_t level = 0; level < levels.size(); ++level) {
        values.at(levels.at(level).index) = level == selected ? 1.0 : 0.0;
    }
}

struct RepairResource {
    enum class Type { Facility, Battery };
    Type type = Type::Battery;
    size_t index = 0;
    double cost = 0.0;
    double rspMarginal = 0.0;
};

struct RepairResult {
    std::vector<double> infeasibleAnchor;
    std::vector<double> feasibleDesign;
    double seconds = 0.0;
    int probes = 0;
    int batteryProbes = 0;
    int facilityProbes = 0;
    int acceptedResources = 0;
    int acceptedBatterySteps = 0;
    int acceptedBatteryLayers = 0;
    int acceptedFacilities = 0;
    int rspCertificateChecks = 0;
    int rspCertificateHits = 0;
    int rspCertifiedFacilities = 0;
    int rspDeferredFacilities = 0;
};

struct RepairIncumbent {
    std::vector<double> design;
    double objective = std::numeric_limits<double>::infinity();
};

enum class ProbeClassification { Infeasible, Feasible, Unknown };

struct ProbeResult {
    ProbeClassification classification = ProbeClassification::Unknown;
    solver::Result solve;
};

std::string BatteryVector(const MasterState& master, size_t scenario,
    const std::vector<double>& values)
{
    std::ostringstream out;
    for (size_t route = 0; route < master.batteries.at(scenario).size(); ++route) {
        if (route != 0) out << ';';
        const auto& levels = master.batteries.at(scenario).at(route);
        out << route << ':' << levels.at(SelectedBatteryLevel(levels, values)).capacity;
    }
    return out.str();
}

std::string RepairKey(const MasterState& master, const ExactScenario& subproblem,
    size_t scenario, const std::vector<double>& values)
{
    std::ostringstream out;
    for (size_t index : subproblem.relevant) {
        if (index < master.facilities.size()) out << (values.at(index) > 0.5 ? '1' : '0');
    }
    out << '|';
    for (const auto& route : master.batteries.at(scenario)) {
        out << SelectedBatteryLevel(route, values) << ',';
    }
    return out.str();
}

void MaximizeResource(const MasterState& master, size_t scenario,
    const RepairResource& resource, std::vector<double>& values)
{
    if (resource.type == RepairResource::Type::Facility) {
        values.at(resource.index) = 1.0;
    } else {
        const auto& levels = master.batteries.at(scenario).at(resource.index);
        SelectBatteryLevel(levels, levels.size() - 1, values);
    }
}

std::vector<double> MaximizePrefix(const MasterState& master, size_t scenario,
    const std::vector<double>& anchor, const std::vector<RepairResource>& resources,
    size_t cursor, size_t count)
{
    std::vector<double> result = anchor;
    for (size_t offset = 0; offset < count; ++offset) {
        MaximizeResource(master, scenario, resources.at(cursor + offset), result);
    }
    return result;
}

RepairResult RepairInfeasibleScenario(ExactScenario& subproblem,
    const MasterState& master, size_t scenario,
    const std::vector<double>& original, const SolverConfig& config,
    const std::chrono::steady_clock::time_point& globalStart,
    const ScenarioRelaxationResult* rspCertificate)
{
    const auto repairStart = std::chrono::steady_clock::now();
    RepairResult result;
    result.infeasibleAnchor = original;
    double feasibleDesignCost = std::numeric_limits<double>::infinity();

    const auto rspCertificateValue = [&](const std::vector<double>& candidate) {
        double value = rspCertificate->constant;
        for (size_t index = 0; index < candidate.size(); ++index) {
            value += rspCertificate->coefficients.at(index) * candidate.at(index);
        }
        return value;
    };
    const bool hasRspCertificate = rspCertificate != nullptr &&
        rspCertificate->status == solver::Status::Infeasible &&
        rspCertificate->coefficients.size() == original.size() &&
        std::isfinite(rspCertificateValue(original)) &&
        rspCertificateValue(original) < -kRspCertificateTolerance;

    const auto recordFeasibleDesign = [&](const std::vector<double>& candidate) {
        const double cost = DesignCost(master, candidate);
        if (cost + kTolerance < feasibleDesignCost) {
            feasibleDesignCost = cost;
            result.feasibleDesign = candidate;
        }
    };

    const auto byCost = [](const RepairResource& lhs, const RepairResource& rhs) {
            if (std::fabs(lhs.cost - rhs.cost) > 1e-12) {
                return lhs.cost < rhs.cost;
            }
            return lhs.index < rhs.index;
    };

    const auto batteryResources = [&](const std::vector<double>& anchor) {
        std::vector<RepairResource> resources;
        for (size_t route = 0; route < master.batteries.at(scenario).size(); ++route) {
            const auto& levels = master.batteries.at(scenario).at(route);
            const size_t current = SelectedBatteryLevel(levels, anchor);
            if (current + 1 >= levels.size()) continue;
            resources.push_back({RepairResource::Type::Battery, route,
                master.costs.at(levels.at(current + 1).index) -
                    master.costs.at(levels.at(current).index)});
        }
        std::sort(resources.begin(), resources.end(), byCost);
        return resources;
    };

    const std::set<size_t> relevantResources(
        subproblem.relevant.begin(), subproblem.relevant.end());
    std::vector<RepairResource> facilityResources;
    for (size_t facility = 0; facility < master.facilities.size(); ++facility) {
        if (relevantResources.count(facility) != 0 && original.at(facility) < 0.5) {
            facilityResources.push_back({RepairResource::Type::Facility, facility,
                master.costs.at(facility), hasRspCertificate
                    ? rspCertificate->coefficients.at(facility) : 0.0});
        }
    }
    std::sort(facilityResources.begin(), facilityResources.end(), byCost);

    std::map<std::string, ProbeResult> cache;
    const auto probe = [&](const std::vector<double>& values) {
        const std::string key = RepairKey(master, subproblem, scenario, values);
        const auto found = cache.find(key);
        if (found != cache.end()) return found->second;

        ProbeResult probeResult;
        const double remaining = config.timeLimit - Elapsed(globalStart);
        if (remaining <= 0.0) return probeResult;
        probeResult.solve = EvaluateExactFeasibility(
            subproblem, master, scenario, values, remaining);
        ++result.probes;
        result.seconds += probeResult.solve.seconds;
        if (probeResult.solve.status == solver::Status::Infeasible) {
            probeResult.classification = ProbeClassification::Infeasible;
        } else if (probeResult.solve.hasSolution) {
            probeResult.classification = ProbeClassification::Feasible;
        }
        cache.emplace(key, probeResult);
        return probeResult;
    };

    const auto incrementBatteryPrefix = [&](const std::vector<double>& anchor,
        const std::vector<RepairResource>& resources, size_t count) {
        std::vector<double> candidate = anchor;
        for (size_t offset = 0; offset < count; ++offset) {
            const auto& levels = master.batteries.at(scenario).at(
                resources.at(offset).index);
            const size_t current = SelectedBatteryLevel(levels, candidate);
            SelectBatteryLevel(levels, current + 1, candidate);
        }
        return candidate;
    };

    const auto batteryProbe = [&](const std::vector<double>& candidate) {
        const int before = result.probes;
        const ProbeResult probeResult = probe(candidate);
        result.batteryProbes += result.probes - before;
        return probeResult;
    };
    const auto facilityProbe = [&](const std::vector<double>& candidate) {
        const int before = result.probes;
        const ProbeResult probeResult = probe(candidate);
        result.facilityProbes += result.probes - before;
        return probeResult;
    };

    try {
        const auto searchBatteryCoarse = [&]() {
            const std::vector<RepairResource> resources =
                batteryResources(result.infeasibleAnchor);
            if (resources.empty()) return true;
            const std::vector<double> nextLayer = incrementBatteryPrefix(
                result.infeasibleAnchor, resources, resources.size());
            const ProbeResult layerResult = batteryProbe(nextLayer);
            if (layerResult.classification == ProbeClassification::Infeasible) {
                result.infeasibleAnchor = nextLayer;
                result.acceptedResources += static_cast<int>(resources.size());
                result.acceptedBatterySteps += static_cast<int>(resources.size());
                ++result.acceptedBatteryLayers;
                return true;
            }
            if (layerResult.classification == ProbeClassification::Unknown) return false;
            recordFeasibleDesign(nextLayer);

            for (size_t prefix = resources.size() / 2; prefix > 0; prefix /= 2) {
                const std::vector<double> candidate = incrementBatteryPrefix(
                    result.infeasibleAnchor, resources, prefix);
                const ProbeResult prefixResult = batteryProbe(candidate);
                if (prefixResult.classification == ProbeClassification::Infeasible) {
                    result.infeasibleAnchor = candidate;
                    result.acceptedResources += static_cast<int>(prefix);
                    result.acceptedBatterySteps += static_cast<int>(prefix);
                    return true;
                }
                if (prefixResult.classification == ProbeClassification::Unknown) {
                    return false;
                }
                recordFeasibleDesign(candidate);
            }
            return true;
        };

        const auto searchFacilities = [&]() {
            if (facilityResources.empty()) return true;
            std::vector<RepairResource> exactResources = facilityResources;

            // The battery-repaired anchor is a complete Master design: it retains
            // every facility already selected by the current Master candidate.
            // If that full design still violates the old RSP cut, add unbuilt
            // facilities by increasing RSP marginal and accept only the prefix
            // that remains strictly certified infeasible.
            if (hasRspCertificate) {
                ++result.rspCertificateChecks;
                if (rspCertificateValue(result.infeasibleAnchor) <
                    -kRspCertificateTolerance) {
                    ++result.rspCertificateHits;
                    std::vector<RepairResource> rspOrder = facilityResources;
                    std::sort(rspOrder.begin(), rspOrder.end(),
                        [&](const RepairResource& lhs, const RepairResource& rhs) {
                            if (std::fabs(lhs.rspMarginal - rhs.rspMarginal) > 1e-12) {
                                return lhs.rspMarginal < rhs.rspMarginal;
                            }
                            return byCost(lhs, rhs);
                        });

                    size_t certifiedPrefix = 0;
                    for (size_t prefix = 1; prefix <= rspOrder.size(); ++prefix) {
                        const std::vector<double> candidate = MaximizePrefix(
                            master, scenario, result.infeasibleAnchor,
                            rspOrder, 0, prefix);
                        ++result.rspCertificateChecks;
                        if (rspCertificateValue(candidate) <
                            -kRspCertificateTolerance) {
                            ++result.rspCertificateHits;
                            certifiedPrefix = prefix;
                        } else {
                            break;
                        }
                    }

                    if (certifiedPrefix > 0) {
                        result.infeasibleAnchor = MaximizePrefix(master, scenario,
                            result.infeasibleAnchor, rspOrder, 0, certifiedPrefix);
                        result.acceptedResources += static_cast<int>(certifiedPrefix);
                        result.acceptedFacilities += static_cast<int>(certifiedPrefix);
                        result.rspCertifiedFacilities +=
                            static_cast<int>(certifiedPrefix);
                    }

                    if (certifiedPrefix == rspOrder.size()) return true;

                    // The first facility that exhausts the RSP certificate is the
                    // first Exact candidate. Keep the remaining facilities in the
                    // same increasing-marginal order for the shallow 1,2,4 search.
                    exactResources.assign(
                        rspOrder.begin() + certifiedPrefix, rspOrder.end());
                    result.rspDeferredFacilities =
                        static_cast<int>(exactResources.size());
                } else {
                    // Battery changes have exhausted this old certificate.  Fall
                    // back exactly to the original construction-cost order.
                    result.rspDeferredFacilities =
                        static_cast<int>(exactResources.size());
                }
            }

            const std::vector<double> facilityBase = result.infeasibleAnchor;
            size_t accepted = 0;
            for (size_t prefix = 1;;) {
                const std::vector<double> candidate = MaximizePrefix(master, scenario,
                    facilityBase, exactResources, 0, prefix);
                const ProbeResult prefixResult = facilityProbe(candidate);
                if (prefixResult.classification == ProbeClassification::Infeasible) {
                    result.infeasibleAnchor = candidate;
                    accepted = prefix;
                } else if (prefixResult.classification == ProbeClassification::Feasible) {
                    recordFeasibleDesign(candidate);
                    break;
                } else {
                    result.acceptedResources += static_cast<int>(accepted);
                    result.acceptedFacilities += static_cast<int>(accepted);
                    return false;
                }

                if (prefix == exactResources.size()) break;
                prefix = prefix > exactResources.size() / 2
                    ? exactResources.size() : prefix * 2;
            }
            result.acceptedResources += static_cast<int>(accepted);
            result.acceptedFacilities += static_cast<int>(accepted);
            return true;
        };

        if (searchBatteryCoarse()) searchFacilities();
    } catch (...) {
        FixExactResources(subproblem, master, scenario, original);
        throw;
    }

    FixExactResources(subproblem, master, scenario, original);
    result.seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - repairStart).count();
    return result;
}

struct RepairRequest {
    size_t scenario = 0;
    int failures = 0;
};

struct ScenarioRepairResult {
    size_t scenario = 0;
    int failures = 0;
    RepairResult repair;
};

std::vector<ScenarioRepairResult> RepairScenarios(
    std::vector<ExactScenario>& subproblems, const MasterState& master,
    const std::vector<double>& values, const SolverConfig& config,
    const std::chrono::steady_clock::time_point& start,
    const std::vector<RepairRequest>& requests, int workerLimit,
    const std::vector<ScenarioRelaxationResult>* rspCertificates)
{
    std::vector<ScenarioRepairResult> results(requests.size());
    std::atomic<size_t> next{0};
    std::exception_ptr firstException;
    std::mutex exceptionMutex;
    const size_t workerCount = std::min(requests.size(),
        static_cast<size_t>(std::max(1, workerLimit)));
    std::vector<std::thread> workers;
    workers.reserve(workerCount);
    for (size_t worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&]() {
            while (true) {
                const size_t requestIndex = next.fetch_add(1);
                if (requestIndex >= requests.size()) return;
                try {
                    const RepairRequest& request = requests.at(requestIndex);
                    ScenarioRepairResult& result = results.at(requestIndex);
                    result.scenario = request.scenario;
                    result.failures = request.failures;
                    result.repair = RepairInfeasibleScenario(
                        subproblems.at(request.scenario), master, request.scenario,
                        values, config, start,
                        rspCertificates == nullptr ? nullptr :
                            &rspCertificates->at(request.scenario));
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

RepairIncumbent EvaluateCombinedRepair(
    std::vector<ExactScenario>& subproblems, const MasterState& master,
    const std::vector<double>& baseDesign,
    const std::vector<std::vector<double>>& scenarioDesigns,
    const SolverConfig& config,
    const std::chrono::steady_clock::time_point& globalStart,
    double& subproblemSeconds, int workerLimit)
{
    RepairIncumbent result;
    if (std::any_of(scenarioDesigns.begin(), scenarioDesigns.end(),
            [](const std::vector<double>& design) { return design.empty(); })) {
        return result;
    }

    result.design = baseDesign;
    for (size_t scenario = 0; scenario < scenarioDesigns.size(); ++scenario) {
        // Facility repair orders are scenario-specific, but facilities are not.
        // Form one shared layout before validating every Exact SP.
        for (size_t facility = 0; facility < master.facilities.size(); ++facility) {
            result.design.at(facility) = std::max(result.design.at(facility),
                scenarioDesigns.at(scenario).at(facility));
        }
        for (const auto& route : master.batteries.at(scenario)) {
            for (const BatteryLevel& level : route) {
                result.design.at(level.index) =
                    scenarioDesigns.at(scenario).at(level.index);
            }
        }
    }

    if (config.timeLimit - Elapsed(globalStart) <= 0.0) return RepairIncumbent();
    const auto exactStart = std::chrono::steady_clock::now();
    const std::vector<solver::Result> exactResults = EvaluateExactScenarios(
        subproblems, master, result.design, config, globalStart, workerLimit);
    subproblemSeconds += Elapsed(exactStart);
    double recourse = 0.0;
    for (const solver::Result& exact : exactResults) {
        if (exact.status != solver::Status::Optimal) return RepairIncumbent();
        recourse += exact.objective;
    }
    result.objective = DesignCost(master, result.design) + recourse;
    return result;
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

Expr AffineCut(const MasterState& master, const ScenarioRelaxationResult& result)
{
    Expr cut(result.constant);
    for (size_t index = 0; index < result.coefficients.size(); ++index) {
        const double coefficient = result.coefficients.at(index);
        if (std::fabs(coefficient) > 1e-12) {
            cut += coefficient * master.design.at(index);
        }
    }
    return cut;
}

} // namespace

solver::Result SolveFramework(const InstanceData& instance,
    const RunConfig& runConfig, const SolverConfig& solverConfig,
    bool repairEnhancedCuts)
{
    const auto start = std::chrono::steady_clock::now();
    const auto preprocessStart = std::chrono::steady_clock::now();
    const BatteryCapacityLimits batteryCapacityLimits =
        ComputeBatteryCapacityLimits(instance);
    std::cout << "Battery upper-bound preprocess_ms="
              << std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - preprocessStart).count()
              << '\n';
    const std::vector<model::DwcLink> candidates = runConfig.allowWirelessCharging
        ? model::SelectDwcCandidates(instance, runConfig.dwcTopN)
        : std::vector<model::DwcLink>();
    Envr environment = solver::CreateEnvironment();
    Model model = environment.CreateModel(
        repairEnhancedCuts ? "full_master" : "lifted_two_phase_master");
    MasterState master = BuildMaster(model, instance, candidates, batteryCapacityLimits,
        runConfig.useValidInequality);
    solver::Configure(model, solverConfig, solverConfig.phase2MasterLog);

    const solver::Result phaseOne = RunPhaseOne(model, master, instance, candidates,
        solverConfig, start);
    if (phaseOne.status != solver::Status::Optimal) {
        solver::Result result = phaseOne;
        result.hasSolution = false;
        result.objective = std::numeric_limits<double>::infinity();
        result.relativeGap = 1.0;
        result.seconds = Elapsed(start);
        std::cout << (repairEnhancedCuts ? "Full" : "Lifted Two-Phase")
                  << " status=" << solver::ToString(result.status);
        PrintBounds(result.objective, result.bound);
        std::cout << ", elapsed_ms=" << result.seconds * 1000.0 << '\n';
        return result;
    }
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
    const int scenarioWorkerLimit = runConfig.rspWorkers > 0
        ? runConfig.rspWorkers : std::min(3, solverConfig.threads);
    const int actualScenarioWorkers = std::min(
        static_cast<int>(instance.scenarios.size()), scenarioWorkerLimit);
    const int actualScenarioThreads = runConfig.rspThreads > 0
        ? runConfig.rspThreads
        : std::max(1, solverConfig.threads / actualScenarioWorkers);
    if (actualScenarioWorkers * actualScenarioThreads > solverConfig.threads) {
        throw std::invalid_argument(
            "Phase 2 scenario workers multiplied by scenario threads exceeds "
            "solver thread budget");
    }
    SolverConfig scenarioSolverConfig = solverConfig;
    scenarioSolverConfig.threads = actualScenarioThreads;
    std::vector<ExactScenario> subproblems;
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        subproblems.push_back(BuildExact(instance, scenario, candidates, master,
            scenarioSolverConfig));
    }
    std::vector<ScenarioRelaxation> relaxedSubproblems;
    if (runConfig.useRsp) {
        for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
            std::vector<std::vector<size_t>> batteryIndices;
            for (const auto& route : master.batteries.at(scenario)) {
                std::vector<size_t> routeIndices;
                for (const BatteryLevel& level : route) routeIndices.push_back(level.index);
                batteryIndices.push_back(std::move(routeIndices));
            }
            relaxedSubproblems.emplace_back(instance, scenario, candidates, batteryIndices,
                batteryCapacityLimits.at(scenario), master.design.size(), solverConfig,
                actualScenarioThreads);
        }
    }
    std::cout << "Phase 2 initialized: scenarios=" << subproblems.size()
              << ", target_gap=" << kPhaseTwoTargetGap * 100.0 << '%'
              << ", scenario_workers=" << actualScenarioWorkers
              << ", scenario_threads=" << actualScenarioThreads
              << ", RSP=" << (runConfig.useRsp ? "on" : "off");
    std::cout << '\n';

    double upperBound = std::numeric_limits<double>::infinity();
    double lowerBound = phaseOne.bound;
    const auto finalResult = [&](solver::Status status) {
        solver::Result result;
        result.status = status;
        result.hasSolution = std::isfinite(upperBound);
        result.objective = upperBound;
        result.bound = lowerBound;
        result.relativeGap = result.hasSolution
            ? (upperBound - lowerBound) / std::max(1.0, std::fabs(upperBound))
            : 1.0;
        result.seconds = Elapsed(start);
        std::cout << (repairEnhancedCuts ? "Full" : "Lifted Two-Phase")
                  << " status=" << solver::ToString(status);
        PrintBounds(upperBound, lowerBound);
        std::cout << ", elapsed_ms=" << result.seconds * 1000.0 << '\n';
        return result;
    };
    const auto hasConverged = [&]() {
        return std::isfinite(upperBound) && (upperBound - lowerBound) /
            std::max(1.0, std::fabs(upperBound)) <= kPhaseTwoTargetGap;
    };
    constexpr int kRepairFailureThreshold = 2;
    std::vector<int> unresolvedFailures(subproblems.size(), 0);
    for (int iteration = 0; iteration < kMaximumIterations; ++iteration) {
        double remaining = solverConfig.timeLimit - Elapsed(start);
        if (remaining <= 0.0) break;
        model.SetDblParam(COPT_DBLPARAM_TIMELIMIT, remaining);
        const solver::Result masterResult = solver::Solve(model);
        if (masterResult.status == solver::Status::TimeLimit) {
            lowerBound = std::max(lowerBound, masterResult.bound);
            break;
        }
        if (!masterResult.hasSolution) return finalResult(masterResult.status);
        lowerBound = std::max(lowerBound, masterResult.bound);
        if (hasConverged()) return finalResult(solver::Status::Optimal);
        const std::vector<double> values = Values(master.design);
        const double thetaValue = master.theta.Get(COPT_DBLINFO_VALUE);
        std::vector<double> thetaValues;
        for (Var& theta : master.scenarioTheta) thetaValues.push_back(theta.Get(COPT_DBLINFO_VALUE));

        bool infeasible = false;
        int feasibilityCuts = 0;
        int optimalityCuts = 0;
        double recourse = 0.0;
        double subproblemSeconds = 0.0;
        double rspSeconds = 0.0;
        int rspFeasibilityCuts = 0;
        int rspOptimalityCuts = 0;
        double rspRecourse = 0.0;
        bool rspInfeasible = false;
        double repairSeconds = 0.0;
        int repairProbes = 0;
        int strengthenedCuts = 0;
        double repairUpperBound = std::numeric_limits<double>::infinity();
        std::vector<std::vector<double>> rspRepairDesigns(
            subproblems.size(), values);
        bool allRspInfeasibleRepaired = true;
        if (runConfig.useRsp) {
            bool rspTimedOut = false;
            std::vector<bool> rspCertifiedInfeasible(subproblems.size(), false);
            std::vector<RepairRequest> rspRepairRequests;
            const auto rspStart = std::chrono::steady_clock::now();
            const std::vector<ScenarioRelaxationResult> rspResults =
                EvaluateScenarios(relaxedSubproblems, values, solverConfig, start,
                    actualScenarioWorkers);
            rspSeconds = Elapsed(rspStart);
            for (size_t scenario = 0; scenario < rspResults.size(); ++scenario) {
                const ScenarioRelaxationResult& result = rspResults.at(scenario);
                if (result.status == solver::Status::Infeasible) {
                    if (!result.coefficients.empty()) {
                        model.AddConstr(AffineCut(master, result) >= 0.0,
                            ("p2_rsp_feas_" + std::to_string(iteration) + "_" +
                                std::to_string(scenario)).c_str());
                    } else {
                        const Expr mismatch = Mismatch(master, values,
                            subproblems.at(scenario).relevant);
                        model.AddConstr(mismatch >= 1.0,
                            ("p2_rsp_fallback_" + std::to_string(iteration) + "_" +
                                std::to_string(scenario)).c_str());
                    }
                    ++rspFeasibilityCuts;
                    rspInfeasible = true;
                    rspCertifiedInfeasible.at(scenario) = true;
                    if (repairEnhancedCuts) ++unresolvedFailures.at(scenario);
                    if (repairEnhancedCuts &&
                        unresolvedFailures.at(scenario) >= kRepairFailureThreshold) {
                        rspRepairRequests.push_back(
                            {scenario, unresolvedFailures.at(scenario)});
                    } else {
                        allRspInfeasibleRepaired = false;
                    }
                } else if (result.status == solver::Status::Optimal) {
                    rspRecourse += result.objective;
                    if (thetaValues.at(scenario) + kTolerance < result.objective) {
                        model.AddConstr(master.scenarioTheta.at(scenario) >=
                            AffineCut(master, result),
                            ("p2_rsp_opt_" + std::to_string(iteration) + "_" +
                                std::to_string(scenario)).c_str());
                        ++rspOptimalityCuts;
                    }
                } else if (result.status == solver::Status::Numerical ||
                    result.status == solver::Status::TimeLimit ||
                    result.status == solver::Status::Interrupted) {
                    std::cout << "Phase 2 iter " << iteration
                              << ", RSP scenario=" << scenario
                              << " skipped after status="
                              << solver::ToString(result.status) << '\n';
                    if (result.status == solver::Status::TimeLimit) {
                        rspTimedOut = true;
                    }
                } else {
                    return finalResult(result.status);
                }
            }
            if (rspTimedOut || solverConfig.timeLimit - Elapsed(start) <= 0.0) break;
            if (!rspRepairRequests.empty()) {
                const auto repairBatchStart = std::chrono::steady_clock::now();
                const std::vector<ScenarioRepairResult> repairs = RepairScenarios(
                    subproblems, master, values, solverConfig, start,
                    rspRepairRequests, actualScenarioWorkers, &rspResults);
                repairSeconds += Elapsed(repairBatchStart);
                for (const ScenarioRepairResult& completed : repairs) {
                    const size_t scenario = completed.scenario;
                    const RepairResult& repair = completed.repair;
                    repairProbes += repair.probes;
                    if (!repair.feasibleDesign.empty()) {
                        rspRepairDesigns.at(scenario) = repair.feasibleDesign;
                    } else {
                        allRspInfeasibleRepaired = false;
                    }
                    const Expr improvements = MonotoneImprovements(master, scenario,
                        repair.infeasibleAnchor, subproblems.at(scenario).relevant);
                    model.AddConstr(improvements >= 1.0,
                        ("p2_rsp_repair_" + std::to_string(iteration) + "_" +
                            std::to_string(scenario)).c_str());
                    if (repair.acceptedResources > 0) ++strengthenedCuts;
                    std::cout << "Repair iter=" << iteration
                              << ", scenario=" << scenario
                              << ", trigger=rsp-unresolved"
                              << ", failures=" << completed.failures
                              << ", before=" << BatteryVector(master, scenario, values)
                              << ", after=" << BatteryVector(master, scenario,
                                    repair.infeasibleAnchor)
                              << ", probes=" << repair.probes
                              << ", battery_probes=" << repair.batteryProbes
                              << ", facility_probes=" << repair.facilityProbes
                              << ", repair_ms=" << repair.seconds * 1000.0
                              << ", accepted_resources=" << repair.acceptedResources
                              << ", accepted_battery_steps="
                              << repair.acceptedBatterySteps
                              << ", accepted_battery_layers="
                              << repair.acceptedBatteryLayers
                              << ", accepted_facilities="
                              << repair.acceptedFacilities
                              << ", rsp_cert_checks="
                              << repair.rspCertificateChecks
                              << ", rsp_cert_hits="
                              << repair.rspCertificateHits
                              << ", rsp_certified_facilities="
                              << repair.rspCertifiedFacilities
                              << ", rsp_deferred_facilities="
                              << repair.rspDeferredFacilities
                              << '\n';
                }
            }
            if (repairEnhancedCuts && rspInfeasible && allRspInfeasibleRepaired) {
                const RepairIncumbent incumbent = EvaluateCombinedRepair(
                    subproblems, master, values, rspRepairDesigns,
                    solverConfig, start, subproblemSeconds,
                    actualScenarioWorkers);
                repairUpperBound = incumbent.objective;
                if (repairUpperBound + kTolerance < upperBound) {
                    PrintCostBreakdown("repair-rsp", master, incumbent.design,
                        subproblems);
                    upperBound = repairUpperBound;
                }
                // A repaired incumbent is a different resource design.  It does not
                // break the infeasibility streak of the current Master candidates.
            }
            model.Update();
            // RSP is only a screening relaxation.  A feasible relaxation does not
            // certify feasibility of the integer scenario problem, so retain any
            // RSP optimality cuts and continue to Exact SP whenever every RSP is
            // feasible.
            if (rspInfeasible) {
                if (repairEnhancedCuts) {
                    for (size_t scenario = 0;
                        scenario < rspCertifiedInfeasible.size(); ++scenario) {
                        if (!rspCertifiedInfeasible.at(scenario)) {
                            unresolvedFailures.at(scenario) = 0;
                        }
                    }
                }
                std::cout << "Phase 2 iter " << iteration
                          << ", RSP feasibility cut x" << rspFeasibilityCuts
                          << ", RSP optimality cut x" << rspOptimalityCuts
                          << ", RSP Q(z)=" << rspRecourse;
                PrintBounds(upperBound, lowerBound);
                std::cout << ", master_ms=" << masterResult.seconds * 1000.0
                          << ", rsp_ms=" << rspSeconds * 1000.0
                          << ", subproblem_ms=" << subproblemSeconds * 1000.0
                          << ", repair_probes=" << repairProbes
                          << ", repair_ms=" << repairSeconds * 1000.0
                          << ", strengthened_cuts=" << strengthenedCuts
                          << ", repair_ub=";
                if (std::isfinite(repairUpperBound)) std::cout << repairUpperBound;
                else std::cout << "inf";
                std::cout
                          << ", elapsed_ms=" << Elapsed(start) * 1000.0 << '\n';
                if (hasConverged()) return finalResult(solver::Status::Optimal);
                continue;
            }
        }
        bool exactTimedOut = false;
        std::vector<std::vector<double>> repairFeasibleDesigns(subproblems.size());
        remaining = solverConfig.timeLimit - Elapsed(start);
        if (remaining <= 0.0) break;
        const auto exactStart = std::chrono::steady_clock::now();
        const std::vector<solver::Result> exactResults = EvaluateExactScenarios(
            subproblems, master, values, solverConfig, start,
            actualScenarioWorkers);
        subproblemSeconds += Elapsed(exactStart);
        std::vector<RepairRequest> exactRepairRequests;
        for (size_t scenario = 0; scenario < subproblems.size(); ++scenario) {
            const solver::Result& result = exactResults.at(scenario);
            if (result.status == solver::Status::Infeasible) {
                if (repairEnhancedCuts) ++unresolvedFailures.at(scenario);
                if (repairEnhancedCuts &&
                    unresolvedFailures.at(scenario) >= kRepairFailureThreshold) {
                    exactRepairRequests.push_back(
                        {scenario, unresolvedFailures.at(scenario)});
                }
            } else if (result.status == solver::Status::Optimal) {
                if (repairEnhancedCuts) unresolvedFailures.at(scenario) = 0;
                repairFeasibleDesigns.at(scenario) = values;
                recourse += result.objective;
                const Expr mismatch = Mismatch(master, values,
                    subproblems.at(scenario).relevant);
                if (thetaValues.at(scenario) + kTolerance < result.bound) {
                    const Expr optimalityRhs = result.bound * (1.0 - mismatch);
                    model.AddConstr(master.scenarioTheta.at(scenario) >= optimalityRhs,
                        ("p2_opt_" + std::to_string(iteration) + "_" +
                            std::to_string(scenario)).c_str());
                    ++optimalityCuts;
                }
            } else if (result.status == solver::Status::TimeLimit) {
                exactTimedOut = true;
                break;
            } else return finalResult(result.status);
        }
        if (exactTimedOut) break;
        std::vector<int> exactRepairIndex(subproblems.size(), -1);
        std::vector<ScenarioRepairResult> exactRepairs;
        if (!exactRepairRequests.empty()) {
            const auto repairBatchStart = std::chrono::steady_clock::now();
            exactRepairs = RepairScenarios(
                subproblems, master, values, solverConfig, start,
                exactRepairRequests, actualScenarioWorkers, nullptr);
            repairSeconds += Elapsed(repairBatchStart);
            for (size_t index = 0; index < exactRepairs.size(); ++index) {
                const ScenarioRepairResult& completed = exactRepairs.at(index);
                const size_t scenario = completed.scenario;
                const RepairResult& repair = completed.repair;
                exactRepairIndex.at(scenario) = static_cast<int>(index);
                repairProbes += repair.probes;
                if (!repair.feasibleDesign.empty()) {
                    repairFeasibleDesigns.at(scenario) = repair.feasibleDesign;
                }
                if (repair.acceptedResources > 0) ++strengthenedCuts;
                std::cout << "Repair iter=" << iteration
                          << ", scenario=" << scenario
                          << ", trigger=exact-unresolved"
                          << ", failures=" << completed.failures
                          << ", before=" << BatteryVector(master, scenario, values)
                          << ", after=" << BatteryVector(master, scenario,
                                repair.infeasibleAnchor)
                          << ", probes=" << repair.probes
                          << ", battery_probes=" << repair.batteryProbes
                          << ", facility_probes=" << repair.facilityProbes
                          << ", repair_ms=" << repair.seconds * 1000.0
                          << ", accepted_resources=" << repair.acceptedResources
                          << ", accepted_battery_steps="
                          << repair.acceptedBatterySteps
                          << ", accepted_battery_layers="
                          << repair.acceptedBatteryLayers
                          << ", accepted_facilities="
                          << repair.acceptedFacilities
                          << ", rsp_cert_checks="
                          << repair.rspCertificateChecks
                          << ", rsp_cert_hits="
                          << repair.rspCertificateHits
                          << ", rsp_certified_facilities="
                          << repair.rspCertifiedFacilities
                          << ", rsp_deferred_facilities="
                          << repair.rspDeferredFacilities
                          << '\n';
            }
        }
        for (size_t scenario = 0; scenario < subproblems.size(); ++scenario) {
            if (exactResults.at(scenario).status != solver::Status::Infeasible) continue;
            const int repairIndex = exactRepairIndex.at(scenario);
            const std::vector<double>& anchor = repairIndex >= 0
                ? exactRepairs.at(static_cast<size_t>(repairIndex)).repair.infeasibleAnchor
                : values;
            const Expr mismatch = Mismatch(master, values,
                subproblems.at(scenario).relevant);
            const Expr improvements = MonotoneImprovements(master, scenario, anchor,
                subproblems.at(scenario).relevant);
            model.AddConstr((repairEnhancedCuts ? improvements : mismatch) >= 1.0,
                ("p2_feas_" + std::to_string(iteration) + "_" +
                    std::to_string(scenario)).c_str());
            ++feasibilityCuts;
            infeasible = true;
        }
        if (infeasible && repairEnhancedCuts &&
            std::all_of(repairFeasibleDesigns.begin(), repairFeasibleDesigns.end(),
                [](const std::vector<double>& design) { return !design.empty(); })) {
            const RepairIncumbent incumbent = EvaluateCombinedRepair(
                subproblems, master, values, repairFeasibleDesigns,
                solverConfig, start, subproblemSeconds,
                actualScenarioWorkers);
            repairUpperBound = incumbent.objective;
            if (repairUpperBound + kTolerance < upperBound) {
                PrintCostBreakdown("repair-exact", master, incumbent.design,
                    subproblems);
                upperBound = repairUpperBound;
            }
            // A repaired incumbent is a different resource design.  It does not
            // break the infeasibility streak of the current Master candidates.
        }
        model.Update();
        if (!infeasible) {
            std::fill(unresolvedFailures.begin(), unresolvedFailures.end(), 0);
            const double candidateUpperBound = DesignCost(master, values) + recourse;
            if (candidateUpperBound + kTolerance < upperBound) {
                PrintCostBreakdown("master", master, values, subproblems);
                upperBound = candidateUpperBound;
            }
        }
        std::cout << "Phase 2 iter " << iteration;
        if (infeasible) std::cout << ", feasibility cut x" << feasibilityCuts;
        else std::cout << ", theta=" << thetaValue << ", Q(z)=" << recourse
                       << ", optimality cut x" << optimalityCuts;
        PrintBounds(upperBound, lowerBound);
        std::cout << ", master_ms=" << masterResult.seconds * 1000.0
                  << ", rsp_ms=" << rspSeconds * 1000.0
                  << ", subproblem_ms=" << subproblemSeconds * 1000.0
                  << ", repair_probes=" << repairProbes
                  << ", repair_ms=" << repairSeconds * 1000.0
                  << ", strengthened_cuts=" << strengthenedCuts
                  << ", repair_ub=";
        if (std::isfinite(repairUpperBound)) std::cout << repairUpperBound;
        else std::cout << "inf";
        std::cout
                  << ", elapsed_ms=" << Elapsed(start) * 1000.0 << '\n';

        if (hasConverged()) {
            std::cout << (repairEnhancedCuts ? "Full" : "Lifted Two-Phase")
                      << " converged.\n";
            return finalResult(solver::Status::Optimal);
        }
    }
    return finalResult(solver::Status::TimeLimit);
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
