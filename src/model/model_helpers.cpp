#include "model/model_helpers.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace wireless::model {

std::string DwcVariableName(DwcLink link)
{
    return "w_link_l" + std::to_string(link.first) + "To" + std::to_string(link.second);
}

std::vector<DwcLink> SelectDwcCandidates(const InstanceData& instance, int limit)
{
    std::map<DwcLink, long long> scores;
    for (const ScenarioData& scenario : instance.scenarios) {
        const ScheduleData& schedule = scenario.schedule;
        for (size_t trip = 0; trip < schedule.stationsByTrip.size(); ++trip) {
            const auto& stations = schedule.stationsByTrip.at(trip);
            for (size_t index = 0; index + 1 < stations.size(); ++index) {
                const long long travelTime = schedule.arrivalTimesByTrip.at(trip).at(index + 1)
                    - schedule.departureTimesByTrip.at(trip).at(index);
                scores[{stations[index], stations[index + 1]}] += travelTime;
            }
        }
    }

    std::vector<std::pair<DwcLink, long long>> ranked(scores.begin(), scores.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        if (left.second != right.second) return left.second > right.second;
        return left.first < right.first;
    });
    if (limit <= 0 || static_cast<size_t>(limit) > ranked.size()) {
        throw std::invalid_argument("DWC Top-N must be in [1, " +
            std::to_string(ranked.size()) + "]");
    }

    std::vector<DwcLink> candidates;
    candidates.reserve(static_cast<size_t>(limit));
    for (int index = 0; index < limit; ++index) {
        candidates.push_back(ranked.at(static_cast<size_t>(index)).first);
    }
    return candidates;
}

std::vector<int> CountBusesByRoute(const ScheduleData& schedule)
{
    std::vector<int> counts(schedule.routeNames.size(), 0);
    for (int route : schedule.routeByBus) ++counts.at(static_cast<size_t>(route));
    return counts;
}

bool IsPlugInTerminalStop(const NetworkData& network, int station, size_t stationIndex)
{
    return stationIndex == 0 && network.plugInStations.count(station) != 0;
}

} // namespace wireless::model
