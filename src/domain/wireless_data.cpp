#include "domain/wireless_data.hpp"

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace wireless {
namespace {

using StringTable = std::vector<std::vector<std::string>>;
using IntTable = std::vector<std::vector<int>>;

std::string Trim(const std::string& value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

std::vector<std::string> Split(const std::string& line)
{
    std::vector<std::string> fields;
    size_t begin = 0;
    while (begin <= line.size()) {
        const size_t comma = line.find(',', begin);
        fields.push_back(Trim(line.substr(begin, comma - begin)));
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return fields;
}

StringTable ReadStringTable(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open data file: " + path.string());
    StringTable rows;
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (!line.empty() && line.front() != '#') rows.push_back(Split(line));
    }
    if (rows.empty()) throw std::runtime_error("Empty data file: " + path.string());
    return rows;
}

IntTable ReadIntTable(const std::filesystem::path& path)
{
    IntTable table;
    for (const auto& row : ReadStringTable(path)) {
        std::vector<int> values;
        values.reserve(row.size());
        for (const std::string& field : row) values.push_back(std::stoi(field));
        table.push_back(std::move(values));
    }
    return table;
}

LinkDistances ReadLinkDistances(const std::filesystem::path& path)
{
    LinkDistances distances;
    for (const auto& row : ReadStringTable(path)) {
        distances[std::stoi(row.at(0))][std::stoi(row.at(1))] = std::stod(row.at(2));
    }
    return distances;
}

std::filesystem::path DataRoot(const std::string& dataName)
{
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        / "data" / (dataName + "_data");
}

std::vector<ScenarioDescriptor> ReadScenarioDescriptors(
    const std::filesystem::path& root, const RunConfig& config)
{
    const StringTable rows = ReadStringTable(root / "scenarioCfg.txt");
    const int available = std::stoi(rows.at(0).at(0));
    if (config.scenarioStart + config.scenarioCount > available) {
        throw std::invalid_argument("Requested scenario range exceeds scenarioCfg entries");
    }
    std::vector<ScenarioDescriptor> descriptors;
    descriptors.reserve(static_cast<size_t>(config.scenarioCount));
    for (int index = 0; index < config.scenarioCount; ++index) {
        const auto& row = rows.at(static_cast<size_t>(config.scenarioStart + index + 1));
        descriptors.push_back({row.at(0), 1.0 / config.scenarioCount});
    }
    return descriptors;
}

void ReadGlobalData(InstanceData& instance, const RunConfig& config,
    std::unordered_map<int, int>& globalPlugCounts)
{
    const StringTable rows = ReadStringTable(instance.root / "globalCfg.txt");
    const auto& costs = rows.at(0);
    const auto& times = rows.at(1);
    const auto& bus = rows.at(2);
    instance.operation.dayEnergyPrice = std::stod(costs.at(0));
    instance.operation.nightEnergyPrice = std::stod(costs.at(1));
    instance.operation.batteryUnitPrice = std::stod(costs.at(2));
    instance.operation.maximumSoc = std::stod(costs.at(3));
    instance.operation.minimumSoc = std::stod(costs.at(4));
    instance.operation.minimumBatteryCapacity = std::stod(costs.at(5));
    instance.operation.maximumBatteryCapacity = std::stod(costs.at(6));
    instance.operation.minimumTime = std::stoi(times.at(0));
    instance.operation.maximumTime = std::stoi(times.at(1));
    instance.operation.energyPerKm = std::stod(bus.at(0)) * config.energyScale;

    const int chargerTypes = std::stoi(rows.at(3).at(0));
    for (int index = 0; index < chargerTypes; ++index) {
        const size_t offset = static_cast<size_t>(index);
        instance.chargers.rates.push_back(std::stod(rows.at(4).at(offset)));
        instance.chargers.efficiencies.push_back(std::stod(rows.at(5).at(offset)));
        instance.chargers.unitCosts.push_back(
            std::stod(rows.at(6).at(offset)) * config.facilityCostScale);
    }
    instance.chargers.inverterCostPerKw =
        std::stod(rows.at(6).at(static_cast<size_t>(chargerTypes))) * config.facilityCostScale;

    const int plugInStations = std::stoi(rows.at(7).at(0));
    for (int index = 0; index < plugInStations; ++index) {
        const int station = std::stoi(rows.at(8 + static_cast<size_t>(index)).at(0));
        globalPlugCounts[station] = config.chargerCapacity;
    }
}

ScenarioData ReadScenario(const InstanceData& instance, ScenarioDescriptor descriptor,
    const RunConfig& config, const std::unordered_map<int, int>& globalPlugCounts)
{
    ScenarioData scenario;
    scenario.descriptor = std::move(descriptor);
    scenario.network.linkDistances = ReadLinkDistances(instance.root / "linkDistance.txt");
    scenario.network.plugCountByStation = globalPlugCounts;
    for (const auto& entry : globalPlugCounts) scenario.network.plugInStations.insert(entry.first);

    const std::filesystem::path folder = instance.root / scenario.descriptor.folder;
    scenario.schedule.arrivalTimesByTrip = ReadIntTable(folder / "tripArrTimes.txt");
    scenario.schedule.departureTimesByTrip = ReadIntTable(folder / "tripDepTimes.txt");
    scenario.schedule.stationsByTrip = ReadIntTable(folder / "tripStations.txt");
    for (const auto& trip : scenario.schedule.stationsByTrip) {
        scenario.network.stations.insert(trip.begin(), trip.end());
    }

    const StringTable rows = ReadStringTable(folder / "scenarioMiscCfg.txt");
    const auto& startDistances = rows.at(0);
    const auto& endDistances = rows.at(1);
    const int busCount = std::stoi(rows.at(2).at(0));
    const auto& busIds = rows.at(3);
    const auto& tripCounts = rows.at(4);
    for (int busIndex = 0; busIndex < busCount; ++busIndex) {
        const size_t offset = static_cast<size_t>(busIndex);
        scenario.network.startDistanceByBus.push_back(std::stod(startDistances.at(offset)));
        scenario.network.endDistanceByBus.push_back(std::stod(endDistances.at(offset)));
        scenario.schedule.busIds.push_back(std::stoi(busIds.at(offset)));
        const int tripCount = std::stoi(tripCounts.at(offset));
        std::vector<int> tripIds;
        tripIds.reserve(static_cast<size_t>(tripCount));
        for (int tripIndex = 0; tripIndex < tripCount; ++tripIndex) {
            tripIds.push_back(std::stoi(rows.at(5 + offset).at(static_cast<size_t>(tripIndex))));
        }
        scenario.schedule.tripIdsByBus.push_back(std::move(tripIds));
    }

    const size_t chargerOffset = 5 + static_cast<size_t>(busCount);
    const int plugInStations = std::stoi(rows.at(chargerOffset).at(0));
    for (int index = 0; index < plugInStations; ++index) {
        const int station = std::stoi(rows.at(chargerOffset + 1 + static_cast<size_t>(index)).at(0));
        scenario.network.plugCountByStation[station] = config.chargerCapacity;
        scenario.network.plugInStations.insert(station);
    }

    const size_t routeOffset = chargerOffset + 1 + static_cast<size_t>(plugInStations);
    const int routeCount = std::stoi(rows.at(routeOffset).at(0));
    scenario.schedule.routeNames = rows.at(routeOffset + 1);
    if (scenario.schedule.routeNames.size() != static_cast<size_t>(routeCount)) {
        throw std::runtime_error("Route name count does not match scenario route count");
    }
    for (const std::string& route : rows.at(routeOffset + 2)) {
        scenario.schedule.routeByBus.push_back(std::stoi(route));
    }
    if (scenario.schedule.routeByBus.size() != static_cast<size_t>(busCount)) {
        throw std::runtime_error("Route assignment count does not match bus count");
    }
    return scenario;
}

} // namespace

InstanceData LoadInstance(const RunConfig& config)
{
    InstanceData instance;
    instance.name = config.dataName;
    instance.root = DataRoot(config.dataName);
    std::unordered_map<int, int> globalPlugCounts;
    ReadGlobalData(instance, config, globalPlugCounts);
    for (ScenarioDescriptor descriptor : ReadScenarioDescriptors(instance.root, config)) {
        instance.scenarios.push_back(ReadScenario(instance, std::move(descriptor), config, globalPlugCounts));
    }
    return instance;
}

} // namespace wireless
