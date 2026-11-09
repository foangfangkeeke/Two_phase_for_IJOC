#pragma once

#include "domain/wireless_data.hpp"
#include "solver/copt_solver.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace wireless::model {

using FacilityVariables = std::unordered_map<std::string, Var>;

struct ScenarioVariables {
    struct ChargeEvent {
        size_t busIndex = 0;
        int trip = 0;
        size_t stationIndex = 0;
        int station = 0;
        Var charge;
    };
    std::vector<std::vector<Var>> batteryChoicesByRoute;
    std::vector<Var> daytimeCharge;
    std::vector<Var> overnightCharge;
    std::vector<ChargeEvent> plugInEvents;
    std::vector<ChargeEvent> dwcEvents;
};

struct ScenarioModelOptions {
    bool includeBatteryCost = true;
    bool enforceSharedChargerCapacity = true;
    std::vector<int> maximumBatteryCapacities;
    bool recordChargeEvents = false;
};

ScenarioVariables BuildScenarioModel(Model& model, const InstanceData& instance,
    const ScenarioData& scenario, size_t scenarioIndex,
    const FacilityVariables& facilityVariables, Expr& objective,
    const ScenarioModelOptions& options = {});

} // namespace wireless::model
