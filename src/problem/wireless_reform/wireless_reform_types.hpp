#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <optional>
#include <iostream>
#include <algorithm>
#include "tools.hpp"

namespace fs = std::filesystem;

using linkTimeType = std::unordered_map<int, std::unordered_map<int, double>>;
using stationTimeType = std::vector<std::vector<int>>;
using stringCfgType = std::vector<std::vector<std::string>>;

inline fs::path ResolveDataFolderPath(const std::string& dataFolder) {
    return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "data" / dataFolder;
}

inline std::optional<stringCfgType> LoadStringTable(const fs::path& filePath) {
    std::ifstream fin(filePath);
    if (!fin.is_open()) {
        std::cerr << "Warning: failed to open " << filePath << std::endl;
        return std::nullopt;
    }
    stringCfgType rows;
    std::string line;
    while (std::getline(fin, line)) {
        line = Tools::Trim(line);
        if (line.empty() || line[0] == '#') continue;
        rows.push_back(Tools::SplitAndTrim(line, ',', true));
    }
    if (rows.empty()) {
        std::cerr << "Warning: no valid data in " << filePath << std::endl;
        return std::nullopt;
    }
    return rows;
}

inline std::optional<stationTimeType> LoadIntTable(const fs::path& filePath) {
    auto rows = LoadStringTable(filePath);
    if (!rows.has_value()) return std::nullopt;
    stationTimeType table;
    table.reserve(rows->size());
    try {
        for (const auto& row : *rows) {
            std::vector<int> vals;
            vals.reserve(row.size());
            for (const auto& token : row) vals.push_back(std::stoi(token));
            table.push_back(vals);
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: invalid integer table in " << filePath << " - " << e.what() << std::endl;
        return std::nullopt;
    }
    return table;
}

inline std::optional<linkTimeType> LoadLinkDistance(const fs::path& filePath) {
    auto rows = LoadStringTable(filePath);
    if (!rows.has_value()) return std::nullopt;
    linkTimeType links;
    try {
        for (const auto& row : *rows) {
            if (row.size() < 3) continue;
            int from = std::stoi(row[0]);
            int to = std::stoi(row[1]);
            double dist = std::stod(row[2]);
            links[from][to] = dist;
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: invalid link table in " << filePath << " - " << e.what() << std::endl;
        return std::nullopt;
    }
    if (links.empty()) {
        std::cerr << "Warning: no valid links in " << filePath << std::endl;
        return std::nullopt;
    }
    return links;
}

struct WirelessScenarioConfig {
    std::string dataFolder;
    double probability = 1.0;
};

inline std::vector<WirelessScenarioConfig> LoadWirelessScenarioConfigs(const std::string& dataRootFolder) {
    const fs::path scenarioCfgPath = ResolveDataFolderPath(dataRootFolder) / "scenarioCfg.txt";
    auto rows = LoadStringTable(scenarioCfgPath);
    if (!rows.has_value()) return {};
    const int numScenarios = std::stoi(rows->at(0).at(0));
    std::vector<WirelessScenarioConfig> scenarios;
    scenarios.reserve(numScenarios);
    for (int idx = 0; idx < numScenarios; ++idx) {
        const auto& row = rows->at(static_cast<size_t>(idx + 1));
        WirelessScenarioConfig scenario;
        scenario.dataFolder = row.at(0);
        scenario.probability = std::stod(row.at(1));
        scenarios.push_back(scenario);
    }
    return scenarios;
}

struct OperationCfg {
    double priceOfPowerDay;
    double priceOfPowerNight;
    double priceOfUnitBat;
    double socMax;
    double socMin;
    double capacityMin;
    double capacityMax;
    double curbWeight;
    double weightPerEnergy;
    double mu;
    double g;
    int timeMin;
    int timeMax;
};

struct ChargerCfg {
    int num;
    std::vector<double> chargeRate;
    std::vector<double> chargeEfficiency;
    std::vector<double> priceOfUnitCharger;
    double priceOfInverterPerkW;
};

struct SchedulingCfg {
    std::vector<int> EBs;
    int numTrip = 0;
    int numEB;
    std::vector<int> tripNumEBs;
    std::vector<std::vector<int>> tripEBs;
    std::vector<std::vector<int>> stationTrips;
    std::vector<std::vector<int>> stationArrTimes;
    std::vector<std::vector<int>> stationDepTimes;
    int numRoutes = 1;
    std::vector<std::string> routeNames;
    std::vector<int> routeIdByEBs;
};

struct NetworkCfg {
    int chargeStationNum;
    std::unordered_set<int> stationPhys;
    std::unordered_set<int> chargerPhys;
    std::unordered_map<int, std::unordered_map<int, double>> linkDistances;
    std::vector<double> startDistanceEBs;
    std::vector<double> endDistanceEBs;
    std::unordered_map<int, int> chargerNums;
};

#define OFFSET_OPERATION_CFG 0
#define OFFSET_CHARGING_CFG (OFFSET_OPERATION_CFG + 3)
#define OFFSET_GLOBAL_CHARGER_NUMS (OFFSET_CHARGING_CFG + 4)
#define OFFSET_SCENARIO_NETWORK_CFG 0
#define OFFSET_SCENARIO_SCHEDULING_CFG (OFFSET_SCENARIO_NETWORK_CFG + 2)
#define OFFSET_SCENARIO_CHARGER_CFG(numEB) (OFFSET_SCENARIO_SCHEDULING_CFG + 3 + (numEB))

class ModelData {
public:
    ModelData(std::string dataRootFolder)
        : ModelData(dataRootFolder, LoadWirelessScenarioConfigs(dataRootFolder).front()) {}

    ModelData(std::string dataRootFolder, WirelessScenarioConfig scenarioConfig)
        : dataRootFolder(std::move(dataRootFolder)),
          dataRootFolderPath(ResolveDataFolderPath(this->dataRootFolder)),
          scenarioConfig(std::move(scenarioConfig)),
          scenarioFolderPath(dataRootFolderPath / fs::path(this->scenarioConfig.dataFolder)) {
        Init();
    }
    ~ModelData() = default;

    const OperationCfg& GetOperationCfg() const { return operationCfg; }
    const ChargerCfg& GetChargerCfg() const { return chargerCfg; }
    const SchedulingCfg& GetSchedulingCfg() const { return schedulingCfg; }
    const NetworkCfg& GetNetworkCfg() const { return networkCfg; }
    const fs::path& GetDataFolderPath() const { return dataRootFolderPath; }

private:
    OperationCfg operationCfg;
    ChargerCfg chargerCfg;
    SchedulingCfg schedulingCfg;
    NetworkCfg networkCfg;
    std::string dataRootFolder;
    fs::path dataRootFolderPath;
    WirelessScenarioConfig scenarioConfig;
    fs::path scenarioFolderPath;

    fs::path ResolveRoot(const std::string& file) const { return dataRootFolderPath / file; }
    fs::path ResolveScenario(const std::string& file) const { return scenarioFolderPath / file; }

    void Init() {
        LoadGlobalConfigFromTxt();
        LoadScenarioOperationalTables();
        LoadScenarioConfigFromTxt();
        for (const auto& stationTrip : schedulingCfg.stationTrips)
            for (int station : stationTrip)
                networkCfg.stationPhys.insert(station);
    }

    bool LoadScenarioOperationalTables() {
        auto tmpDis = LoadLinkDistance(ResolveScenario("linkDistance.txt"));
        auto tmpTripArrTimes = LoadIntTable(ResolveScenario("tripArrTimes.txt"));
        auto tmpTripDepTimes = LoadIntTable(ResolveScenario("tripDepTimes.txt"));
        auto tmpTripStations = LoadIntTable(ResolveScenario("tripStations.txt"));
        if (tmpDis.has_value()) networkCfg.linkDistances = *tmpDis;
        if (tmpTripDepTimes.has_value()) schedulingCfg.stationDepTimes = *tmpTripDepTimes;
        if (tmpTripArrTimes.has_value()) schedulingCfg.stationArrTimes = *tmpTripArrTimes;
        if (tmpTripStations.has_value()) schedulingCfg.stationTrips = *tmpTripStations;
        return true;
    }

    bool LoadGlobalConfigFromTxt() {
        auto tmpGlobalCfg = LoadStringTable(ResolveRoot("globalCfg.txt"));
        if (tmpGlobalCfg.has_value()) {
            std::vector<std::string> operationCfgDouble = tmpGlobalCfg->at(OFFSET_OPERATION_CFG);
            std::vector<std::string> operationCfgInt = tmpGlobalCfg->at(OFFSET_OPERATION_CFG + 1);
            std::vector<std::string> operationCfgBus = tmpGlobalCfg->at(OFFSET_OPERATION_CFG + 2);
            operationCfg.priceOfPowerDay = std::stod(operationCfgDouble[0]);
            operationCfg.priceOfPowerNight = std::stod(operationCfgDouble[1]);
            operationCfg.priceOfUnitBat = std::stod(operationCfgDouble[2]);
            operationCfg.socMax = std::stod(operationCfgDouble[3]);
            operationCfg.socMin = std::stod(operationCfgDouble[4]);
            operationCfg.capacityMin = std::stod(operationCfgDouble[5]);
            operationCfg.capacityMax = std::stod(operationCfgDouble[6]);
            operationCfg.timeMin = std::stoi(operationCfgInt[0]);
            operationCfg.timeMax = std::stoi(operationCfgInt[1]);
            operationCfg.curbWeight = std::stod(operationCfgBus[0]);
            operationCfg.weightPerEnergy = std::stod(operationCfgBus[1]);
            operationCfg.mu = std::stod(operationCfgBus[2]);
            operationCfg.g = std::stod(operationCfgBus[3]);

            std::vector<std::string> chargerCfgNum = tmpGlobalCfg->at(OFFSET_CHARGING_CFG);
            std::vector<std::string> chargerCfgRate = tmpGlobalCfg->at(OFFSET_CHARGING_CFG + 1);
            std::vector<std::string> chargerCfgEfficiency = tmpGlobalCfg->at(OFFSET_CHARGING_CFG + 2);
            std::vector<std::string> chargerCfgInt = tmpGlobalCfg->at(OFFSET_CHARGING_CFG + 3);
            chargerCfg.num = std::stoi(chargerCfgNum[0]);
            for (int i = 0; i < chargerCfg.num; i++) {
                chargerCfg.chargeRate.push_back(std::stod(chargerCfgRate[i]));
                chargerCfg.chargeEfficiency.push_back(std::stod(chargerCfgEfficiency[i]));
                chargerCfg.priceOfUnitCharger.push_back(std::stoi(chargerCfgInt[i]));
            }
            chargerCfg.priceOfInverterPerkW = std::stod(chargerCfgInt[chargerCfg.num]);

            std::vector<std::string> chargerNumsHeader = tmpGlobalCfg->at(OFFSET_GLOBAL_CHARGER_NUMS);
            networkCfg.chargeStationNum = std::stoi(chargerNumsHeader[0]);
            for (int i = 0; i < networkCfg.chargeStationNum; i++) {
                std::vector<std::string> chargerNumsRow = tmpGlobalCfg->at(OFFSET_GLOBAL_CHARGER_NUMS + 1 + i);
                networkCfg.chargerNums[std::stoi(chargerNumsRow[0])] = std::stoi(chargerNumsRow[1]);
                networkCfg.chargerPhys.insert(std::stoi(chargerNumsRow[0]));
            }
        }
        return true;
    }

    bool LoadScenarioConfigFromTxt() {
        auto tmpScenarioCfg = LoadStringTable(ResolveScenario("scenarioMiscCfg.txt"));
        if (tmpScenarioCfg.has_value()) {
            std::vector<std::string> networkCfgStart = tmpScenarioCfg->at(OFFSET_SCENARIO_NETWORK_CFG);
            std::vector<std::string> networkCfgEnd = tmpScenarioCfg->at(OFFSET_SCENARIO_NETWORK_CFG + 1);

            std::vector<std::string> schedulingCfgNum = tmpScenarioCfg->at(OFFSET_SCENARIO_SCHEDULING_CFG);
            std::vector<std::string> schedulingCfgEBs = tmpScenarioCfg->at(OFFSET_SCENARIO_SCHEDULING_CFG + 1);
            std::vector<std::string> schedulingCfgTripNumEBs = tmpScenarioCfg->at(OFFSET_SCENARIO_SCHEDULING_CFG + 2);
            schedulingCfg.numEB = std::stoi(schedulingCfgNum[0]);
            for (int i = 0; i < schedulingCfg.numEB; i++) {
                networkCfg.startDistanceEBs.push_back(std::stod(networkCfgStart[i]));
                networkCfg.endDistanceEBs.push_back(std::stod(networkCfgEnd[i]));
                schedulingCfg.EBs.push_back(std::stoi(schedulingCfgEBs[i]));
                schedulingCfg.tripNumEBs.push_back(std::stoi(schedulingCfgTripNumEBs[i]));
                schedulingCfg.numTrip += std::stoi(schedulingCfgTripNumEBs[i]);
                std::vector<std::string> schedulingCfgTrips = tmpScenarioCfg->at(OFFSET_SCENARIO_SCHEDULING_CFG + 3 + i);
                std::vector<int> tmp;
                for (int j = 0; j < schedulingCfg.tripNumEBs[i]; j++)
                    tmp.push_back(std::stoi(schedulingCfgTrips[j]));
                schedulingCfg.tripEBs.push_back(tmp);
            }

            std::vector<std::string> chargeCfgNums = tmpScenarioCfg->at(OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB));
            networkCfg.chargeStationNum = std::stoi(chargeCfgNums[0]);
            for (int i = 0; i < networkCfg.chargeStationNum; i++) {
                std::vector<std::string> chargerCfgNums = tmpScenarioCfg->at(
                    OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB) + 1 + i);
                networkCfg.chargerNums[std::stoi(chargerCfgNums[0])] = std::stoi(chargerCfgNums[1]);
                networkCfg.chargerPhys.insert(std::stoi(chargerCfgNums[0]));
            }

            const size_t routeCfgOffset = static_cast<size_t>(
                OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB) + 1 + networkCfg.chargeStationNum);
            schedulingCfg.numRoutes = std::stoi(tmpScenarioCfg->at(routeCfgOffset)[0]);
            schedulingCfg.routeNames = tmpScenarioCfg->at(routeCfgOffset + 1);
            const auto& routeIdTokens = tmpScenarioCfg->at(routeCfgOffset + 2);
            schedulingCfg.routeIdByEBs.reserve(routeIdTokens.size());
            for (const auto& token : routeIdTokens)
                schedulingCfg.routeIdByEBs.push_back(std::stoi(token));
        }
        return true;
    }
};
