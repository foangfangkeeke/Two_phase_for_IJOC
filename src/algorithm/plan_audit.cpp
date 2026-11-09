#include "algorithm/plan_audit.hpp"

#include "model/model_helpers.hpp"
#include "model/scenario_model.hpp"
#include "solver/copt_solver.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wireless::algorithm {
namespace {

struct FixedPlan {
    std::string sourceLogSha256;
    double sourceUb = 0.0;
    std::set<std::string> facilities;
    std::map<std::pair<size_t, size_t>, int> batteries;
    std::string data;
    int scenarios = 0;
    int capacity = 0;
    int candidateCount = 0;
    int batteryStep = 0;
    int wireless = -1;
};

struct RouteTotals {
    int buses = 0;
    int battery = 0;
    double plugAvailableMinutes = 0.0;
    double dwcAvailableMinutes = 0.0;
    double plugGridKwh = 0.0;
    double dwcGridKwh = 0.0;
    double nightGridKwh = 0.0;
};

struct StationTotals {
    double availableBusMinutes = 0.0;
    double gridKwh = 0.0;
};

void RequireNoExtra(std::istringstream& input, size_t line)
{
    std::string extra;
    if (input >> extra) {
        throw std::runtime_error("Extra plan field on line " + std::to_string(line));
    }
}

void PrintFiniteJsonNumber(double value)
{
    if (std::isfinite(value)) std::cout << value;
    else std::cout << std::quoted(value > 0.0 ? "inf" : "-inf");
}

FixedPlan ReadPlan(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open fixed plan: " + path.string());
    FixedPlan plan;
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;
        if (line.empty() || line.front() == '#') continue;
        std::istringstream input(line);
        std::string key;
        input >> key;
        if (key == "source_log_sha256") input >> plan.sourceLogSha256;
        else if (key == "source_ub") input >> plan.sourceUb;
        else if (key == "data") input >> plan.data;
        else if (key == "scenarios") input >> plan.scenarios;
        else if (key == "capacity") input >> plan.capacity;
        else if (key == "candidate_count") input >> plan.candidateCount;
        else if (key == "battery_step") input >> plan.batteryStep;
        else if (key == "wireless") input >> plan.wireless;
        else if (key == "facility") {
            std::string name;
            input >> name;
            if (!plan.facilities.insert(name).second) {
                throw std::runtime_error("Duplicate facility in fixed plan: " + name);
            }
        } else if (key == "battery") {
            size_t scenario = 0;
            size_t route = 0;
            int capacity = 0;
            input >> scenario >> route >> capacity;
            if (!plan.batteries.emplace(std::make_pair(scenario, route), capacity).second) {
                throw std::runtime_error("Duplicate battery in fixed plan");
            }
        } else {
            throw std::runtime_error("Unknown fixed plan field on line " +
                std::to_string(lineNumber));
        }
        if (!input) throw std::runtime_error("Invalid fixed plan line " + std::to_string(lineNumber));
        RequireNoExtra(input, lineNumber);
    }
    if (plan.sourceLogSha256.size() != 64 || !std::isfinite(plan.sourceUb) ||
        plan.sourceUb <= 0.0 || plan.data.empty() || plan.scenarios <= 0 ||
        plan.capacity <= 0 || plan.candidateCount <= 0 || plan.batteryStep <= 0 ||
        (plan.wireless != 0 && plan.wireless != 1)) {
        throw std::runtime_error("Incomplete fixed plan header");
    }
    return plan;
}

void CheckPlan(const FixedPlan& plan, const InstanceData& instance,
    const RunConfig& config, const std::vector<model::DwcLink>& candidates)
{
    if (plan.data != config.dataName || plan.scenarios != config.scenarioCount ||
        plan.capacity != config.chargerCapacity ||
        plan.candidateCount != config.dwcTopN ||
        plan.batteryStep != config.batteryCapacityStep ||
        plan.wireless != static_cast<int>(config.allowWirelessCharging) ||
        instance.scenarios.size() != static_cast<size_t>(plan.scenarios)) {
        throw std::runtime_error("Fixed plan header does not match runtime options");
    }
    std::set<std::string> names;
    for (model::DwcLink link : candidates) names.insert(model::DwcVariableName(link));
    for (const std::string& facility : plan.facilities) {
        if (names.count(facility) == 0) {
            throw std::runtime_error("Fixed plan facility is not a candidate: " + facility);
        }
    }
    size_t expected = 0;
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        const size_t routeCount = instance.scenarios.at(scenario).schedule.routeNames.size();
        expected += routeCount;
        for (size_t route = 0; route < routeCount; ++route) {
            const int capacity = plan.batteries.at({scenario, route});
            const std::vector<int> levels = BatteryCapacityLevels(instance.operation,
                static_cast<int>(instance.operation.maximumBatteryCapacity));
            if (std::find(levels.begin(), levels.end(), capacity) == levels.end()) {
                throw std::runtime_error("Fixed plan has invalid battery capacity");
            }
        }
    }
    if (plan.batteries.size() != expected) {
        throw std::runtime_error("Fixed plan has unexpected battery records");
    }
}

void PrintScenarioDetails(const InstanceData& instance, size_t scenarioIndex,
    const FixedPlan& plan, model::ScenarioVariables& variables,
    const solver::Result& result, double& batteryCost, double& dayCost,
    double& nightCost)
{
    const ScenarioData& scenario = instance.scenarios.at(scenarioIndex);
    const ScheduleData& schedule = scenario.schedule;
    const double probability = scenario.descriptor.probability;
    std::vector<RouteTotals> routes(schedule.routeNames.size());
    std::map<int, StationTotals> stations;
    std::map<std::pair<size_t, int>, StationTotals> routeStations;
    for (int station : scenario.network.plugInStations) stations.emplace(station, StationTotals{});
    for (size_t busIndex = 0; busIndex < schedule.busIds.size(); ++busIndex) {
        const size_t route = static_cast<size_t>(schedule.routeByBus.at(busIndex));
        ++routes.at(route).buses;
        routes.at(route).nightGridKwh +=
            variables.overnightCharge.at(busIndex).Get(COPT_DBLINFO_VALUE);
    }
    for (size_t route = 0; route < routes.size(); ++route) {
        routes.at(route).battery = plan.batteries.at({scenarioIndex, route});
        batteryCost += probability * routes.at(route).buses *
            routes.at(route).battery * instance.operation.batteryUnitPrice;
        nightCost += probability * routes.at(route).nightGridKwh *
            instance.operation.nightEnergyPrice;
    }
    for (model::ScenarioVariables::ChargeEvent& event : variables.plugInEvents) {
        const size_t route = static_cast<size_t>(schedule.routeByBus.at(event.busIndex));
        const int arrival = schedule.arrivalTimesByTrip.at(event.trip).at(event.stationIndex);
        const int departure = schedule.departureTimesByTrip.at(event.trip).at(event.stationIndex);
        const double charge = event.charge.Get(COPT_DBLINFO_VALUE);
        routes.at(route).plugAvailableMinutes += departure - arrival;
        routes.at(route).plugGridKwh += charge;
        stations.at(event.station).availableBusMinutes += departure - arrival;
        stations.at(event.station).gridKwh += charge;
        routeStations[{route, event.station}].availableBusMinutes += departure - arrival;
        routeStations[{route, event.station}].gridKwh += charge;
        dayCost += probability * charge * instance.operation.dayEnergyPrice;
    }
    for (model::ScenarioVariables::ChargeEvent& event : variables.dwcEvents) {
        const size_t route = static_cast<size_t>(schedule.routeByBus.at(event.busIndex));
        const auto& tripStations = schedule.stationsByTrip.at(event.trip);
        const std::string facility = model::DwcVariableName(
            {event.station, tripStations.at(event.stationIndex + 1)});
        if (plan.facilities.count(facility) != 0) {
            const int departure = schedule.departureTimesByTrip.at(event.trip).at(event.stationIndex);
            const int arrival = schedule.arrivalTimesByTrip.at(event.trip).at(event.stationIndex + 1);
            routes.at(route).dwcAvailableMinutes += arrival - departure;
        }
        const double charge = event.charge.Get(COPT_DBLINFO_VALUE);
        routes.at(route).dwcGridKwh += charge;
        dayCost += probability * charge * instance.operation.dayEnergyPrice;
    }
    std::cout << "Plan audit scenario: {\"scenario\":" << scenarioIndex
              << ",\"folder\":" << std::quoted(scenario.descriptor.folder)
              << ",\"probability\":" << probability
              << ",\"status\":" << std::quoted(solver::ToString(result.status))
              << ",\"objective\":";
    PrintFiniteJsonNumber(result.objective);
    std::cout << ",\"bound\":";
    PrintFiniteJsonNumber(result.bound);
    std::cout << "}\n";
    for (size_t route = 0; route < routes.size(); ++route) {
        const RouteTotals& row = routes.at(route);
        std::cout << "Plan audit route: {\"scenario\":" << scenarioIndex
                  << ",\"route_index\":" << route
                  << ",\"route\":" << std::quoted(schedule.routeNames.at(route))
                  << ",\"buses\":" << row.buses
                  << ",\"battery_kwh\":" << row.battery
                  << ",\"plug_available_minutes\":" << row.plugAvailableMinutes
                  << ",\"dwc_available_minutes\":" << row.dwcAvailableMinutes
                  << ",\"plug_grid_kwh\":" << row.plugGridKwh
                  << ",\"dwc_grid_kwh\":" << row.dwcGridKwh
                  << ",\"night_grid_kwh\":" << row.nightGridKwh << "}\n";
    }
    for (const auto& [station, row] : stations) {
        std::cout << "Plan audit station: {\"scenario\":" << scenarioIndex
                  << ",\"station\":" << station
                  << ",\"plugs\":" << scenario.network.plugCountByStation.at(station)
                  << ",\"available_bus_minutes\":" << row.availableBusMinutes
                  << ",\"plug_grid_kwh\":" << row.gridKwh
                  << ",\"equivalent_plug_minutes\":" <<
                      row.gridKwh / instance.chargers.rates.at(0)
                  << ",\"plug_battery_kwh\":" <<
                      row.gridKwh * instance.chargers.efficiencies.at(0) << "}\n";
    }
    for (const auto& [key, row] : routeStations) {
        std::cout << "Plan audit route_station: {\"scenario\":" << scenarioIndex
                  << ",\"route_index\":" << key.first
                  << ",\"station\":" << key.second
                  << ",\"available_bus_minutes\":" << row.availableBusMinutes
                  << ",\"plug_grid_kwh\":" << row.gridKwh
                  << ",\"equivalent_plug_minutes\":" <<
                      row.gridKwh / instance.chargers.rates.at(0) << "}\n";
    }
}

} // namespace

void AuditFixedPlan(const InstanceData& instance, const RunConfig& config,
    const SolverConfig& solverConfig)
{
    const FixedPlan plan = ReadPlan(config.analysisPlan);
    const std::vector<model::DwcLink> candidates = config.allowWirelessCharging
        ? model::SelectDwcCandidates(instance, config.dwcTopN)
        : std::vector<model::DwcLink>();
    CheckPlan(plan, instance, config, candidates);
    double facilityCost = 0.0;
    for (model::DwcLink link : candidates) {
        const std::string name = model::DwcVariableName(link);
        if (plan.facilities.count(name) != 0) {
            facilityCost += model::DwcFacilityCost(instance, link);
            std::cout << "Plan audit facility: {\"name\":" << std::quoted(name)
                      << ",\"from\":" << link.first
                      << ",\"to\":" << link.second << "}\n";
        }
    }
    std::cout << "Plan audit source: {\"sha256\":" <<
        std::quoted(plan.sourceLogSha256) << ",\"source_ub\":" <<
        plan.sourceUb << ",\"selected_facilities\":" <<
        plan.facilities.size() << ",\"battery_unit_price\":" <<
        instance.operation.batteryUnitPrice << ",\"dwc_unit_cost_per_km\":" <<
        instance.chargers.unitCosts.at(2) << ",\"inverter_cost_per_kw\":" <<
        instance.chargers.inverterCostPerKw << ",\"plug_rate_kwh_per_minute\":" <<
        instance.chargers.rates.at(0) << ",\"plug_efficiency\":" <<
        instance.chargers.efficiencies.at(0) << "}\n";
    double batteryCost = 0.0;
    double dayCost = 0.0;
    double nightCost = 0.0;
    bool allSolved = true;
    bool allOptimal = true;
    for (size_t scenarioIndex = 0; scenarioIndex < instance.scenarios.size(); ++scenarioIndex) {
        Envr environment = solver::CreateEnvironment();
        Model model = environment.CreateModel(
            ("plan_audit_scenario_" + std::to_string(scenarioIndex)).c_str());
        model::FacilityVariables facilities;
        for (model::DwcLink link : candidates) {
            const std::string name = model::DwcVariableName(link);
            const double selected = plan.facilities.count(name) != 0 ? 1.0 : 0.0;
            facilities.emplace(name, model.AddVar(selected, selected, 0.0,
                COPT_CONTINUOUS, name.c_str()));
        }
        Expr objective(0.0);
        model::ScenarioModelOptions options;
        options.recordChargeEvents = true;
        model::ScenarioVariables variables = model::BuildScenarioModel(model,
            instance, instance.scenarios.at(scenarioIndex), scenarioIndex,
            facilities, objective, options);
        for (size_t route = 0; route < variables.batteryChoicesByRoute.size(); ++route) {
            const int selected = plan.batteries.at({scenarioIndex, route});
            const std::vector<int> levels = BatteryCapacityLevels(instance.operation,
                static_cast<int>(instance.operation.maximumBatteryCapacity));
            for (size_t level = 0; level < levels.size(); ++level) {
                const double value = levels.at(level) == selected ? 1.0 : 0.0;
                Var& variable = variables.batteryChoicesByRoute.at(route).at(level);
                variable.Set(COPT_DBLINFO_LB, value);
                variable.Set(COPT_DBLINFO_UB, value);
            }
        }
        model.SetObjective(objective, COPT_MINIMIZE);
        model.Update();
        solver::Configure(model, solverConfig, false);
        const solver::Result result = solver::Solve(model);
        allOptimal = allOptimal && result.status == solver::Status::Optimal;
        if (!result.hasSolution) {
            allSolved = false;
            std::cout << "Plan audit scenario: {\"scenario\":" << scenarioIndex
                      << ",\"status\":" << std::quoted(solver::ToString(result.status))
                      << ",\"has_solution\":false}\n";
            continue;
        }
        PrintScenarioDetails(instance, scenarioIndex, plan, variables, result,
            batteryCost, dayCost, nightCost);
    }
    if (!allSolved) {
        std::cout << "Plan audit status: INCOMPLETE\n";
        return;
    }
    const double total = facilityCost + batteryCost + dayCost + nightCost;
    std::cout << "Plan audit total: {\"facility_cost\":" << facilityCost
              << ",\"battery_cost\":" << batteryCost
              << ",\"day_energy_cost\":" << dayCost
              << ",\"night_energy_cost\":" << nightCost
              << ",\"total\":" << total
              << ",\"source_ub\":" << plan.sourceUb
              << ",\"difference\":" << total - plan.sourceUb << "}\n";
    std::cout << "Plan audit status: " <<
        (allOptimal ? "COMPLETE_OPTIMAL_RECOURSE" : "COMPLETE_FEASIBLE_RECOURSE") << '\n';
}

} // namespace wireless::algorithm
