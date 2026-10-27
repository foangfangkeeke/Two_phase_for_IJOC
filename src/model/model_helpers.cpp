#include "model/model_helpers.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace wireless::model {

std::string DwcVariableName(DwcLink link)
{
    return "w_link_l" + std::to_string(link.first) + "To" + std::to_string(link.second);
}

double DwcFacilityCost(const InstanceData& instance, DwcLink link)
{
    const double distance = instance.scenarios.front().network.linkDistances
        .at(link.first).at(link.second);
    return instance.chargers.unitCosts.at(2) * distance +
        60.0 * instance.chargers.inverterCostPerKw * instance.chargers.rates.at(2);
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

std::vector<MaximumTripEnergyChain> BuildMaximumTripEnergyChains(
    const InstanceData& instance, const ScenarioData& scenario,
    const std::vector<DwcLink>& candidates)
{
    const ScheduleData& schedule = scenario.schedule;
    const NetworkData& network = scenario.network;
    const OperationData& operation = instance.operation;
    const ChargerData& chargers = instance.chargers;

    std::map<DwcLink, size_t> candidateIndex;
    for (size_t index = 0; index < candidates.size(); ++index) {
        candidateIndex.emplace(candidates.at(index), index);
    }

    std::vector<size_t> representative(
        schedule.routeNames.size(), schedule.busIds.size());
    for (size_t bus = 0; bus < schedule.busIds.size(); ++bus) {
        const size_t route = static_cast<size_t>(schedule.routeByBus.at(bus));
        const size_t current = representative.at(route);
        if (current == schedule.busIds.size() ||
            schedule.tripIdsByBus.at(bus).size() >
                schedule.tripIdsByBus.at(current).size()) {
            representative.at(route) = bus;
        }
    }

    std::vector<MaximumTripEnergyChain> chains;
    chains.reserve(schedule.routeNames.size());
    for (size_t route = 0; route < schedule.routeNames.size(); ++route) {
        // The first bus with the largest trip count is the route's representative;
        // ties deliberately follow input order instead of comparing directions.
        const size_t bus = representative.at(route);
        if (bus == schedule.busIds.size()) {
            throw std::runtime_error("Route has no assigned bus: " +
                schedule.routeNames.at(route));
        }

        MaximumTripEnergyChain chain;
        chain.busIndex = bus;
        chain.tripCount = schedule.tripIdsByBus.at(bus).size();
        chain.dwcOpportunities.assign(candidates.size(), 0.0);
        double distance = network.startDistanceByBus.at(bus) +
            network.endDistanceByBus.at(bus);

        for (int trip : schedule.tripIdsByBus.at(bus)) {
            const size_t tripIndex = static_cast<size_t>(trip);
            const auto& stations = schedule.stationsByTrip.at(tripIndex);
            const auto& arrivals = schedule.arrivalTimesByTrip.at(tripIndex);
            const auto& departures = schedule.departureTimesByTrip.at(tripIndex);
            for (size_t stationIndex = 0; stationIndex < stations.size(); ++stationIndex) {
                const int station = stations.at(stationIndex);
                if (IsPlugInTerminalStop(network, station, stationIndex)) {
                    const int duration = departures.at(stationIndex) -
                        arrivals.at(stationIndex);
                    chain.plugInOpportunity += chargers.rates.at(0) * duration *
                        chargers.efficiencies.at(0);
                }
                if (stationIndex + 1 == stations.size()) continue;

                const int nextStation = stations.at(stationIndex + 1);
                distance += network.linkDistances.at(station).at(nextStation);
                const auto candidate = candidateIndex.find({station, nextStation});
                if (candidate != candidateIndex.end()) {
                    const int duration = arrivals.at(stationIndex + 1) -
                        departures.at(stationIndex);
                    chain.dwcOpportunities.at(candidate->second) +=
                        chargers.rates.at(2) * duration * chargers.efficiencies.at(2);
                }
            }
        }
        chain.consumption = distance * operation.energyPerKm;
        chains.push_back(std::move(chain));
    }
    return chains;
}

} // namespace wireless::model
