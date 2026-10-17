#pragma once

#include "domain/wireless_data.hpp"

#include <string>
#include <utility>
#include <vector>

namespace wireless::model {

using DwcLink = std::pair<int, int>;

std::string DwcVariableName(DwcLink link);
std::vector<DwcLink> SelectDwcCandidates(const InstanceData& instance, int limit);
std::vector<int> CountBusesByRoute(const ScheduleData& schedule);
bool IsPlugInTerminalStop(const NetworkData& network, int station, size_t stationIndex);

} // namespace wireless::model
