#pragma once

#include "domain/wireless_data.hpp"

#include <string>
#include <utility>
#include <vector>

namespace wireless::model {

using DwcLink = std::pair<int, int>;

struct MaximumTripEnergyChain {
    size_t busIndex = 0;
    size_t tripCount = 0;
    double consumption = 0.0;
    double plugInOpportunity = 0.0;
    std::vector<double> dwcOpportunities;
};

std::string DwcVariableName(DwcLink link);
double DwcFacilityCost(const InstanceData& instance, DwcLink link);
std::vector<DwcLink> SelectDwcCandidates(const InstanceData& instance, int limit);
std::vector<int> CountBusesByRoute(const ScheduleData& schedule);
bool IsPlugInTerminalStop(const NetworkData& network, int station, size_t stationIndex);
std::vector<MaximumTripEnergyChain> BuildMaximumTripEnergyChains(
    const InstanceData& instance, const ScenarioData& scenario,
    const std::vector<DwcLink>& candidates);

} // namespace wireless::model
