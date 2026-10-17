#include "model/scenario_model.hpp"

#include "model/model_helpers.hpp"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wireless::model {
namespace {

constexpr int kBatteryCapacityStep = 20;

using VariableGrid = std::vector<std::vector<std::vector<Var>>>;
using OptionalVariableGrid = std::vector<std::vector<std::vector<std::optional<Var>>>>;

std::string StopSuffix(int bus, int trip, int station, size_t stationIndex)
{
    return "_b" + std::to_string(bus) + "_n" + std::to_string(trip) +
        "_s" + std::to_string(station) + "_i" + std::to_string(stationIndex);
}

ScenarioVariables BuildScenarioInternal(Model& model, const InstanceData& instance,
    const ScenarioData& scenario, size_t scenarioIndex,
    const FacilityVariables& facilityVariables, Expr& objective)
{
    const OperationData& operation = instance.operation;
    const ChargerData& chargers = instance.chargers;
    const ScheduleData& schedule = scenario.schedule;
    const NetworkData& network = scenario.network;
    const std::string prefix = "sc" + std::to_string(scenarioIndex) + "_";

    std::vector<Expr> routeCapacities;
    routeCapacities.reserve(schedule.routeNames.size());
    ScenarioVariables variables;
    variables.batteryChoicesByRoute.resize(schedule.routeNames.size());
    Expr scenarioObjective(0.0);
    const std::vector<int> busesByRoute = CountBusesByRoute(schedule);
    for (size_t route = 0; route < schedule.routeNames.size(); ++route) {
        routeCapacities.emplace_back(0.0);
        Expr& selectedCapacity = routeCapacities.back();
        Expr selectOne(0.0);
        for (int capacity = static_cast<int>(operation.minimumBatteryCapacity);
             capacity <= static_cast<int>(operation.maximumBatteryCapacity);
             capacity += kBatteryCapacityStep) {
            const std::string name = prefix + "E_r" + std::to_string(route) +
                "_cap" + std::to_string(capacity);
            const Var choice = model.AddVar(0.0, 1.0, 0.0, COPT_BINARY, name.c_str());
            variables.batteryChoicesByRoute.at(route).push_back(choice);
            selectOne += choice;
            selectedCapacity += capacity * choice;
        }
        const std::string name = prefix + "selectBatteryCapacity_r" + std::to_string(route);
        model.AddConstr(selectOne == 1.0, name.c_str());
        scenarioObjective += busesByRoute.at(route) * operation.batteryUnitPrice * selectedCapacity;
    }

    std::vector<Var> nightCharge;
    VariableGrid stopCharge;
    OptionalVariableGrid linkCharge;
    VariableGrid departureEnergy;
    VariableGrid arrivalEnergy;
    nightCharge.reserve(schedule.busIds.size());
    stopCharge.reserve(schedule.busIds.size());
    linkCharge.reserve(schedule.busIds.size());
    departureEnergy.reserve(schedule.busIds.size());
    arrivalEnergy.reserve(schedule.busIds.size());

    for (size_t busIndex = 0; busIndex < schedule.busIds.size(); ++busIndex) {
        const int bus = schedule.busIds.at(busIndex);
        const std::string nightName = prefix + "c_Night_b" + std::to_string(bus);
        nightCharge.push_back(model.AddVar(
            0.0, COPT_INFINITY, 0.0, COPT_CONTINUOUS, nightName.c_str()));

        std::vector<std::vector<Var>> busStopCharge;
        std::vector<std::vector<std::optional<Var>>> busLinkCharge;
        std::vector<std::vector<Var>> busDepartureEnergy;
        std::vector<std::vector<Var>> busArrivalEnergy;
        const auto& trips = schedule.tripIdsByBus.at(busIndex);
        for (int trip : trips) {
            const auto& stations = schedule.stationsByTrip.at(static_cast<size_t>(trip));
            std::vector<Var> tripStopCharge;
            std::vector<std::optional<Var>> tripLinkCharge;
            std::vector<Var> tripDepartureEnergy;
            std::vector<Var> tripArrivalEnergy;
            for (size_t stationIndex = 0; stationIndex < stations.size(); ++stationIndex) {
                const int station = stations.at(stationIndex);
                const std::string suffix = StopSuffix(bus, trip, station, stationIndex);
                tripDepartureEnergy.push_back(model.AddVar(0.0, COPT_INFINITY, 0.0,
                    COPT_CONTINUOUS, (prefix + "e_Dep" + suffix).c_str()));
                tripArrivalEnergy.push_back(model.AddVar(0.0, COPT_INFINITY, 0.0,
                    COPT_CONTINUOUS, (prefix + "e_Arr" + suffix).c_str()));
                const double stopUpperBound = IsPlugInTerminalStop(network, station, stationIndex)
                    ? COPT_INFINITY : 0.0;
                tripStopCharge.push_back(model.AddVar(0.0, stopUpperBound, 0.0,
                    COPT_CONTINUOUS, (prefix + "c" + suffix).c_str()));

                if (stationIndex + 1 < stations.size()) {
                    const std::string facility = DwcVariableName(
                        {station, stations.at(stationIndex + 1)});
                    if (facilityVariables.count(facility) != 0) {
                        tripLinkCharge.emplace_back(model.AddVar(0.0, COPT_INFINITY, 0.0,
                            COPT_CONTINUOUS, (prefix + "c_link" + suffix).c_str()));
                    } else {
                        tripLinkCharge.emplace_back(std::nullopt);
                    }
                } else {
                    tripLinkCharge.emplace_back(std::nullopt);
                }
            }
            busStopCharge.push_back(std::move(tripStopCharge));
            busLinkCharge.push_back(std::move(tripLinkCharge));
            busDepartureEnergy.push_back(std::move(tripDepartureEnergy));
            busArrivalEnergy.push_back(std::move(tripArrivalEnergy));
        }
        stopCharge.push_back(std::move(busStopCharge));
        linkCharge.push_back(std::move(busLinkCharge));
        departureEnergy.push_back(std::move(busDepartureEnergy));
        arrivalEnergy.push_back(std::move(busArrivalEnergy));
    }
    model.Update();

    Expr dayCharge(0.0);
    Expr overnightCharge(0.0);
    for (size_t bus = 0; bus < schedule.busIds.size(); ++bus) {
        overnightCharge += nightCharge.at(bus);
        for (size_t trip = 0; trip < schedule.tripIdsByBus.at(bus).size(); ++trip) {
            for (const Var& charge : stopCharge.at(bus).at(trip)) dayCharge += charge;
            for (const auto& charge : linkCharge.at(bus).at(trip)) {
                if (charge.has_value()) dayCharge += *charge;
            }
        }
    }
    scenarioObjective += operation.dayEnergyPrice * dayCharge +
        operation.nightEnergyPrice * overnightCharge;

    for (size_t bus = 0; bus < schedule.busIds.size(); ++bus) {
        const size_t route = static_cast<size_t>(schedule.routeByBus.at(bus));
        const Expr& capacity = routeCapacities.at(route);
        const auto& trips = schedule.tripIdsByBus.at(bus);
        for (size_t tripIndex = 0; tripIndex < trips.size(); ++tripIndex) {
            const auto& stations = schedule.stationsByTrip.at(static_cast<size_t>(trips.at(tripIndex)));
            for (size_t stationIndex = 0; stationIndex < stations.size(); ++stationIndex) {
                const Var arrival = arrivalEnergy.at(bus).at(tripIndex).at(stationIndex);
                const Var departure = departureEnergy.at(bus).at(tripIndex).at(stationIndex);
                model.AddConstr(arrival >= operation.minimumSoc * capacity);
                model.AddConstr(arrival <= operation.maximumSoc * capacity);
                model.AddConstr(departure >= operation.minimumSoc * capacity);
                model.AddConstr(departure <= operation.maximumSoc * capacity);
            }
        }
    }

    using OccupancyByTime = std::unordered_map<int, Var>;
    using OccupancyByStation = std::unordered_map<int, OccupancyByTime>;
    using OccupancyByTrip = std::unordered_map<int, OccupancyByStation>;
    using OccupancyByBus = std::unordered_map<int, OccupancyByTrip>;
    OccupancyByBus occupancyVariables;
    std::unordered_map<int, std::vector<Expr>> stationOccupancy;
    const size_t timeSpan = static_cast<size_t>(operation.maximumTime - operation.minimumTime);
    for (int station : network.plugInStations) {
        auto& values = stationOccupancy[station];
        values.reserve(timeSpan);
        for (size_t time = 0; time < timeSpan; ++time) values.emplace_back(0.0);
    }

    for (size_t busIndex = 0; busIndex < schedule.busIds.size(); ++busIndex) {
        const int bus = schedule.busIds.at(busIndex);
        for (int trip : schedule.tripIdsByBus.at(busIndex)) {
            const auto& stations = schedule.stationsByTrip.at(static_cast<size_t>(trip));
            for (size_t stationIndex = 0; stationIndex < stations.size(); ++stationIndex) {
                const int station = stations.at(stationIndex);
                if (!IsPlugInTerminalStop(network, station, stationIndex)) continue;
                const int arrival = schedule.arrivalTimesByTrip.at(static_cast<size_t>(trip)).at(stationIndex);
                const int departure = schedule.departureTimesByTrip.at(static_cast<size_t>(trip)).at(stationIndex);
                for (int time = arrival; time < departure; ++time) {
                    const std::string name = prefix + "x_b" + std::to_string(bus) +
                        "_n" + std::to_string(trip) + "_s" + std::to_string(station) +
                        "_i" + std::to_string(stationIndex) + "_t" + std::to_string(time);
                    const Var occupancy = model.AddVar(
                        0.0, 1.0, 0.0, COPT_BINARY, name.c_str());
                    occupancyVariables[bus][trip][station].emplace(time, occupancy);
                    stationOccupancy.at(station).at(
                        static_cast<size_t>(time - operation.minimumTime)) += occupancy;
                }
            }
        }
    }

    for (int station : network.plugInStations) {
        const int plugCount = network.plugCountByStation.at(station);
        for (int time = operation.minimumTime; time < operation.maximumTime; ++time) {
            Expr& occupancy = stationOccupancy.at(station).at(
                static_cast<size_t>(time - operation.minimumTime));
            if (occupancy.Size() != 0) {
                const std::string name = prefix + "constChargerNum_s" +
                    std::to_string(station) + "_t" + std::to_string(time);
                model.AddConstr(occupancy <= plugCount, name.c_str());
            }
        }
    }

    for (size_t busIndex = 0; busIndex < schedule.busIds.size(); ++busIndex) {
        const int bus = schedule.busIds.at(busIndex);
        const auto& trips = schedule.tripIdsByBus.at(busIndex);
        for (size_t tripIndex = 0; tripIndex < trips.size(); ++tripIndex) {
            const int trip = trips.at(tripIndex);
            const auto& stations = schedule.stationsByTrip.at(static_cast<size_t>(trip));
            for (size_t stationIndex = 0; stationIndex < stations.size(); ++stationIndex) {
                const int station = stations.at(stationIndex);
                if (!IsPlugInTerminalStop(network, station, stationIndex)) continue;
                Expr occupiedTime(0.0);
                const int arrival = schedule.arrivalTimesByTrip.at(static_cast<size_t>(trip)).at(stationIndex);
                const int departure = schedule.departureTimesByTrip.at(static_cast<size_t>(trip)).at(stationIndex);
                for (int time = arrival; time < departure; ++time) {
                    occupiedTime += occupancyVariables.at(bus).at(trip).at(station).at(time);
                }
                model.AddConstr(stopCharge.at(busIndex).at(tripIndex).at(stationIndex)
                    <= chargers.rates.at(0) * occupiedTime);
            }
        }
    }

    for (size_t busIndex = 0; busIndex < schedule.busIds.size(); ++busIndex) {
        const int bus = schedule.busIds.at(busIndex);
        const Expr& capacity = routeCapacities.at(
            static_cast<size_t>(schedule.routeByBus.at(busIndex)));
        const auto& trips = schedule.tripIdsByBus.at(busIndex);
        const double startConsumption = network.startDistanceByBus.at(busIndex) * operation.energyPerKm;
        model.AddConstr(arrivalEnergy.at(busIndex).front().front()
            == operation.maximumSoc * capacity - startConsumption);

        const int lastTrip = trips.back();
        const size_t lastStation = schedule.stationsByTrip.at(static_cast<size_t>(lastTrip)).size() - 1;
        const double endConsumption = network.endDistanceByBus.at(busIndex) * operation.energyPerKm;
        const Var finalDeparture = departureEnergy.at(busIndex).back().at(lastStation);
        model.AddConstr(finalDeparture >= operation.minimumSoc * capacity + endConsumption);

        for (size_t tripIndex = 0; tripIndex < trips.size(); ++tripIndex) {
            const int trip = trips.at(tripIndex);
            const auto& stations = schedule.stationsByTrip.at(static_cast<size_t>(trip));
            for (size_t stationIndex = 0; stationIndex < stations.size(); ++stationIndex) {
                const int station = stations.at(stationIndex);
                const Var arrival = arrivalEnergy.at(busIndex).at(tripIndex).at(stationIndex);
                const Var departure = departureEnergy.at(busIndex).at(tripIndex).at(stationIndex);
                const Var stop = stopCharge.at(busIndex).at(tripIndex).at(stationIndex);
                if (network.plugInStations.count(station) != 0) {
                    model.AddConstr(departure == arrival + stop * chargers.efficiencies.at(0));
                } else {
                    model.AddConstr(departure == arrival);
                }

                size_t nextTripIndex = tripIndex;
                size_t nextStationIndex = stationIndex + 1;
                bool transfersTrip = false;
                if (nextStationIndex == stations.size()) {
                    if (tripIndex + 1 == trips.size()) continue;
                    nextTripIndex = tripIndex + 1;
                    nextStationIndex = 0;
                    transfersTrip = true;
                }
                const int nextTrip = trips.at(nextTripIndex);
                const int nextStation = schedule.stationsByTrip.at(
                    static_cast<size_t>(nextTrip)).at(nextStationIndex);
                const Var nextArrival = arrivalEnergy.at(busIndex).at(nextTripIndex).at(nextStationIndex);
                const double distance = transfersTrip ? 0.0 :
                    network.linkDistances.at(station).at(nextStation);
                const double consumption = distance * operation.energyPerKm;
                const auto& wirelessCharge = linkCharge.at(busIndex).at(tripIndex).at(stationIndex);
                if (wirelessCharge.has_value()) {
                    const std::string facilityName = DwcVariableName({station, nextStation});
                    const int departureTime = schedule.departureTimesByTrip.at(
                        static_cast<size_t>(trip)).at(stationIndex);
                    const int arrivalTime = schedule.arrivalTimesByTrip.at(
                        static_cast<size_t>(nextTrip)).at(nextStationIndex);
                    const double upperBound = chargers.rates.at(2) * (arrivalTime - departureTime);
                    model.AddConstr(*wirelessCharge <= upperBound * facilityVariables.at(facilityName));
                    model.AddConstr(nextArrival - departure -
                        *wirelessCharge * chargers.efficiencies.at(2) == -consumption);
                } else {
                    model.AddConstr(nextArrival - departure == -consumption);
                }
            }
        }

        const std::string name = prefix + "e_Overnight_b" + std::to_string(bus);
        model.AddConstr(nightCharge.at(busIndex) * chargers.efficiencies.at(0) +
            finalDeparture == operation.maximumSoc * capacity + endConsumption, name.c_str());
    }

    objective += scenario.descriptor.probability * scenarioObjective;
    return variables;
}

} // namespace

ScenarioVariables BuildScenarioModel(Model& model, const InstanceData& instance,
    const ScenarioData& scenario, size_t scenarioIndex,
    const FacilityVariables& facilityVariables, Expr& objective)
{
    return BuildScenarioInternal(model, instance, scenario, scenarioIndex,
        facilityVariables, objective);
}

} // namespace wireless::model
