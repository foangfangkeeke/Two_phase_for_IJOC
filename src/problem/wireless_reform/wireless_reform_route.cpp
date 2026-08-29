#include "wireless_reform_route.hpp"
#include "gurobi_c++.h"
#include "tools.hpp"
#include <iostream>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <memory>
#include <optional>
#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

using namespace std;
namespace fs = std::filesystem;

// === Duplicated definitions and helpers from wireless_battery_master.cpp ===

#define OFFSET_OPERATION_CFG 0
#define OFFSET_CHARGING_CFG (OFFSET_OPERATION_CFG + 3)
#define OFFSET_SCENARIO_NETWORK_CFG 0
#define OFFSET_SCENARIO_SCHEDULING_CFG (OFFSET_SCENARIO_NETWORK_CFG + 2)
#define OFFSET_SCENARIO_CHARGER_CFG(numEB) (OFFSET_SCENARIO_SCHEDULING_CFG + 3 + (numEB))

using linkTimeType = unordered_map<int, unordered_map<int, double>>;
using stationTimeType = vector<vector<int>>;
using stringCfgType = vector<vector<string>>;

struct WirelessScenarioConfig {
    string dataFolder;
    double probability = 1.0;
};

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
    vector<double> chargeRate;
    vector<double> chargeEfficiency;
    vector<double> priceOfUnitCharger;
    double priceOfInverterPerkW;
};

struct SchedulingCfg {
    vector<int> EBs;
    int numTrip = 0;
    int numEB;
    vector<int> tripNumEBs;
    vector<vector<int>> tripEBs;
    vector<vector<int>> stationTrips;
    vector<vector<int>> stationArrTimes;
    vector<vector<int>> stationDepTimes;
    int numRoutes = 1;
    vector<string> routeNames;
    vector<int> routeIdByEBs;
};

struct NetworkCfg {
    int chargeStationNum;
    unordered_set<int> stationPhys;
    unordered_set<int> chargerPhys;
    unordered_map<int, unordered_map<int, double>> linkDistances;
    vector<double> startDistanceEBs;
    vector<double> endDistanceEBs;
    unordered_map<int, int> chargerNums;
};

// --- File loading helpers ---

static inline fs::path ResolveDataFolderPath(const string& dataFolder) {
    return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "data" / dataFolder;
}

static optional<stringCfgType> LoadStringTable(const fs::path& filePath) {
    ifstream fin(filePath);
    if (!fin.is_open()) {
        cerr << "Warning: failed to open " << filePath << endl;
        return nullopt;
    }
    stringCfgType rows;
    string line;
    while (getline(fin, line)) {
        line = Tools::Trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        rows.push_back(Tools::SplitAndTrim(line, ',', true));
    }
    if (rows.empty()) {
        cerr << "Warning: no valid data in " << filePath << endl;
        return nullopt;
    }
    return rows;
}

static optional<stationTimeType> LoadIntTable(const fs::path& filePath) {
    auto rows = LoadStringTable(filePath);
    if (!rows.has_value()) return nullopt;
    stationTimeType table;
    table.reserve(rows->size());
    try {
        for (const auto& row : *rows) {
            vector<int> vals;
            vals.reserve(row.size());
            for (const auto& token : row)
                vals.push_back(stoi(token));
            table.push_back(vals);
        }
    } catch (const exception& e) {
        cerr << "Warning: invalid integer table in " << filePath << " - " << e.what() << endl;
        return nullopt;
    }
    return table;
}

static optional<linkTimeType> LoadLinkDistance(const fs::path& filePath) {
    auto rows = LoadStringTable(filePath);
    if (!rows.has_value()) return nullopt;
    linkTimeType links;
    try {
        for (const auto& row : *rows) {
            if (row.size() < 3) continue;
            int from = stoi(row[0]);
            int to = stoi(row[1]);
            double dist = stod(row[2]);
            links[from][to] = dist;
        }
    } catch (const exception& e) {
        cerr << "Warning: invalid link table in " << filePath << " - " << e.what() << endl;
        return nullopt;
    }
    if (links.empty()) {
        cerr << "Warning: no valid links in " << filePath << endl;
        return nullopt;
    }
    return links;
}

static vector<WirelessScenarioConfig> LoadWirelessScenarioConfigs(const string& dataRootFolder) {
    const fs::path scenarioCfgPath = ResolveDataFolderPath(dataRootFolder) / "scenarioCfg.txt";
    auto rows = LoadStringTable(scenarioCfgPath);
    if (!rows.has_value()) return {};
    const int numScenarios = stoi(rows->at(0).at(0));
    vector<WirelessScenarioConfig> scenarios;
    scenarios.reserve(numScenarios);
    for (int idx = 0; idx < numScenarios; ++idx) {
        const auto& row = rows->at(static_cast<size_t>(idx + 1));
        WirelessScenarioConfig scenario;
        scenario.dataFolder = row.at(0);
        scenario.probability = stod(row.at(1));
        scenarios.push_back(scenario);
    }
    return scenarios;
}

// --- ModelData (duplicated) ---

class ModelData {
public:
    ModelData(string dataRootFolder)
        : ModelData(dataRootFolder, LoadWirelessScenarioConfigs(dataRootFolder).front()) {}

    ModelData(string dataRootFolder, WirelessScenarioConfig scenarioConfig)
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
    string dataRootFolder;
    fs::path dataRootFolderPath;
    WirelessScenarioConfig scenarioConfig;
    fs::path scenarioFolderPath;

    fs::path ResolveRoot(const string& file) const { return dataRootFolderPath / file; }
    fs::path ResolveScenario(const string& file) const { return scenarioFolderPath / file; }

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
            vector<string> operationCfgDouble = tmpGlobalCfg->at(OFFSET_OPERATION_CFG);
            vector<string> operationCfgInt = tmpGlobalCfg->at(OFFSET_OPERATION_CFG + 1);
            vector<string> operationCfgBus = tmpGlobalCfg->at(OFFSET_OPERATION_CFG + 2);
            operationCfg.priceOfPowerDay = stod(operationCfgDouble[0]);
            operationCfg.priceOfPowerNight = stod(operationCfgDouble[1]);
            operationCfg.priceOfUnitBat = stod(operationCfgDouble[2]);
            operationCfg.socMax = stod(operationCfgDouble[3]);
            operationCfg.socMin = stod(operationCfgDouble[4]);
            operationCfg.capacityMin = stod(operationCfgDouble[5]);
            operationCfg.capacityMax = stod(operationCfgDouble[6]);
            operationCfg.timeMin = stoi(operationCfgInt[0]);
            operationCfg.timeMax = stoi(operationCfgInt[1]);
            operationCfg.curbWeight = stod(operationCfgBus[0]);
            operationCfg.weightPerEnergy = stod(operationCfgBus[1]);
            operationCfg.mu = stod(operationCfgBus[2]);
            operationCfg.g = stod(operationCfgBus[3]);

            vector<string> chargerCfgNum = tmpGlobalCfg->at(OFFSET_CHARGING_CFG);
            vector<string> chargerCfgRate = tmpGlobalCfg->at(OFFSET_CHARGING_CFG + 1);
            vector<string> chargerCfgEfficiency = tmpGlobalCfg->at(OFFSET_CHARGING_CFG + 2);
            vector<string> chargerCfgInt = tmpGlobalCfg->at(OFFSET_CHARGING_CFG + 3);
            chargerCfg.num = stoi(chargerCfgNum[0]);
            for (int i = 0; i < chargerCfg.num; i++) {
                chargerCfg.chargeRate.push_back(stod(chargerCfgRate[i]));
                chargerCfg.chargeEfficiency.push_back(stod(chargerCfgEfficiency[i]));
                chargerCfg.priceOfUnitCharger.push_back(stod(chargerCfgInt[i]));
            }
            chargerCfg.priceOfInverterPerkW = stod(chargerCfgInt[chargerCfg.num]);
        }
        return true;
    }

    bool LoadScenarioConfigFromTxt() {
        auto tmpScenarioCfg = LoadStringTable(ResolveScenario("scenarioMiscCfg.txt"));
        if (tmpScenarioCfg.has_value()) {
            vector<string> networkCfgStart = tmpScenarioCfg->at(OFFSET_SCENARIO_NETWORK_CFG);
            vector<string> networkCfgEnd = tmpScenarioCfg->at(OFFSET_SCENARIO_NETWORK_CFG + 1);

            vector<string> schedulingCfgNum = tmpScenarioCfg->at(OFFSET_SCENARIO_SCHEDULING_CFG);
            vector<string> schedulingCfgEBs = tmpScenarioCfg->at(OFFSET_SCENARIO_SCHEDULING_CFG + 1);
            vector<string> schedulingCfgTripNumEBs = tmpScenarioCfg->at(OFFSET_SCENARIO_SCHEDULING_CFG + 2);
            schedulingCfg.numEB = stoi(schedulingCfgNum[0]);
            for (int i = 0; i < schedulingCfg.numEB; i++) {
                networkCfg.startDistanceEBs.push_back(stod(networkCfgStart[i]));
                networkCfg.endDistanceEBs.push_back(stod(networkCfgEnd[i]));
                schedulingCfg.EBs.push_back(stoi(schedulingCfgEBs[i]));
                schedulingCfg.tripNumEBs.push_back(stoi(schedulingCfgTripNumEBs[i]));
                schedulingCfg.numTrip += stoi(schedulingCfgTripNumEBs[i]);
                vector<string> schedulingCfgTrips = tmpScenarioCfg->at(OFFSET_SCENARIO_SCHEDULING_CFG + 3 + i);
                vector<int> tmp;
                for (int j = 0; j < schedulingCfg.tripNumEBs[i]; j++)
                    tmp.push_back(stoi(schedulingCfgTrips[j]));
                schedulingCfg.tripEBs.push_back(tmp);
            }

            vector<string> chargeCfgNums = tmpScenarioCfg->at(OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB));
            networkCfg.chargeStationNum = stoi(chargeCfgNums[0]);
            for (int i = 0; i < networkCfg.chargeStationNum; i++) {
                vector<string> chargerCfgNums = tmpScenarioCfg->at(
                    OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB) + 1 + i);
                networkCfg.chargerNums[stoi(chargerCfgNums[0])] = stoi(chargerCfgNums[1]);
                networkCfg.chargerPhys.insert(stoi(chargerCfgNums[0]));
            }

            const size_t routeCfgOffset = static_cast<size_t>(
                OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB) + 1 + networkCfg.chargeStationNum);
            schedulingCfg.numRoutes = stoi(tmpScenarioCfg->at(routeCfgOffset)[0]);
            schedulingCfg.routeNames = tmpScenarioCfg->at(routeCfgOffset + 1);
            const auto& routeIdTokens = tmpScenarioCfg->at(routeCfgOffset + 2);
            schedulingCfg.routeIdByEBs.reserve(routeIdTokens.size());
            for (const auto& token : routeIdTokens)
                schedulingCfg.routeIdByEBs.push_back(stoi(token));
        }
        return true;
    }
};

// === Facility RHS constraint helpers ===

constexpr const char* kWirelessFacilityRhsConstrGroup = "wireless_facility_rhs_constraints";
constexpr const char* kWirelessFacilityRhsConstrPrefix = "wireless_facility_rhs_m";

static string MakeWirelessFacilityRhsConstrName(size_t masterIdx, size_t serial) {
    return string(kWirelessFacilityRhsConstrPrefix) + to_string(masterIdx) + "_" + to_string(serial);
}

static bool TryParseWirelessFacilityRhsMasterIndex(const string& constrName, size_t& masterIdx) {
    const string prefix = kWirelessFacilityRhsConstrPrefix;
    if (constrName.size() <= prefix.size() || constrName.compare(0, prefix.size(), prefix) != 0)
        return false;
    size_t splitPos = constrName.find('_', prefix.size());
    if (splitPos == string::npos) return false;
    masterIdx = static_cast<size_t>(stoull(constrName.substr(prefix.size(), splitPos - prefix.size())));
    return true;
}

static bool TryGetWirelessFacilityRhsMasterIndex(const GRBConstr& constr, size_t masterCount, size_t& masterIdx) {
    return TryParseWirelessFacilityRhsMasterIndex(constr.get(GRB_StringAttr_ConstrName), masterIdx)
        && masterIdx < masterCount;
}

static void SetWirelessFacilityRhsConstrs(vector<GRBConstr>& constrs, double rhs) {
    for (auto& c : constrs) c.set(GRB_DoubleAttr_RHS, rhs);
}

static void SetWirelessFacilityRhsConstrs(vector<GRBConstr>& constrs, const vector<double>& zValues) {
    for (auto& c : constrs) {
        size_t mi = 0;
        if (TryGetWirelessFacilityRhsMasterIndex(c, zValues.size(), mi))
            c.set(GRB_DoubleAttr_RHS, zValues[mi]);
    }
}

// === Battery RHS constraint helpers (for RouteBatteryRhsConstr) ===

constexpr const char* kWirelessBatteryRhsConstrPrefix = "wireless_battery_rhs_r";

static string MakeWirelessBatteryRhsConstrName(int routeId, size_t serial) {
    return string(kWirelessBatteryRhsConstrPrefix) + to_string(routeId) + "_" + to_string(serial);
}

static void UpdateRouteBatteryRhsConstrs(vector<RouteBatteryRhsConstr>& constrs, double routeE) {
    for (auto& brc : constrs) {
        double rhs = brc.beta * routeE + brc.constant;
        brc.constr.set(GRB_DoubleAttr_RHS, rhs);
    }
}

// Build flat array index: [scenario][routeId] → [(capacity, masterVarIndex), ...]
// Eliminates O(N×M) string scanning in dual extraction and routeE computation.
static auto BuildCapacityVarIdxForRoute(const vector<ProblemDataVar>& masterVars)
    -> vector<vector<vector<pair<int, size_t>>>>
{
    int maxSc = -1, maxRid = -1;
    for (const auto& var : masterVars) {
        const string& name = var.name;
        if (name.rfind("sc", 0) != 0) continue;
        size_t ePos = name.find("_E_r");
        if (ePos == string::npos) continue;
        size_t capPos = name.find("_cap", ePos + 4);
        if (capPos == string::npos) continue;
        int sc = stoi(name.substr(2, ePos - 2));
        int rid = stoi(name.substr(ePos + 4, capPos - (ePos + 4)));
        if (sc > maxSc) maxSc = sc;
        if (rid > maxRid) maxRid = rid;
    }
    vector<vector<vector<pair<int, size_t>>>> idx(
        static_cast<size_t>(maxSc + 1),
        vector<vector<pair<int, size_t>>>(static_cast<size_t>(maxRid + 1)));
    for (size_t j = 0; j < masterVars.size(); ++j) {
        const string& name = masterVars[j].name;
        if (name.rfind("sc", 0) != 0) continue;
        size_t ePos = name.find("_E_r");
        if (ePos == string::npos) continue;
        size_t capPos = name.find("_cap", ePos + 4);
        if (capPos == string::npos) continue;
        int sc = stoi(name.substr(2, ePos - 2));
        int rid = stoi(name.substr(ePos + 4, capPos - (ePos + 4)));
        int cap = stoi(name.substr(capPos + 4));
        idx[static_cast<size_t>(sc)][static_cast<size_t>(rid)].emplace_back(cap, j);
    }
    return idx;
}

// O(1) array lookup: compute routeE for target (scenario, route)
static double ComputeRouteEForRouteIdx(const vector<double>& zValues,
    size_t scenarioIdx, int targetRouteId,
    const vector<vector<vector<pair<int, size_t>>>>& capIdx)
{
    double routeE = 0.0;
    if (scenarioIdx >= capIdx.size()) return routeE;
    const auto& scVec = capIdx[scenarioIdx];
    const size_t rid = static_cast<size_t>(targetRouteId);
    if (rid >= scVec.size()) return routeE;
    for (const auto& [capacity, j] : scVec[rid])
        routeE += static_cast<double>(capacity) * zValues[j];
    return routeE;
}

// Dummy index; calls the O(N×M) version if index is empty
static double RouteEHelper(const vector<ProblemDataVar>& masterVars,
    const vector<double>& zValues, size_t scenarioIdx, int targetRouteId,
    const vector<vector<vector<pair<int, size_t>>>>& capIdx)
{
    if (!capIdx.empty())
        return ComputeRouteEForRouteIdx(zValues, scenarioIdx, targetRouteId, capIdx);
    // fallback
    double routeE = 0.0;
    for (size_t j = 0; j < masterVars.size(); ++j) {
        const string& name = masterVars[j].name;
        if (name.find("_E_r") != string::npos && name.find("_cap") != string::npos) {
            size_t scPos = 2;
            size_t ePos = name.find("_E_r");
            int varScIdx = stoi(name.substr(scPos, ePos - scPos));
            if (static_cast<size_t>(varScIdx) != scenarioIdx) continue;
            size_t rPos = ePos + 4;
            size_t capPos = name.find("_cap", rPos);
            int routeId = stoi(name.substr(rPos, capPos - rPos));
            if (routeId != targetRouteId) continue;
            int capacity = stoi(name.substr(capPos + 4));
            routeE += static_cast<double>(capacity) * zValues[j];
        }
    }
    return routeE;
}

// O(1) array lookup: extract duals for target (scenario, route)
static void ExtractRouteBatteryRhsDualsIdx(const vector<RouteBatteryRhsConstr>& batteryRhs,
    const vector<double>& zValues,
    size_t scenarioIdx, int targetRouteId, LShapeCutInfo& cutInfo,
    const vector<vector<vector<pair<int, size_t>>>>& capIdx)
{
    if (scenarioIdx >= capIdx.size()) return;
    const auto& scVec = capIdx[scenarioIdx];
    const size_t rid = static_cast<size_t>(targetRouteId);
    if (rid >= scVec.size()) return;
    for (const auto& brc : batteryRhs) {
        const double pi = brc.constr.get(GRB_DoubleAttr_Pi);
        if (std::fabs(pi) < 1e-14) continue;
        for (const auto& [capacity, j] : scVec[rid]) {
            const double coeff = pi * brc.beta * static_cast<double>(capacity);
            cutInfo.yCoeffs[j] += coeff;
            cutInfo.constant -= coeff * zValues[j];
        }
    }
}

// O(1) array lookup: extract Farkas duals for target (scenario, route)
static void ExtractRouteBatteryRhsFarkasDualsIdx(const vector<RouteBatteryRhsConstr>& batteryRhs,
    const vector<double>& zValues,
    size_t scenarioIdx, int targetRouteId, LShapeCutInfo& cutInfo,
    const vector<vector<vector<pair<int, size_t>>>>& capIdx)
{
    if (scenarioIdx >= capIdx.size()) return;
    const auto& scVec = capIdx[scenarioIdx];
    const size_t rid = static_cast<size_t>(targetRouteId);
    if (rid >= scVec.size()) return;
    for (const auto& brc : batteryRhs) {
        const double lambda = brc.constr.get(GRB_DoubleAttr_FarkasDual);
        if (std::fabs(lambda) < 1e-14) continue;
        for (const auto& [capacity, j] : scVec[rid]) {
            const double coeff = lambda * brc.beta * static_cast<double>(capacity);
            cutInfo.yCoeffs[j] += coeff;
            cutInfo.constant -= coeff * zValues[j];
        }
    }
}

static void ExtractRouteBatteryRhsDuals(const vector<RouteBatteryRhsConstr>& batteryRhs,
    const vector<ProblemDataVar>& masterVars, const vector<double>& zValues,
    size_t scenarioIdx, int targetRouteId, LShapeCutInfo& cutInfo)
{
    for (const auto& brc : batteryRhs) {
        const double pi = brc.constr.get(GRB_DoubleAttr_Pi);
        for (size_t j = 0; j < masterVars.size(); ++j) {
            const string& name = masterVars[j].name;
            if (name.find("_E_r") != string::npos && name.find("_cap") != string::npos) {
                size_t scPos = 2;
                size_t ePos = name.find("_E_r");
                int varScIdx = stoi(name.substr(scPos, ePos - scPos));
                if (static_cast<size_t>(varScIdx) != scenarioIdx) continue;

                size_t rPos = ePos + 4;
                size_t capPos = name.find("_cap", rPos);
                int routeId = stoi(name.substr(rPos, capPos - rPos));
                if (routeId != targetRouteId) continue;

                int capacity = stoi(name.substr(capPos + 4));
                cutInfo.yCoeffs[j] += pi * brc.beta * static_cast<double>(capacity);
                cutInfo.constant -= pi * brc.beta * static_cast<double>(capacity) * zValues[j];
            }
        }
    }
}

static void ExtractRouteBatteryRhsFarkasDuals(const vector<RouteBatteryRhsConstr>& batteryRhs,
    const vector<ProblemDataVar>& masterVars, const vector<double>& zValues,
    size_t scenarioIdx, int targetRouteId, LShapeCutInfo& cutInfo)
{
    for (const auto& brc : batteryRhs) {
        const double lambda = brc.constr.get(GRB_DoubleAttr_FarkasDual);
        for (size_t j = 0; j < masterVars.size(); ++j) {
            const string& name = masterVars[j].name;
            if (name.find("_E_r") != string::npos && name.find("_cap") != string::npos) {
                size_t scPos = 2;
                size_t ePos = name.find("_E_r");
                int varScIdx = stoi(name.substr(scPos, ePos - scPos));
                if (static_cast<size_t>(varScIdx) != scenarioIdx) continue;

                size_t rPos = ePos + 4;
                size_t capPos = name.find("_cap", rPos);
                int routeId = stoi(name.substr(rPos, capPos - rPos));
                if (routeId != targetRouteId) continue;

                int capacity = stoi(name.substr(capPos + 4));
                cutInfo.yCoeffs[j] += lambda * brc.beta * static_cast<double>(capacity);
                cutInfo.constant -= lambda * brc.beta * static_cast<double>(capacity) * zValues[j];
            }
        }
    }
}

static void BuildWirelessNoGoodFeasibilityCutRoute(
    const vector<ProblemDataVar>& masterVars, const vector<double>& zValues,
    size_t scenarioIdx, int targetRouteId, const vector<string>& routeFacilityVarNames,
    LShapeCutInfo& cutInfo)
{
    cutInfo.yCoeffs.assign(zValues.size(), 0.0);
    unordered_set<string> routeFacilityVarNameSet(routeFacilityVarNames.begin(), routeFacilityVarNames.end());

    int selectedCapacity = -1;
    for (size_t i = 0; i < masterVars.size(); ++i) {
        const string& name = masterVars[i].name;
        if (name.find("_E_r") == string::npos || name.find("_cap") == string::npos || zValues[i] <= 0.5) {
            continue;
        }

        size_t scPos = 2;
        size_t ePos = name.find("_E_r");
        int varScIdx = stoi(name.substr(scPos, ePos - scPos));
        if (static_cast<size_t>(varScIdx) != scenarioIdx) {
            continue;
        }

        size_t rPos = ePos + 4;
        size_t capPos = name.find("_cap", rPos);
        int routeId = stoi(name.substr(rPos, capPos - rPos));
        if (routeId != targetRouteId) {
            continue;
        }

        selectedCapacity = stoi(name.substr(capPos + 4));
        break;
    }

    for (size_t i = 0; i < masterVars.size(); ++i) {
        const string& name = masterVars[i].name;
        if (name.rfind("w_station_", 0) == 0 || name.rfind("w_link_", 0) == 0) {
            if (routeFacilityVarNameSet.count(name) > 0 && zValues[i] <= 0.5) {
                cutInfo.yCoeffs[i] = 1.0;
            }
            continue;
        }

        if (selectedCapacity < 0 || name.find("_E_r") == string::npos || name.find("_cap") == string::npos) {
            continue;
        }

        size_t scPos = 2;
        size_t ePos = name.find("_E_r");
        int varScIdx = stoi(name.substr(scPos, ePos - scPos));
        if (static_cast<size_t>(varScIdx) != scenarioIdx) {
            continue;
        }

        size_t rPos = ePos + 4;
        size_t capPos = name.find("_cap", rPos);
        int routeId = stoi(name.substr(rPos, capPos - rPos));
        if (routeId != targetRouteId) {
            continue;
        }

        int capacity = stoi(name.substr(capPos + 4));
        if (capacity > selectedCapacity) {
            cutInfo.yCoeffs[i] = 1.0;
        }
    }

    cutInfo.rhs = 1.0;
    cutInfo.isOptimalityCut = false;
    cutInfo.sense = '>';
    cutInfo.constant = 0.0;
}

// === BuildRouteSubProblem ===

static bool BuildRouteSubProblem(
    const string& dataRootFolder, const WirelessScenarioConfig& scenario,
    size_t scenarioIdx,
    const vector<ProblemDataVar>& masterVars, GRBModel& model,
    LShapeSubProblemContext& context,
    GRBLinExpr& aggregateObj, size_t& facilityRhsSerial,
    vector<RouteBatteryRhsConstr>& batteryRhsOut,
    const vector<int>& routeEbIndices, int targetRouteId)
{
    ModelData data(dataRootFolder, scenario);

    OperationCfg operationCfg = data.GetOperationCfg();
    ChargerCfg chargerCfg = data.GetChargerCfg();
    SchedulingCfg schedulingCfg = data.GetSchedulingCfg();
    NetworkCfg networkCfg = data.GetNetworkCfg();

    // Build lookup from master variable names to indices
    unordered_map<string, size_t> masterIdxByName;
    masterIdxByName.reserve(masterVars.size());
    for (size_t idx = 0; idx < masterVars.size(); ++idx)
        masterIdxByName[masterVars[idx].name] = idx;

    auto& facilityRhsConstrs = context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup);
    const string namePrefix = "sc" + to_string(scenarioIdx) + "_";

    // Build a set for quick EB membership check
    unordered_set<int> routeEbSet(routeEbIndices.begin(), routeEbIndices.end());

    // Battery RHS serial counter
    size_t batteryRhsSerial = 0;

    // === Per-EB variables (keyed by ebIdx) ===
    unordered_map<int, GRBVar> chargeNightByEb;                                          // ebIdx -> var

    // charge_bns: ebIdx -> [tripIdx][station] -> var
    unordered_map<int, vector<unordered_map<int, GRBVar>>> chargeByEb;

    // charge_bnl: ebIdx -> [tripIdx][startStation][endStation] -> var
    unordered_map<int, vector<unordered_map<int, unordered_map<int, GRBVar>>>> chargeLinkByEb;

    // energyDep_bns: ebIdx -> [tripIdx][stationIdx] -> var
    unordered_map<int, vector<vector<GRBVar>>> energyDepByEb;

    // energyArr_bns: ebIdx -> [tripIdx][stationIdx] -> var
    unordered_map<int, vector<vector<GRBVar>>> energyArrByEb;

    // Create chargeNight variables
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        string varName = namePrefix + "c_Night_b" + to_string(eb);
        chargeNightByEb[ebIdx] = model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, varName);
    }

    // Create per-trip/per-station variables for each EB in the route
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        size_t numEBTrip = schedulingCfg.tripEBs[ebIdx].size();

        vector<unordered_map<int, GRBVar>> charge_b;
        vector<vector<GRBVar>> energyDep_b;
        vector<vector<GRBVar>> energyArr_b;
        charge_b.reserve(numEBTrip);
        energyDep_b.reserve(numEBTrip);
        energyArr_b.reserve(numEBTrip);

        for (int tripIdx = 0; tripIdx < static_cast<int>(numEBTrip); tripIdx++) {
            int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
            size_t numEBTripStations = schedulingCfg.stationTrips[trip].size();

            unordered_map<int, GRBVar> charge_bn;
            vector<GRBVar> energyDep_bn;
            vector<GRBVar> energyArr_bn;
            energyDep_bn.reserve(numEBTripStations);
            energyArr_bn.reserve(numEBTripStations);

            for (int stationIdx = 0; stationIdx < static_cast<int>(numEBTripStations); stationIdx++) {
                int station = schedulingCfg.stationTrips[trip][stationIdx];
                string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station)
                    + "_i" + to_string(stationIdx);
                energyDep_bn.push_back(model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
                    namePrefix + "e_Dep" + sub));
                energyArr_bn.push_back(model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
                    namePrefix + "e_Arr" + sub));
                charge_bn[station] = model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
                    namePrefix + "c" + sub);
            }
            charge_b.push_back(charge_bn);
            energyDep_b.push_back(energyDep_bn);
            energyArr_b.push_back(energyArr_bn);
        }
        chargeByEb[ebIdx] = charge_b;
        energyDepByEb[ebIdx] = energyDep_b;
        energyArrByEb[ebIdx] = energyArr_b;
    }

    // Create dynamic wireless charging (link) variables
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        size_t numEBTrip = schedulingCfg.tripEBs[ebIdx].size();

        vector<unordered_map<int, unordered_map<int, GRBVar>>> charge_b;
        charge_b.reserve(numEBTrip);

        for (int tripIdx = 0; tripIdx < static_cast<int>(numEBTrip); tripIdx++) {
            int tripDep = schedulingCfg.tripEBs[ebIdx][tripIdx];
            size_t numEBTripStations = schedulingCfg.stationTrips[tripDep].size();

            unordered_map<int, unordered_map<int, GRBVar>> charge_bn;

            for (int stationIdx = 0; stationIdx < static_cast<int>(numEBTripStations); stationIdx++) {
                int stationDep = schedulingCfg.stationTrips[tripDep][stationIdx];
                int tripArr;
                int stationArr;
                int stationArrIdx;
                if (stationIdx == static_cast<int>(numEBTripStations) - 1) {
                    if (tripIdx == static_cast<int>(numEBTrip) - 1)
                        continue;
                    tripArr = schedulingCfg.tripEBs[ebIdx][tripIdx + 1];
                    stationArrIdx = 0;
                    stationArr = schedulingCfg.stationTrips[tripArr][stationArrIdx];
                } else {
                    tripArr = tripDep;
                    stationArrIdx = stationIdx + 1;
                    stationArr = schedulingCfg.stationTrips[tripArr][stationArrIdx];
                }

                string sub = "_b" + to_string(eb) + "_n" + to_string(tripDep) + "_s"
                    + to_string(stationDep) + "_i" + to_string(stationIdx)
                    + "To_n" + to_string(tripArr) + "_s" + to_string(stationArr)
                    + "_i" + to_string(stationArrIdx);
                charge_bn[stationDep][stationArr] = model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS,
                    namePrefix + "c" + sub);
            }
            charge_b.push_back(charge_bn);
        }
        chargeLinkByEb[ebIdx] = charge_b;
    }

    model.update();

    // === Objective function ===
    GRBLinExpr obj = 0;

    // Daytime charging (station + link)
    GRBLinExpr sum_day = 0;
    for (int ebIdx : routeEbIndices) {
        const auto& chargeTrips = chargeByEb[ebIdx];
        for (const auto& chargeStationMap : chargeTrips) {
            for (const auto& [station, chargeVar] : chargeStationMap)
                sum_day += chargeVar;
        }
    }
    for (int ebIdx : routeEbIndices) {
        const auto& chargeLinkTrips = chargeLinkByEb[ebIdx];
        for (const auto& chargeLinkMap : chargeLinkTrips) {
            for (const auto& [startStation, endMap] : chargeLinkMap) {
                for (const auto& [endStation, chargeVar] : endMap)
                    sum_day += chargeVar;
            }
        }
    }

    // Night charging
    GRBLinExpr sum_night = 0;
    for (int ebIdx : routeEbIndices)
        sum_night += chargeNightByEb[ebIdx];

    obj += operationCfg.priceOfPowerDay * sum_day + operationCfg.priceOfPowerNight * sum_night;

    // === Energy range constraints (battery capacity via RHS) ===
    const double socMax = operationCfg.socMax;
    const double socMin = operationCfg.socMin;
    for (int ebIdx : routeEbIndices) {
        const auto& trips = schedulingCfg.tripEBs[ebIdx];
        for (int tripIdx = 0; tripIdx < static_cast<int>(trips.size()); tripIdx++) {
            int trip = trips[tripIdx];
            size_t numEBTripStations = schedulingCfg.stationTrips[trip].size();
            for (int stationIdx = 0; stationIdx < static_cast<int>(numEBTripStations); stationIdx++) {
                GRBVar arrE = energyArrByEb[ebIdx][tripIdx][stationIdx];
                GRBVar depE = energyDepByEb[ebIdx][tripIdx][stationIdx];

                // arrE >= routeE * socMin
                batteryRhsOut.push_back({model.addConstr(arrE >= 0.0,
                    MakeWirelessBatteryRhsConstrName(targetRouteId, batteryRhsSerial++)),
                    targetRouteId, socMin, 0.0});
                // arrE <= routeE * socMax
                batteryRhsOut.push_back({model.addConstr(arrE <= 0.0,
                    MakeWirelessBatteryRhsConstrName(targetRouteId, batteryRhsSerial++)),
                    targetRouteId, socMax, 0.0});
                // depE >= routeE * socMin
                batteryRhsOut.push_back({model.addConstr(depE >= 0.0,
                    MakeWirelessBatteryRhsConstrName(targetRouteId, batteryRhsSerial++)),
                    targetRouteId, socMin, 0.0});
                // depE <= routeE * socMax
                batteryRhsOut.push_back({model.addConstr(depE <= 0.0,
                    MakeWirelessBatteryRhsConstrName(targetRouteId, batteryRhsSerial++)),
                    targetRouteId, socMax, 0.0});
            }
        }
    }

    // === Fast charging station constraints ===
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        for (int tripIdx = 0; tripIdx < static_cast<int>(schedulingCfg.tripEBs[ebIdx].size()); tripIdx++) {
            int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < static_cast<int>(schedulingCfg.stationTrips[trip].size()); stationIdx++) {
                int station = schedulingCfg.stationTrips[trip][stationIdx];
                if (!networkCfg.chargerPhys.count(station))
                    continue;
                string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station)
                    + "_i" + to_string(stationIdx);
                GRBVar charge = chargeByEb[ebIdx][tripIdx][station];
                const int stayTime = schedulingCfg.stationDepTimes[trip][stationIdx]
                    - schedulingCfg.stationArrTimes[trip][stationIdx];
                model.addConstr(charge <= chargerCfg.chargeRate[0] * static_cast<double>(stayTime),
                    "constrChargeFast" + sub);
            }
        }
    }

    // === Static wireless charging station constraints ===
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        for (int tripIdx = 0; tripIdx < static_cast<int>(schedulingCfg.tripEBs[ebIdx].size()); tripIdx++) {
            int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < static_cast<int>(schedulingCfg.stationTrips[trip].size()); stationIdx++) {
                int station = schedulingCfg.stationTrips[trip][stationIdx];
                if (networkCfg.chargerPhys.count(station))
                    continue;

                const string masterVarName = "w_station_s" + to_string(station);
                const size_t masterIdx = masterIdxByName.at(masterVarName);

                string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station)
                    + "_i" + to_string(stationIdx);
                GRBVar charge = chargeByEb[ebIdx][tripIdx][station];
                const int stayTime = schedulingCfg.stationDepTimes[trip][stationIdx]
                    - schedulingCfg.stationArrTimes[trip][stationIdx];
                const double chargeUpperBound = chargerCfg.chargeRate[1] * static_cast<double>(stayTime);
                if (chargeUpperBound <= 0.0) {
                    model.addConstr(charge <= 0.0, namePrefix + "constrChargeWirelessStationZero" + sub);
                    continue;
                }

                GRBConstr rhsConstr = model.addConstr((1.0 / chargeUpperBound) * charge <= 0.0,
                    MakeWirelessFacilityRhsConstrName(masterIdx, facilityRhsSerial++));
                facilityRhsConstrs.push_back(rhsConstr);
            }
        }
    }

    // === Dynamic wireless charging link constraints ===
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        for (int tripIdx = 0; tripIdx < static_cast<int>(schedulingCfg.tripEBs[ebIdx].size()); tripIdx++) {
            int tripDep = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < static_cast<int>(schedulingCfg.stationTrips[tripDep].size()); stationIdx++) {
                int stationDep = schedulingCfg.stationTrips[tripDep][stationIdx];
                int tripArrIdx;
                int stationArrIdx;
                if (stationIdx == static_cast<int>(schedulingCfg.stationTrips[tripDep].size()) - 1) {
                    if (tripIdx == static_cast<int>(schedulingCfg.tripEBs[ebIdx].size()) - 1)
                        continue;
                    tripArrIdx = tripIdx + 1;
                    stationArrIdx = 0;
                } else {
                    tripArrIdx = tripIdx;
                    stationArrIdx = stationIdx + 1;
                }

                int tripArr = schedulingCfg.tripEBs[ebIdx][tripArrIdx];
                int stationArr = schedulingCfg.stationTrips[tripArr][stationArrIdx];
                const string masterVarName = "w_link_l" + to_string(stationDep) + "To" + to_string(stationArr);
                const size_t masterIdx = masterIdxByName.at(masterVarName);

                string subCharge = "_b" + to_string(eb) + "_n" + to_string(tripDep) + "_s"
                    + to_string(stationDep) + "_i" + to_string(stationIdx)
                    + "To_n" + to_string(tripArr) + "_s" + to_string(stationArr)
                    + "_i" + to_string(stationArrIdx);
                GRBVar linkCharge = chargeLinkByEb[ebIdx][tripIdx][stationDep][stationArr];
                int timeDep = schedulingCfg.stationDepTimes[tripDep][stationIdx];
                int timeArr = schedulingCfg.stationArrTimes[tripArr][stationArrIdx];
                int travelTime = timeArr - timeDep;
                const double chargeUpperBound = chargerCfg.chargeRate[2] * static_cast<double>(travelTime);
                if (chargeUpperBound <= 0.0) {
                    model.addConstr(linkCharge <= 0.0, namePrefix + "constrChargeWirelessLinkZero" + subCharge);
                    continue;
                }

                GRBConstr rhsConstr = model.addConstr((1.0 / chargeUpperBound) * linkCharge <= 0.0,
                    MakeWirelessFacilityRhsConstrName(masterIdx, facilityRhsSerial++));
                facilityRhsConstrs.push_back(rhsConstr);
            }
        }
    }

    // === First and last trip energy (battery capacity via RHS) ===
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        const auto& trips = schedulingCfg.tripEBs[ebIdx];

        // Start trip
        int startTrip = trips[0];
        int startStation = schedulingCfg.stationTrips[startTrip][0];
        double distStart = networkCfg.startDistanceEBs[ebIdx];
        double C_start = distStart * operationCfg.curbWeight * operationCfg.mu * operationCfg.g / 3.6;
        double beta_start = socMax - distStart * operationCfg.weightPerEnergy
            * operationCfg.mu * operationCfg.g / 3.6;
        GRBVar startEnergy = energyArrByEb[ebIdx][0][0];
        batteryRhsOut.push_back({model.addConstr(startEnergy == 0.0,
            MakeWirelessBatteryRhsConstrName(targetRouteId, batteryRhsSerial++)),
            targetRouteId, beta_start, -C_start});

        // End trip
        int endTripIdx = static_cast<int>(trips.size()) - 1;
        int endTrip = trips[endTripIdx];
        int endStationIdx = static_cast<int>(schedulingCfg.stationTrips[endTrip].size()) - 1;
        double distEnd = networkCfg.endDistanceEBs[ebIdx];
        double C_end = distEnd * operationCfg.curbWeight * operationCfg.mu * operationCfg.g / 3.6;
        double beta_end = socMin + distEnd * operationCfg.weightPerEnergy
            * operationCfg.mu * operationCfg.g / 3.6;
        GRBVar endEnergy = energyDepByEb[ebIdx][endTripIdx][endStationIdx];
        batteryRhsOut.push_back({model.addConstr(endEnergy >= 0.0,
            MakeWirelessBatteryRhsConstrName(targetRouteId, batteryRhsSerial++)),
            targetRouteId, beta_end, C_end});
    }

    // === Inner station energy connection ===
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        for (int tripIdx = 0; tripIdx < static_cast<int>(schedulingCfg.tripEBs[ebIdx].size()); tripIdx++) {
            int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < static_cast<int>(schedulingCfg.stationTrips[trip].size()); stationIdx++) {
                int station = schedulingCfg.stationTrips[trip][stationIdx];
                string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station)
                    + "_i" + to_string(stationIdx);
                GRBVar arrE = energyArrByEb[ebIdx][tripIdx][stationIdx];
                GRBVar depE = energyDepByEb[ebIdx][tripIdx][stationIdx];
                if (networkCfg.chargerPhys.count(station)) {
                    GRBVar charge = chargeByEb[ebIdx][tripIdx][station];
                    model.addConstr(depE == arrE + charge * chargerCfg.chargeEfficiency[0],
                        "e_InnerStation" + sub);
                } else {
                    GRBVar charge = chargeByEb[ebIdx][tripIdx][station];
                    model.addConstr(depE == arrE + charge * chargerCfg.chargeEfficiency[1],
                        "e_InnerStation" + sub);
                }
            }
        }
    }

    // === Inter station energy connection (battery capacity via RHS) ===
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        for (int tripIdx = 0; tripIdx < static_cast<int>(schedulingCfg.tripEBs[ebIdx].size()); tripIdx++) {
            int tripDep = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < static_cast<int>(schedulingCfg.stationTrips[tripDep].size()); stationIdx++) {
                int stationDep = schedulingCfg.stationTrips[tripDep][stationIdx];
                GRBVar depE = energyDepByEb[ebIdx][tripIdx][stationIdx];

                int tripArrIdx;
                int stationArrIdx;
                if (stationIdx == static_cast<int>(schedulingCfg.stationTrips[tripDep].size()) - 1) {
                    if (tripIdx == static_cast<int>(schedulingCfg.tripEBs[ebIdx].size()) - 1)
                        continue;
                    tripArrIdx = tripIdx + 1;
                    stationArrIdx = 0;
                } else {
                    tripArrIdx = tripIdx;
                    stationArrIdx = stationIdx + 1;
                }

                int tripArr = schedulingCfg.tripEBs[ebIdx][tripArrIdx];
                int stationArr = schedulingCfg.stationTrips[tripArr][stationArrIdx];
                GRBVar arrE = energyArrByEb[ebIdx][tripArrIdx][stationArrIdx];
                double dist = networkCfg.linkDistances[stationDep][stationArr];

                double C_inter = dist * operationCfg.curbWeight * operationCfg.mu * operationCfg.g / 3.6;
                double beta_inter = -dist * operationCfg.weightPerEnergy
                    * operationCfg.mu * operationCfg.g / 3.6;
                GRBVar linkCharge = chargeLinkByEb[ebIdx][tripIdx][stationDep][stationArr];
                batteryRhsOut.push_back({model.addConstr(
                    arrE - depE - linkCharge * chargerCfg.chargeEfficiency[2] == 0.0,
                    MakeWirelessBatteryRhsConstrName(targetRouteId, batteryRhsSerial++)),
                    targetRouteId, beta_inter, -C_inter});
            }
        }
    }

    // === Overnight charging (battery capacity via RHS) ===
    for (int ebIdx : routeEbIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        const auto& trips = schedulingCfg.tripEBs[ebIdx];
        int endTripIdx = static_cast<int>(trips.size()) - 1;
        int endTrip = trips[endTripIdx];
        int endStationIdx = static_cast<int>(schedulingCfg.stationTrips[endTrip].size()) - 1;
        GRBVar endDepE = energyDepByEb[ebIdx][endTripIdx][endStationIdx];
        GRBVar nightCharge = chargeNightByEb[ebIdx];
        double dist = networkCfg.endDistanceEBs[ebIdx];

        double C_night = dist * operationCfg.curbWeight * operationCfg.mu * operationCfg.g / 3.6;
        double beta_night = socMax + dist * operationCfg.weightPerEnergy
            * operationCfg.mu * operationCfg.g / 3.6;
        batteryRhsOut.push_back({model.addConstr(
            nightCharge * chargerCfg.chargeEfficiency[0] + endDepE == 0.0,
            "wireless_battery_rhs_r" + to_string(targetRouteId) + "_overnight_b" + to_string(eb)),
            targetRouteId, beta_night, C_night});
    }

    aggregateObj += scenario.probability * obj;
    model.update();
    return true;
}

// === RouteSubProblem method implementations ===

int RouteSubProblem::ScenarioCount(const ProblemData&) const { return 1; }

void RouteSubProblem::InitSubProblem(const ProblemData& problemData, int scenarioIndex, GRBModel& subModel,
    LShapeSubProblemContext& context)
{
    this->scenarioIndex = scenarioIndex;

    const auto& dataRootFolder = problemData.getData<string>("wireless_data_root_folder");
    const auto& masterVars = problemData.getData<vector<ProblemDataVar>>("masterVars");

    // Load scenario configs directly from files
    vector<WirelessScenarioConfig> scenarios = LoadWirelessScenarioConfigs(dataRootFolder);
    if (scenarioIndex >= static_cast<int>(scenarios.size()))
        throw runtime_error("RouteSubProblem: invalid scenario index " + to_string(scenarioIndex)
            + " (max " + to_string(scenarios.size() - 1) + ")");

    context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup).clear();
    batteryRhsConstrs.clear();

    GRBLinExpr obj = 0.0;
    size_t facilityRhsSerial = 0;

    bool built = BuildRouteSubProblem(dataRootFolder, scenarios[static_cast<size_t>(scenarioIndex)],
        static_cast<size_t>(scenarioIndex), masterVars, subModel, context,
        obj, facilityRhsSerial, batteryRhsConstrs,
        routeInfo.ebIndices, routeInfo.routeId);
    if (!built)
        throw runtime_error("RouteSubProblem: BuildRouteSubProblem failed");

    subModel.setObjective(obj, GRB_MINIMIZE);

    // Initialize all facility RHS to 0
    auto& facilityRhsConstrs = context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup);
    SetWirelessFacilityRhsConstrs(facilityRhsConstrs, 0.0);

    subModel.set(GRB_IntParam_Method, 1);  // dual simplex
    subModel.set(GRB_IntParam_InfUnbdInfo, 1);  // ensure FarkasProof available
    subModel.update();

    // Build capacity var index once (shared across all RouteSubProblem instances)
    if (capacityVarIdx.empty()) {
        capacityVarIdx = BuildCapacityVarIdxForRoute(masterVars);
    }

    lowerBoundReady = false;
    qLowerBound = 0.0;
    lowerBoundCutAdded = false;
}

void RouteSubProblem::UpdateSubProblem(const ProblemData& problemData, GRBModel& subModel,
    LShapeSubProblemContext& context, const vector<double>& zValues)
{
    const auto& masterVars = problemData.getData<vector<ProblemDataVar>>("masterVars");
    if (zValues.size() != masterVars.size())
        throw runtime_error("RouteSubProblem: master value size mismatch");

    // Update facility RHS constraints
    auto& facilityRhsConstrs = context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup);
    SetWirelessFacilityRhsConstrs(facilityRhsConstrs, zValues);

    // Compute routeE for the target route and update battery RHS constraints
    double routeE = RouteEHelper(masterVars, zValues,
        static_cast<size_t>(scenarioIndex), routeInfo.routeId, capacityVarIdx);
    UpdateRouteBatteryRhsConstrs(batteryRhsConstrs, routeE);

    subModel.update();
}

Status RouteSubProblem::SolveSubProblem(const ProblemData& problemData, GRBModel& subModel,
    LShapeSubProblemContext& context, const vector<double>& zValues,
    LShapeCutInfo& cutInfo, double& subObj)
{
    subModel.optimize();
    int status = subModel.get(GRB_IntAttr_Status);

    if (status == GRB_OPTIMAL) {
        subObj = subModel.get(GRB_DoubleAttr_ObjVal);

        if (lowerBoundReady && !lowerBoundCutAdded) {
            cutInfo.isOptimalityCut = true;
            cutInfo.sense = '>';
            cutInfo.rhs = 0.0;
            cutInfo.constant = qLowerBound;
            cutInfo.yCoeffs.assign(zValues.size(), 0.0);
            lowerBoundCutAdded = true;
            return OK;
        }

        // Extract duals from facility RHS and battery RHS constraints
        cutInfo.isOptimalityCut = true;
        cutInfo.sense = '>';
        cutInfo.rhs = 0.0;
        cutInfo.yCoeffs.assign(zValues.size(), 0.0);
        cutInfo.constant = subObj;

        const auto& masterVars = problemData.getData<vector<ProblemDataVar>>("masterVars");

        // Facility duals
        auto& facilityRhsConstrs = context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup);
        for (auto& constr : facilityRhsConstrs) {
            size_t masterIdx = 0;
            if (TryGetWirelessFacilityRhsMasterIndex(constr, zValues.size(), masterIdx)) {
                const double pi = constr.get(GRB_DoubleAttr_Pi);
                cutInfo.yCoeffs[masterIdx] += pi;
                cutInfo.constant -= pi * zValues[masterIdx];
            }
        }

        // Battery duals
        ExtractRouteBatteryRhsDualsIdx(batteryRhsConstrs, zValues,
            static_cast<size_t>(scenarioIndex), routeInfo.routeId, cutInfo, capacityVarIdx);

        return OK;
    }

    if (status == GRB_INFEASIBLE || status == GRB_INF_OR_UNBD || status == GRB_UNBOUNDED) {
        const auto& masterVars = problemData.getData<vector<ProblemDataVar>>("masterVars");
        try {
            const double farkasProof = subModel.get(GRB_DoubleAttr_FarkasProof);
            if (isfinite(farkasProof) && farkasProof > 1e-9) {
                cutInfo.isOptimalityCut = false;
                cutInfo.sense = '>';
                cutInfo.rhs = farkasProof;
                cutInfo.yCoeffs.assign(zValues.size(), 0.0);
                cutInfo.constant = 0.0;

                // Facility Farkas duals
                auto& facilityRhsConstrs = context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup);
                for (auto& constr : facilityRhsConstrs) {
                    size_t masterIdx = 0;
                    if (TryGetWirelessFacilityRhsMasterIndex(constr, zValues.size(), masterIdx)) {
                        const double lambda = constr.get(GRB_DoubleAttr_FarkasDual);
                        cutInfo.yCoeffs[masterIdx] += lambda;
                        cutInfo.constant -= lambda * zValues[masterIdx];
                    }
                }

                // Battery Farkas duals
                ExtractRouteBatteryRhsFarkasDualsIdx(batteryRhsConstrs, zValues,
                    static_cast<size_t>(scenarioIndex), routeInfo.routeId, cutInfo, capacityVarIdx);

                subObj = GRB_INFINITY;
                return OK;
            }
        } catch (const GRBException&) {
            // FarkasProof not available, fall back to no-good
        }

        const auto& routeFacilityVarNames = problemData.getData<vector<vector<vector<string>>>>(
            "wireless_route_facility_var_names");
        BuildWirelessNoGoodFeasibilityCutRoute(masterVars, zValues,
            static_cast<size_t>(scenarioIndex), routeInfo.routeId,
            routeFacilityVarNames.at(static_cast<size_t>(scenarioIndex)).at(static_cast<size_t>(routeInfo.routeId)),
            cutInfo);
        subObj = GRB_INFINITY;
        return OK;
    }

    cerr << "RouteSubProblem: unexpected status " << status << endl;
    return ERROR;
}

Status RouteSubProblem::SolveRelaxedSubProblem(const ProblemData& pd, GRBModel& sm,
    LShapeSubProblemContext& ctx, const vector<double>& zv, LShapeCutInfo& ci, double& so)
{
    return SolveSubProblem(pd, sm, ctx, zv, ci, so);
}

void RouteSubProblem::SetLowerBound(double lb) { qLowerBound = lb; lowerBoundReady = true; }

unique_ptr<ISubProblem_LShape> RouteSubProblem::Clone() const {
    auto c = make_unique<RouteSubProblem>();
    c->scenarioIndex = scenarioIndex;
    c->routeInfo = routeInfo;
    c->lowerBoundReady = lowerBoundReady;
    c->qLowerBound = qLowerBound;
    c->lowerBoundCutAdded = lowerBoundCutAdded;
    return c;
}
