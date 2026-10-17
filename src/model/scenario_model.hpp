#pragma once

#include "domain/wireless_data.hpp"
#include "solver/copt_solver.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace wireless::model {

using FacilityVariables = std::unordered_map<std::string, Var>;

struct ScenarioVariables {
    std::vector<std::vector<Var>> batteryChoicesByRoute;
};

struct ScenarioModelOptions {
    bool includeBatteryCost = true;
    bool enforceSharedChargerCapacity = true;
};

ScenarioVariables BuildScenarioModel(Model& model, const InstanceData& instance,
    const ScenarioData& scenario, size_t scenarioIndex,
    const FacilityVariables& facilityVariables, Expr& objective,
    const ScenarioModelOptions& options = {});

} // namespace wireless::model
