#include "algorithm/route_relaxation.hpp"

#include "model/scenario_model.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

namespace wireless::algorithm {
namespace {

ScenarioData SelectRoute(const ScenarioData& source, size_t routeIndex)
{
    ScenarioData selected;
    selected.descriptor = source.descriptor;
    selected.schedule.stationsByTrip = source.schedule.stationsByTrip;
    selected.schedule.arrivalTimesByTrip = source.schedule.arrivalTimesByTrip;
    selected.schedule.departureTimesByTrip = source.schedule.departureTimesByTrip;
    selected.schedule.routeNames.push_back(source.schedule.routeNames.at(routeIndex));
    selected.network = source.network;
    selected.network.startDistanceByBus.clear();
    selected.network.endDistanceByBus.clear();

    for (size_t bus = 0; bus < source.schedule.busIds.size(); ++bus) {
        if (static_cast<size_t>(source.schedule.routeByBus.at(bus)) != routeIndex) continue;
        selected.schedule.busIds.push_back(source.schedule.busIds.at(bus));
        selected.schedule.tripIdsByBus.push_back(source.schedule.tripIdsByBus.at(bus));
        selected.schedule.routeByBus.push_back(0);
        selected.network.startDistanceByBus.push_back(source.network.startDistanceByBus.at(bus));
        selected.network.endDistanceByBus.push_back(source.network.endDistanceByBus.at(bus));
    }
    return selected;
}

std::set<model::DwcLink> RouteLinks(const ScenarioData& scenario)
{
    std::set<model::DwcLink> result;
    for (const auto& trips : scenario.schedule.tripIdsByBus) {
        for (int trip : trips) {
            const auto& stations = scenario.schedule.stationsByTrip.at(
                static_cast<size_t>(trip));
            for (size_t index = 0; index + 1 < stations.size(); ++index) {
                result.emplace(stations.at(index), stations.at(index + 1));
            }
        }
    }
    return result;
}

} // namespace

RouteRelaxation::RouteRelaxation(const InstanceData& instance, size_t scenarioIndex,
    size_t routeIndex, const std::vector<model::DwcLink>& candidates,
    const std::vector<BatteryLevelRef>& batteryLevels, size_t masterVariableCount,
    const SolverConfig& solverConfig)
    : scenarioIndex_(scenarioIndex), routeIndex_(routeIndex),
      masterVariableCount_(masterVariableCount), batteryLevels_(batteryLevels),
      environment_(std::make_unique<Envr>())
{
    model_ = std::make_unique<Model>(environment_->CreateModel(
        ("route_relax_sc" + std::to_string(scenarioIndex) + "_r" +
            std::to_string(routeIndex)).c_str()));
    const ScenarioData routeScenario = SelectRoute(instance.scenarios.at(scenarioIndex), routeIndex);
    const std::set<model::DwcLink> routeLinks = RouteLinks(routeScenario);

    model::FacilityVariables facilities;
    std::vector<Var> facilityVariables;
    for (size_t index = 0; index < candidates.size(); ++index) {
        const std::string name = model::DwcVariableName(candidates.at(index));
        const Var variable = model_->AddVar(0.0, 1.0, 0.0, COPT_CONTINUOUS, name.c_str());
        facilities.emplace(name, variable);
        facilityVariables.push_back(variable);
        if (routeLinks.count(candidates.at(index)) != 0) relevantFacilities_.push_back(index);
    }

    Expr objective(0.0);
    model::ScenarioVariables variables = model::BuildScenarioModel(*model_, instance,
        routeScenario, scenarioIndex, facilities, objective, {false, false});
    model_->Update();
    const VarArray allVariables = model_->GetVars();
    for (int index = 0; index < allVariables.Size(); ++index) {
        allVariables[index].SetType(COPT_CONTINUOUS);
    }

    for (size_t index = 0; index < facilityVariables.size(); ++index) {
        const Constraint link = model_->AddConstr(facilityVariables.at(index) == 0.0,
            ("fix_w_" + std::to_string(index)).c_str());
        links_.push_back({link, index});
    }
    const auto& routeBatteryVariables = variables.batteryChoicesByRoute.at(0);
    if (routeBatteryVariables.size() != batteryLevels_.size()) {
        throw std::runtime_error("Route relaxation battery level mismatch");
    }
    for (size_t level = 0; level < routeBatteryVariables.size(); ++level) {
        const Constraint link = model_->AddConstr(routeBatteryVariables.at(level) == 0.0,
            ("fix_b_" + std::to_string(level)).c_str());
        links_.push_back({link, batteryLevels_.at(level).masterIndex});
    }

    model_->SetObjective(objective, COPT_MINIMIZE);
    model_->Update();
    SolverConfig routeConfig = solverConfig;
    routeConfig.threads = 1;
    solver::Configure(*model_, routeConfig, false);
    model_->SetIntParam(COPT_INTPARAM_LPMETHOD, 1);
    model_->SetIntParam(COPT_INTPARAM_REQFARKASRAY, 1);
}

RouteRelaxationResult RouteRelaxation::Evaluate(
    const std::vector<double>& masterValues, double remainingSeconds)
{
    for (LinkConstraint& link : links_) {
        const double value = masterValues.at(link.masterIndex);
        link.constraint.Set(COPT_DBLINFO_LB, value);
        link.constraint.Set(COPT_DBLINFO_UB, value);
    }
    model_->SetDblParam(COPT_DBLPARAM_TIMELIMIT, std::max(0.001, remainingSeconds));
    model_->Update();
    const solver::Result solved = solver::Solve(*model_);

    RouteRelaxationResult result;
    result.status = solved.status;
    result.objective = solved.objective;
    result.seconds = solved.seconds;
    if (solved.status != solver::Status::Optimal) return result;

    result.coefficients.assign(masterVariableCount_, 0.0);
    for (LinkConstraint& link : links_) {
        result.coefficients.at(link.masterIndex) += link.constraint.Get(COPT_DBLINFO_DUAL);
    }
    result.constant = result.objective;
    for (size_t index = 0; index < result.coefficients.size(); ++index) {
        result.constant -= result.coefficients.at(index) * masterValues.at(index);
    }
    return result;
}

} // namespace wireless::algorithm
