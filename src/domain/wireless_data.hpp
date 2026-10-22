#pragma once

#include "config/run_config.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wireless {

using LinkDistances = std::unordered_map<int, std::unordered_map<int, double>>;

struct ScenarioDescriptor { std::string folder; double probability = 1.0; };

struct OperationData {
    double dayEnergyPrice = 0.0;
    double nightEnergyPrice = 0.0;
    double batteryUnitPrice = 0.0;
    double maximumSoc = 0.0;
    double minimumSoc = 0.0;
    double minimumBatteryCapacity = 0.0;
    double maximumBatteryCapacity = 0.0;
    int batteryCapacityStep = 20;
    double energyPerKm = 0.0;
    int minimumTime = 0;
    int maximumTime = 0;
};

struct ChargerData {
    std::vector<double> rates;
    std::vector<double> efficiencies;
    std::vector<double> unitCosts;
    double inverterCostPerKw = 0.0;
};

struct ScheduleData {
    std::vector<int> busIds;
    std::vector<std::vector<int>> tripIdsByBus;
    std::vector<std::vector<int>> stationsByTrip;
    std::vector<std::vector<int>> arrivalTimesByTrip;
    std::vector<std::vector<int>> departureTimesByTrip;
    std::vector<std::string> routeNames;
    std::vector<int> routeByBus;
};

struct NetworkData {
    std::unordered_set<int> stations;
    std::unordered_set<int> plugInStations;
    LinkDistances linkDistances;
    std::vector<double> startDistanceByBus;
    std::vector<double> endDistanceByBus;
    std::unordered_map<int, int> plugCountByStation;
};

struct ScenarioData {
    ScenarioDescriptor descriptor;
    ScheduleData schedule;
    NetworkData network;
};

struct InstanceData {
    std::string name;
    std::filesystem::path root;
    OperationData operation;
    ChargerData chargers;
    std::vector<ScenarioData> scenarios;
};

InstanceData LoadInstance(const RunConfig& config);
std::vector<int> BatteryCapacityLevels(const OperationData& operation,
    int maximumCapacity);

} // namespace wireless
