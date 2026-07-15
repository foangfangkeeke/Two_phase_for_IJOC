#include <unordered_set>
#include <unordered_map>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <algorithm>
#include <stdexcept>
#include "wireless_reform.hpp"
#include "gurobi_c++.h"
#include "tools.hpp"

using namespace std;
namespace fs = std::filesystem;

#define OFFSET_OPERATION_CFG 0
#define OFFSET_CHARGING_CFG (OFFSET_OPERATION_CFG + 3)
#define OFFSET_GLOBAL_CHARGER_NUMS (OFFSET_CHARGING_CFG + 4)
#define OFFSET_SCENARIO_NETWORK_CFG 0
#define OFFSET_SCENARIO_SCHEDULING_CFG (OFFSET_SCENARIO_NETWORK_CFG + 2)
#define OFFSET_SCENARIO_CHARGER_CFG(numEB) (OFFSET_SCENARIO_SCHEDULING_CFG + 3 + (numEB))

using linkTimeType = unordered_map<int, unordered_map<int, double>>;
using stationTimeType = vector<vector<int>>;
using stringCfgType = vector<vector<string>>;

// Build flat array index: capIdx[scenario][routeId] → [(capacity, masterVarIndex), ...]
// Replaces O(masterVars) string scanning with direct [scenario][routeId] array lookup.
static auto BuildCapacityVarIndex(const vector<ProblemDataVar>& masterVars)
    -> vector<vector<vector<pair<int, size_t>>>>
{
    // First pass: determine dimensions
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
    // Second pass: fill entries
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

namespace {
inline fs::path ResolveDataFolderPath(const string& dataFolder) {
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
        if (line.empty() || line[0] == '#') {
            continue;
        }
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
    if (!rows.has_value()) {
        return nullopt;
    }

    stationTimeType table;
    table.reserve(rows->size());
    try {
        for (const auto& row : *rows) {
            vector<int> vals;
            vals.reserve(row.size());
            for (const auto& token : row) {
                vals.push_back(stoi(token));
            }
            table.push_back(vals);
        }
    } catch (const std::exception& e) {
        cerr << "Warning: invalid integer table in " << filePath << " - " << e.what() << endl;
        return nullopt;
    }

    return table;
}

static optional<linkTimeType> LoadLinkDistance(const fs::path& filePath) {
    auto rows = LoadStringTable(filePath);
    if (!rows.has_value()) {
        return nullopt;
    }

    linkTimeType links;
    try {
        for (const auto& row : *rows) {
            if (row.size() < 3) {
                continue;
            }
            int from = stoi(row[0]);
            int to = stoi(row[1]);
            double dist = stod(row[2]);
            links[from][to] = dist;
        }
    } catch (const std::exception& e) {
        cerr << "Warning: invalid link table in " << filePath << " - " << e.what() << endl;
        return nullopt;
    }

    if (links.empty()) {
        cerr << "Warning: no valid links in " << filePath << endl;
        return nullopt;
    }
    return links;
}

constexpr const char* kWirelessFacilityRhsConstrGroup = "wireless_facility_rhs_constraints";
constexpr const char* kWirelessFacilityRhsConstrPrefix = "wireless_facility_rhs_m";

static string MakeWirelessFacilityRhsConstrName(size_t masterIdx, size_t serial)
{
    return string(kWirelessFacilityRhsConstrPrefix) + to_string(masterIdx) + "_" + to_string(serial);
}

static bool TryParseWirelessFacilityRhsMasterIndex(const string& constrName, size_t& masterIdx)
{
    const string prefix = kWirelessFacilityRhsConstrPrefix;
    if (constrName.size() <= prefix.size() || constrName.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    const size_t splitPos = constrName.find('_', prefix.size());
    if (splitPos == string::npos) {
        return false;
    }

    masterIdx = static_cast<size_t>(stoull(constrName.substr(prefix.size(), splitPos - prefix.size())));
    return true;
}

static bool TryGetWirelessFacilityRhsMasterIndex(const GRBConstr& constr, size_t masterCount, size_t& masterIdx)
{
    const string constrName = constr.get(GRB_StringAttr_ConstrName);
    return TryParseWirelessFacilityRhsMasterIndex(constrName, masterIdx) && masterIdx < masterCount;
}

static void SetWirelessFacilityRhsConstrs(vector<GRBConstr>& constrs, double rhs)
{
    for (auto& constr : constrs) {
        constr.set(GRB_DoubleAttr_RHS, rhs);
    }
}

static void SetWirelessFacilityRhsConstrs(vector<GRBConstr>& constrs, const vector<double>& zValues)
{
    for (auto& constr : constrs) {
        size_t masterIdx = 0;
        if (TryGetWirelessFacilityRhsMasterIndex(constr, zValues.size(), masterIdx)) {
            constr.set(GRB_DoubleAttr_RHS, zValues[masterIdx]);
        }
    }
}

constexpr const char* kWirelessBatteryRhsConstrPrefix = "wireless_battery_rhs_r";

static string MakeWirelessBatteryRhsConstrName(int routeId, size_t serial)
{
    return string(kWirelessBatteryRhsConstrPrefix) + to_string(routeId) + "_" + to_string(serial);
}

static vector<double> ComputeRouteEPerRoute(const vector<double>& zValues,
    size_t scenarioIdx, int numRoutes,
    const vector<vector<vector<pair<int, size_t>>>>& capIdx)
{
    vector<double> routeE(static_cast<size_t>(numRoutes), 0.0);
    if (scenarioIdx >= capIdx.size()) return routeE;
    const auto& scVec = capIdx[scenarioIdx];
    for (size_t rid = 0; rid < scVec.size(); ++rid) {
        for (const auto& [capacity, j] : scVec[rid]) {
            routeE[rid] += static_cast<double>(capacity) * zValues[j];
        }
    }
    return routeE;
}

static void UpdateBatteryRhsConstrs(vector<BatteryRhsConstr>& constrs, const vector<double>& routeE)
{
    for (auto& brc : constrs) {
        double rhs = brc.beta * routeE.at(static_cast<size_t>(brc.routeId)) + brc.constant;
        brc.constr.set(GRB_DoubleAttr_RHS, rhs);
    }
}

static void ExtractBatteryRhsDuals(const vector<BatteryRhsConstr>& batteryRhs,
    const vector<double>& zValues,
    size_t scenarioIdx, LShapeCutInfo& cutInfo,
    const vector<vector<vector<pair<int, size_t>>>>& capIdx)
{
    if (scenarioIdx >= capIdx.size()) return;
    const auto& scVec = capIdx[scenarioIdx];
    for (const auto& brc : batteryRhs) {
        const double pi = brc.constr.get(GRB_DoubleAttr_Pi);
        if (std::fabs(pi) < 1e-14) continue;
        const size_t rid = static_cast<size_t>(brc.routeId);
        if (rid >= scVec.size()) continue;
        for (const auto& [capacity, j] : scVec[rid]) {
            const double coeff = pi * brc.beta * static_cast<double>(capacity);
            cutInfo.yCoeffs[j] += coeff;
            cutInfo.constant -= coeff * zValues[j];
        }
    }
}

static void ExtractBatteryRhsFarkasDuals(const vector<BatteryRhsConstr>& batteryRhs,
    const vector<double>& zValues,
    size_t scenarioIdx, LShapeCutInfo& cutInfo,
    const vector<vector<vector<pair<int, size_t>>>>& capIdx)
{
    if (scenarioIdx >= capIdx.size()) return;
    const auto& scVec = capIdx[scenarioIdx];
    for (const auto& brc : batteryRhs) {
        const double lambda = brc.constr.get(GRB_DoubleAttr_FarkasDual);
        if (std::fabs(lambda) < 1e-14) continue;
        const size_t rid = static_cast<size_t>(brc.routeId);
        if (rid >= scVec.size()) continue;
        for (const auto& [capacity, j] : scVec[rid]) {
            const double coeff = lambda * brc.beta * static_cast<double>(capacity);
            cutInfo.yCoeffs[j] += coeff;
            cutInfo.constant -= coeff * zValues[j];
        }
    }
}

struct WirelessScenarioConfig {
    string dataFolder;
    double probability = 1.0;
};

static vector<WirelessScenarioConfig> LoadWirelessScenarioConfigs(const string& dataRootFolder)
{
    const fs::path scenarioCfgPath = ResolveDataFolderPath(dataRootFolder) / "scenarioCfg.txt";
    auto rows = LoadStringTable(scenarioCfgPath);
    if (!rows.has_value()) {
        return {};
    }

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

} // namespace

struct OperationCfg {
    double priceOfPowerDay; // CNY per kW
    double priceOfPowerNight; // CNY per kW
    double priceOfUnitBat; // CNY per kW.h
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

// charger information: fast charging, static wireless charging, dynamic wireless charging
struct ChargerCfg {
    int num;
    vector<double> chargeRate; // kW per minute;
    vector<double> chargeEfficiency;
    vector<double> priceOfUnitCharger; // unit price of facility construction
    double priceOfInverterPerkW; // unit price of inverter (CNY/kW)
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

    // RouteCfg is read from the active scenario's scenarioMiscCfg.txt.
    // routeIdByEBs is aligned with ebIdx / EBs ordering, not raw EB ids.
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

    const OperationCfg& GetOperationCfg() const {
        return operationCfg;
    }
    const ChargerCfg& GetChargerCfg() const {
        return chargerCfg;
    }
    const SchedulingCfg& GetSchedulingCfg() const {
        return schedulingCfg;
    }
    const NetworkCfg& GetNetworkCfg() const {
        return networkCfg;
    }

    const fs::path& GetDataFolderPath() const {
        return dataRootFolderPath;
    }

private:
    struct OperationCfg operationCfg;
    struct ChargerCfg chargerCfg;
    struct SchedulingCfg schedulingCfg;
    struct NetworkCfg networkCfg;
    string dataRootFolder;
    fs::path dataRootFolderPath;
    WirelessScenarioConfig scenarioConfig;
    fs::path scenarioFolderPath;

    fs::path ResolveRoot(const string& file) const {
        return dataRootFolderPath / file;
    }

    fs::path ResolveScenario(const string& file) const {
        return scenarioFolderPath / file;
    }

    void Init() {
        LoadGlobalConfigFromTxt();
        LoadScenarioOperationalTables();
        LoadScenarioConfigFromTxt();

        for (const auto& stationTrip : schedulingCfg.stationTrips) {
            for (int station : stationTrip) {
                networkCfg.stationPhys.insert(station);
            }
        }
    }

    bool LoadScenarioOperationalTables() {
        auto tmpDis = LoadLinkDistance(ResolveScenario("linkDistance.txt"));
        auto tmpTripArrTimes = LoadIntTable(ResolveScenario("tripArrTimes.txt"));
        auto tmpTripDepTimes = LoadIntTable(ResolveScenario("tripDepTimes.txt"));
        auto tmpTripStations = LoadIntTable(ResolveScenario("tripStations.txt"));
        if (tmpDis.has_value()) {
            networkCfg.linkDistances = *tmpDis;
        }
        if (tmpTripDepTimes.has_value()) {
            schedulingCfg.stationDepTimes = *tmpTripDepTimes;
        }
        if (tmpTripArrTimes.has_value()) {
            schedulingCfg.stationArrTimes = *tmpTripArrTimes;
        }
        if (tmpTripStations.has_value()) {
            schedulingCfg.stationTrips = *tmpTripStations;
        }

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
                chargerCfg.priceOfUnitCharger.push_back(stoi(chargerCfgInt[i]));
            }
            chargerCfg.priceOfInverterPerkW = stod(chargerCfgInt[chargerCfg.num]);

            vector<string> chargerNumsHeader = tmpGlobalCfg->at(OFFSET_GLOBAL_CHARGER_NUMS);
            networkCfg.chargeStationNum = stoi(chargerNumsHeader[0]);
            for (int i = 0; i < networkCfg.chargeStationNum; i++) {
                vector<string> chargerNumsRow = tmpGlobalCfg->at(OFFSET_GLOBAL_CHARGER_NUMS + 1 + i);
                networkCfg.chargerNums[stoi(chargerNumsRow[0])] = stoi(chargerNumsRow[1]);
                networkCfg.chargerPhys.insert(stoi(chargerNumsRow[0]));
            }
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
                for (int j = 0; j < schedulingCfg.tripNumEBs[i]; j++) {
                    tmp.push_back(stoi(schedulingCfgTrips[j]));
                }
                schedulingCfg.tripEBs.push_back(tmp);
            }

            vector<string> chargeCfgNums = tmpScenarioCfg->at(OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB));
            networkCfg.chargeStationNum = stoi(chargeCfgNums[0]);
            for (int i = 0; i < networkCfg.chargeStationNum; i++) {
                vector<string> chargerCfgNums = tmpScenarioCfg->at(OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB) + 1 + i);
                networkCfg.chargerNums[stoi(chargerCfgNums[0])] = stoi(chargerCfgNums[1]);
                networkCfg.chargerPhys.insert(stoi(chargerCfgNums[0]));
            }

            const size_t routeCfgOffset = static_cast<size_t>(
                OFFSET_SCENARIO_CHARGER_CFG(schedulingCfg.numEB) + 1 + networkCfg.chargeStationNum);
            schedulingCfg.numRoutes = stoi(tmpScenarioCfg->at(routeCfgOffset)[0]);
            schedulingCfg.routeNames = tmpScenarioCfg->at(routeCfgOffset + 1);

            const auto& routeIdTokens = tmpScenarioCfg->at(routeCfgOffset + 2);
            schedulingCfg.routeIdByEBs.reserve(routeIdTokens.size());
            for (const auto& token : routeIdTokens) {
                schedulingCfg.routeIdByEBs.push_back(stoi(token));
            }
        }

        return true;
    }
};

static vector<int> CountEBsByRoute(const SchedulingCfg& schedulingCfg)
{
    vector<int> routeEBCounts(static_cast<size_t>(schedulingCfg.numRoutes), 0);
    for (int ebIdx = 0; ebIdx < schedulingCfg.numEB; ++ebIdx) {
        const int routeId = schedulingCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
        routeEBCounts.at(static_cast<size_t>(routeId)) += 1;
    }
    return routeEBCounts;
}

static vector<vector<vector<string>>> BuildRouteFacilityVarNames(
    const string& dataFolder, const vector<WirelessScenarioConfig>& scenarios,
    const vector<ProblemDataVar>& masterVars)
{
    unordered_set<string> masterFacilityNames;
    masterFacilityNames.reserve(masterVars.size());
    for (const auto& var : masterVars) {
        if (var.name.rfind("w_station_", 0) == 0 || var.name.rfind("w_link_", 0) == 0) {
            masterFacilityNames.insert(var.name);
        }
    }

    vector<vector<vector<string>>> routeFacilityVarNames;
    routeFacilityVarNames.reserve(scenarios.size());
    for (const auto& scenario : scenarios) {
        ModelData scenarioData(dataFolder, scenario);
        const SchedulingCfg& schedulingCfg = scenarioData.GetSchedulingCfg();

        vector<unordered_set<string>> routeFacilitySets(static_cast<size_t>(schedulingCfg.numRoutes));
        for (int ebIdx = 0; ebIdx < schedulingCfg.numEB; ++ebIdx) {
            const int routeId = schedulingCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
            auto& facilitySet = routeFacilitySets.at(static_cast<size_t>(routeId));
            const auto& trips = schedulingCfg.tripEBs.at(static_cast<size_t>(ebIdx));

            for (size_t tripIdx = 0; tripIdx < trips.size(); ++tripIdx) {
                const int trip = trips[tripIdx];
                const auto& stations = schedulingCfg.stationTrips.at(static_cast<size_t>(trip));

                for (size_t stationIdx = 0; stationIdx < stations.size(); ++stationIdx) {
                    const string stationVarName = "w_station_s" + to_string(stations[stationIdx]);
                    if (masterFacilityNames.count(stationVarName) > 0) {
                        facilitySet.insert(stationVarName);
                    }

                    int tripArr = trip;
                    size_t stationArrIdx = stationIdx + 1;
                    if (stationIdx + 1 == stations.size()) {
                        if (tripIdx + 1 == trips.size()) {
                            continue;
                        }
                        tripArr = trips[tripIdx + 1];
                        stationArrIdx = 0;
                    }

                    const int stationArr = schedulingCfg.stationTrips
                        .at(static_cast<size_t>(tripArr)).at(stationArrIdx);
                    const string linkVarName = "w_link_l" + to_string(stations[stationIdx])
                        + "To" + to_string(stationArr);
                    if (masterFacilityNames.count(linkVarName) > 0) {
                        facilitySet.insert(linkVarName);
                    }
                }
            }
        }

        vector<vector<string>> scenarioRouteFacilities(routeFacilitySets.size());
        for (size_t routeId = 0; routeId < routeFacilitySets.size(); ++routeId) {
            scenarioRouteFacilities[routeId].assign(
                routeFacilitySets[routeId].begin(), routeFacilitySets[routeId].end());
            sort(scenarioRouteFacilities[routeId].begin(), scenarioRouteFacilities[routeId].end());
        }
        routeFacilityVarNames.push_back(std::move(scenarioRouteFacilities));
    }
    return routeFacilityVarNames;
}

static vector<ProblemDataVar> BuildWirelessMasterVars(const string& dataFolder, const vector<WirelessScenarioConfig>& scenarios)
{
    // Use first scenario for facility variables (shared across scenarios)
    ModelData firstScenarioData(dataFolder, scenarios.front());
    const ChargerCfg& chargerCfg = firstScenarioData.GetChargerCfg();
    const NetworkCfg& networkCfg = firstScenarioData.GetNetworkCfg();
    const OperationCfg& operationCfg = firstScenarioData.GetOperationCfg();

    vector<ProblemDataVar> masterVars;

    // Station variables (shared across scenarios)
    vector<int> stations(networkCfg.stationPhys.begin(), networkCfg.stationPhys.end());
    sort(stations.begin(), stations.end());
    const double stationCost =
        (60 * chargerCfg.priceOfInverterPerkW * chargerCfg.chargeRate[1] + chargerCfg.priceOfUnitCharger[1]) * 2;
    for (int station : stations) {
        if (networkCfg.chargerPhys.count(station)) {
            continue;
        }

        ProblemDataVar var;
        var.lb = 0.0;
        var.ub = 1.0;
        var.obj = stationCost;
        var.type = GRB_BINARY;
        var.name = "w_station_s" + to_string(station);
        masterVars.push_back(var);
    }

    // Link variables (shared across scenarios)
    vector<pair<int, int>> links;
    for (const auto& [start, endMap] : networkCfg.linkDistances) {
        for (const auto& [end, _] : endMap) {
            links.emplace_back(start, end);
        }
    }
    sort(links.begin(), links.end());

    for (const auto& [start, end] : links) {
        const double dist = networkCfg.linkDistances.at(start).at(end);
        ProblemDataVar var;
        var.lb = 0.0;
        var.ub = 1.0;
        var.obj = chargerCfg.priceOfUnitCharger[2] * dist +
            60 * chargerCfg.priceOfInverterPerkW * chargerCfg.chargeRate[2];
        var.type = GRB_BINARY;
        var.name = "w_link_l" + to_string(start) + "To" + to_string(end);
        masterVars.push_back(var);
    }

    // Battery capacity variables (scenario-based)
    for (size_t scIdx = 0; scIdx < scenarios.size(); ++scIdx) {
        ModelData scenarioData(dataFolder, scenarios[scIdx]);
        const SchedulingCfg& schedulingCfg = scenarioData.GetSchedulingCfg();
        const vector<int> routeEBCounts = CountEBsByRoute(schedulingCfg);
        const double probability = scenarios[scIdx].probability;

        for (int routeId = 0; routeId < schedulingCfg.numRoutes; ++routeId) {
            const double costCoeff = static_cast<double>(routeEBCounts[routeId]) *
                operationCfg.priceOfUnitBat * probability;
            for (int capacity = static_cast<int>(operationCfg.capacityMin);
                 capacity <= static_cast<int>(operationCfg.capacityMax); capacity += 10) {
                ProblemDataVar var;
                var.lb = 0.0;
                var.ub = 1.0;
                var.obj = costCoeff * static_cast<double>(capacity);
                var.type = GRB_BINARY;
                var.name = "sc" + to_string(scIdx) + "_E_r" + to_string(routeId) + "_cap" + to_string(capacity);
                masterVars.push_back(var);
            }
        }
    }

    return masterVars;
}

static void PrintWirelessScenarioSummary(const vector<WirelessScenarioConfig>& scenarios)
{
    std::cout << "scenario_count=" << scenarios.size() << std::endl;
    for (size_t idx = 0; idx < scenarios.size(); ++idx) {
        std::cout << "scenario_" << idx
                  << " probability=" << scenarios[idx].probability
                  << " folder=" << scenarios[idx].dataFolder << std::endl;
    }
}

// Energy budget per (scenario, route): for the EB with the most trips, compute
//   totalC  = fixed energy consumed (kWh) — start + inter-station + end
//   fastChargeFree = max charge from always-available fast chargers (kWh)
//   facCharges[f] = (facilityVarName, max charge kWh) for each wireless facility visited
//
// Valid inequality (facility-aware):
//   socDelta * sum(cap * z_cap) + sum(facCharge_f * w_f) >= totalC - fastChargeFree
//
// In perStation mode, only the route's bound chargerPhys counts as fast-charge-free;
// other chargerPhys stations contribute nothing (their charge==0 in the subproblem).
static void ComputeRouteEnergyBudget(
    const string& dataFolder,
    const vector<WirelessScenarioConfig>& scenarios,
    const vector<vector<int>>* routeChargerStation,
    vector<vector<double>>& outTotalC,
    vector<vector<double>>& outFastChargeFree,
    vector<vector<vector<string>>>& outFacNames,
    vector<vector<vector<double>>>& outFacCharges)
{
    const size_t nSc = scenarios.size();
    outTotalC.resize(nSc);
    outFastChargeFree.resize(nSc);
    outFacNames.resize(nSc);
    outFacCharges.resize(nSc);

    for (size_t scIdx = 0; scIdx < nSc; ++scIdx) {
        ModelData md(dataFolder, scenarios[scIdx]);
        const auto& sc = md.GetSchedulingCfg();
        const auto& oc = md.GetOperationCfg();
        const auto& cc = md.GetChargerCfg();
        const auto& nc = md.GetNetworkCfg();

        const double cPerKm = oc.curbWeight * oc.mu * oc.g / 3.6;
        const int nRoutes = sc.numRoutes;
        outTotalC[scIdx].resize(nRoutes, 0.0);
        outFastChargeFree[scIdx].resize(nRoutes, 0.0);
        outFacNames[scIdx].resize(nRoutes);
        outFacCharges[scIdx].resize(nRoutes);

        for (int r = 0; r < nRoutes; ++r) {
            // Bound chargerPhys station for per-station mode
            int bound = -1;
            if (routeChargerStation && scIdx < routeChargerStation->size()
                && r < static_cast<int>((*routeChargerStation)[scIdx].size()))
                bound = (*routeChargerStation)[scIdx][r];

            // Find EB with most trips on this route
            int worstEB = -1, maxTrips = -1;
            for (int eb = 0; eb < sc.numEB; ++eb) {
                if (sc.routeIdByEBs[eb] == r && sc.tripNumEBs[eb] > maxTrips) {
                    maxTrips = sc.tripNumEBs[eb];
                    worstEB = eb;
                }
            }
            if (worstEB < 0) continue;

            const auto& trips = sc.tripEBs[worstEB];
            double totalC = nc.startDistanceEBs[worstEB] * cPerKm;
            double fastFree = 0.0;
            // Facility name -> accumulated max charge (summed over all visits/traversals)
            unordered_map<string, double> facAcc;

            for (size_t ti = 0; ti < trips.size(); ++ti) {
                int trip = trips[ti];
                const auto& stations = sc.stationTrips[trip];
                for (size_t si = 0; si < stations.size(); ++si) {
                    int station = stations[si];
                    int arrTime = sc.stationArrTimes[trip][si];
                    int depTime = sc.stationDepTimes[trip][si];
                    int stayTime = depTime - arrTime;

                    if (nc.chargerPhys.count(station)) {
                        // Fast charger is available when this station is in the active subproblem scope.
                        if (!routeChargerStation || station == bound)
                            fastFree += cc.chargeRate[0] * stayTime * cc.chargeEfficiency[0];
                    } else {
                        // Static wireless: depends on w_station_s{station}
                        string facName = "w_station_s" + to_string(station);
                        facAcc[facName] += cc.chargeRate[1] * stayTime * cc.chargeEfficiency[1];
                    }

                    // Inter-station movement
                    int nextTrip, nextStation, nextArrTime;
                    if (si + 1 < stations.size()) {
                        nextTrip = trip;
                        nextStation = stations[si + 1];
                        nextArrTime = sc.stationArrTimes[nextTrip][si + 1];
                    } else if (ti + 1 < trips.size()) {
                        nextTrip = trips[ti + 1];
                        nextStation = sc.stationTrips[nextTrip][0];
                        nextArrTime = sc.stationArrTimes[nextTrip][0];
                    } else {
                        continue;
                    }

                    double dist = nc.linkDistances.at(station).at(nextStation);
                    totalC += dist * cPerKm;

                    // Dynamic wireless on link: depends on w_link_l{from}To{to}
                    int travelTime = nextArrTime - depTime;
                    string linkName = "w_link_l" + to_string(station) + "To" + to_string(nextStation);
                    facAcc[linkName] += cc.chargeRate[2] * travelTime * cc.chargeEfficiency[2];
                }
            }
            totalC += nc.endDistanceEBs[worstEB] * cPerKm;

            outTotalC[scIdx][r] = totalC;
            outFastChargeFree[scIdx][r] = fastFree;
            for (const auto& [name, charge] : facAcc) {
                outFacNames[scIdx][r].push_back(name);
                outFacCharges[scIdx][r].push_back(charge);
            }
        }
    }
}

static void InitWirelessMasterData(ProblemData& data, const string& dataFolder, const string& usageLabel,
    bool perStationMode = false)
{
    vector<WirelessScenarioConfig> scenarios = LoadWirelessScenarioConfigs(dataFolder);
    vector<ProblemDataVar> masterVars = BuildWirelessMasterVars(dataFolder, scenarios);

    data.addData("masterVars", masterVars);
    data.addData("wireless_data_root_folder", dataFolder);
    data.addData("wireless_scenarios", scenarios);
    data.addData("wireless_scenario_count", static_cast<int>(scenarios.size()));

    vector<int> routeCounts;
    vector<vector<int>> routeIdByEBsPerScenario;
    vector<double> scenarioProbabilities;
    routeCounts.reserve(scenarios.size());
    routeIdByEBsPerScenario.reserve(scenarios.size());
    scenarioProbabilities.reserve(scenarios.size());

    // Build ModelData cache once per scenario (avoids repeated file I/O below)
    vector<ModelData> modelDataCache;
    modelDataCache.reserve(scenarios.size());
    for (const auto& scenario : scenarios) {
        modelDataCache.emplace_back(dataFolder, scenario);
        const SchedulingCfg& schedulingCfg = modelDataCache.back().GetSchedulingCfg();
        routeCounts.push_back(schedulingCfg.numRoutes);
        routeIdByEBsPerScenario.push_back(schedulingCfg.routeIdByEBs);
        scenarioProbabilities.push_back(scenario.probability);
    }
    data.addData("wireless_route_counts", routeCounts);
    data.addData("wireless_routeIdByEBs", routeIdByEBsPerScenario);
    data.addData("wireless_scenario_probabilities", scenarioProbabilities);
    data.addData("wireless_route_facility_var_names",
        BuildRouteFacilityVarNames(dataFolder, scenarios, masterVars));
    data.addData("wireless_model_data_cache", modelDataCache);

    // --- Paper2: Build station-group info ---
    // Build route→chargerStation assignment: first chargerPhys station on each route
    vector<vector<int>> routeChargerStation(scenarios.size());
    for (size_t scIdx = 0; scIdx < scenarios.size(); ++scIdx) {
        const auto& scenarioData = modelDataCache[scIdx];
        const auto& schedCfg = scenarioData.GetSchedulingCfg();
        const auto& netCfg = scenarioData.GetNetworkCfg();
        routeChargerStation[scIdx].assign(static_cast<size_t>(schedCfg.numRoutes), -1);
        unordered_map<int, int> routeFirstStation;
        for (int ebIdx = 0; ebIdx < schedCfg.numEB; ++ebIdx) {
            int routeId = schedCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
            if (routeFirstStation.count(routeId)) continue;
            for (int tripIdx = 0; tripIdx < schedCfg.tripEBs[ebIdx].size(); ++tripIdx) {
                int trip = schedCfg.tripEBs[ebIdx][tripIdx];
                for (int sIdx = 0; sIdx < schedCfg.stationTrips[trip].size(); ++sIdx) {
                    int station = schedCfg.stationTrips[trip][sIdx];
                    if (netCfg.chargerPhys.count(station)) {
                        routeFirstStation[routeId] = station;
                        break;
                    }
                }
                if (routeFirstStation.count(routeId)) break;
            }
        }
        for (const auto& [routeId, station] : routeFirstStation) {
            routeChargerStation[scIdx][static_cast<size_t>(routeId)] = station;
        }
    }
    data.addData("wireless_route_charger_station", routeChargerStation);

    // Build station groups
    vector<StationGroupInfo> stationGroups;
    for (size_t scIdx = 0; scIdx < scenarios.size(); ++scIdx) {
        const auto& scenarioData = modelDataCache[scIdx];
        const auto& schedCfg = scenarioData.GetSchedulingCfg();
        const auto& netCfg = scenarioData.GetNetworkCfg();

        if (perStationMode) {
            // Per-station mode: each chargerPhys station = one group
            for (int station : netCfg.chargerPhys) {
                StationGroupInfo info;
                info.stationGroupId = station;
                info.scenarioIndex = static_cast<int>(scIdx);
                for (int routeId = 0; routeId < schedCfg.numRoutes; ++routeId) {
                    if (routeChargerStation[scIdx][static_cast<size_t>(routeId)] == station)
                        info.routeIds.push_back(routeId);
                }
                if (info.routeIds.empty()) continue;
                for (int ebIdx = 0; ebIdx < schedCfg.numEB; ++ebIdx) {
                    int routeId = schedCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
                    for (int r : info.routeIds) {
                        if (r == routeId) { info.ebIndices.push_back(ebIdx); break; }
                    }
                }
                stationGroups.push_back(info);
            }
        } else {
            // Union-find: merge stations that share routes
            unordered_map<int, unordered_set<int>> stationRoutes;
            for (int ebIdx = 0; ebIdx < schedCfg.numEB; ++ebIdx) {
                int routeId = schedCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
                for (int tripIdx = 0; tripIdx < schedCfg.tripEBs[ebIdx].size(); ++tripIdx) {
                    int trip = schedCfg.tripEBs[ebIdx][tripIdx];
                    for (int sIdx = 0; sIdx < schedCfg.stationTrips[trip].size(); ++sIdx) {
                        int station = schedCfg.stationTrips[trip][sIdx];
                        if (netCfg.chargerPhys.count(station))
                            stationRoutes[station].insert(routeId);
                    }
                }
            }
            vector<int> stationList;
            for (const auto& [station, _] : stationRoutes) stationList.push_back(station);
            sort(stationList.begin(), stationList.end());
            unordered_map<int, int> parent;
            for (int s : stationList) parent[s] = s;
            function<int(int)> find = [&](int x) -> int {
                if (parent[x] != x) parent[x] = find(parent[x]);
                return parent[x];
            };
            auto unite = [&](int a, int b) {
                int ra = find(a), rb = find(b);
                if (ra != rb) parent[ra] = rb;
            };
            unordered_map<int, unordered_set<int>> stationToRoutes;
            for (const auto& [station, routes] : stationRoutes) stationToRoutes[station] = routes;
            for (size_t i = 0; i < stationList.size(); ++i) {
                for (size_t j = i + 1; j < stationList.size(); ++j) {
                    int sa = stationList[i], sb = stationList[j];
                    for (int r : stationToRoutes[sa]) {
                        if (stationToRoutes[sb].count(r)) { unite(sa, sb); break; }
                    }
                }
            }
            unordered_map<int, unordered_set<int>> componentRoutes;
            for (int s : stationList) {
                int root = find(s);
                for (int r : stationToRoutes[s]) componentRoutes[root].insert(r);
            }
            for (const auto& [root, routes] : componentRoutes) {
                StationGroupInfo info;
                info.stationGroupId = root;
                info.scenarioIndex = static_cast<int>(scIdx);
                info.routeIds.assign(routes.begin(), routes.end());
                for (int ebIdx = 0; ebIdx < schedCfg.numEB; ++ebIdx) {
                    int routeId = schedCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
                    if (routes.count(routeId)) info.ebIndices.push_back(ebIdx);
                }
                stationGroups.push_back(info);
            }
        }
    }
    data.addData("wireless_station_groups", stationGroups);
    data.addData("wireless_per_station_mode", perStationMode);

    // Valid inequality: facility-aware energy budget from worst-EB on each route
    // Only active for per-station mode where the route→charger station binding is known.
    // The main lifted variants use the reference logic without this inequality.
    if (perStationMode) {
        vector<vector<double>> ebTotalC, ebFastFree;
        vector<vector<vector<string>>> ebFacNames;
        vector<vector<vector<double>>> ebFacCharges;
        ComputeRouteEnergyBudget(dataFolder, scenarios,
            &routeChargerStation,
            ebTotalC, ebFastFree, ebFacNames, ebFacCharges);
        data.addData("wireless_eb_totalC", ebTotalC);
        data.addData("wireless_eb_fastFree", ebFastFree);
        data.addData("wireless_eb_facNames", ebFacNames);
        data.addData("wireless_eb_facCharges", ebFacCharges);
    }

    std::cout << "Loaded wireless charging data for " << usageLabel << "." << std::endl;
    PrintWirelessScenarioSummary(scenarios);
    std::cout << "masterVars=" << masterVars.size() << std::endl;
}

static std::vector<ProblemDataConstr> BuildWirelessMasterConstrs(const ProblemData& data)
{
    const auto& masterVars = data.getData<std::vector<ProblemDataVar>>("masterVars");
    const auto& scenarios = data.getData<std::vector<WirelessScenarioConfig>>("wireless_scenarios");
    const string& dataFolder = data.getData<std::string>("wireless_data_root_folder");

    // Build masterIdxByName for looking up variable indices
    unordered_map<string, size_t> masterIdxByName;
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        masterIdxByName[masterVars[idx].name] = idx;
    }

    vector<ProblemDataConstr> constrs;

    // Read energy-budget valid inequality data, if available
    vector<vector<double>> ebTotalC, ebFastFree;
    vector<vector<vector<string>>> ebFacNames;
    vector<vector<vector<double>>> ebFacCharges;
    try { ebTotalC = data.getData<vector<vector<double>>>("wireless_eb_totalC"); }
    catch (...) {}
    if (!ebTotalC.empty()) {
        ebFastFree = data.getData<vector<vector<double>>>("wireless_eb_fastFree");
        ebFacNames = data.getData<vector<vector<vector<string>>>>("wireless_eb_facNames");
        ebFacCharges = data.getData<vector<vector<vector<double>>>>("wireless_eb_facCharges");
    }

    // For each scenario and each route, add "select exactly one capacity" constraint
    const auto& modelDataCache = data.getData<vector<ModelData>>("wireless_model_data_cache");
    for (size_t scIdx = 0; scIdx < scenarios.size(); ++scIdx) {
        const auto& scenarioData = modelDataCache[scIdx];
        const SchedulingCfg& schedulingCfg = scenarioData.GetSchedulingCfg();
        const OperationCfg& operationCfg = scenarioData.GetOperationCfg();
        const double socDelta = operationCfg.socMax - operationCfg.socMin;

        for (int routeId = 0; routeId < schedulingCfg.numRoutes; ++routeId) {
            ProblemDataConstr constr;
            constr.coeffs.assign(masterVars.size(), 0.0);
            constr.sense = '=';
            constr.rhs = 1.0;
            constr.name = "selectCap_sc" + to_string(scIdx) + "_r" + to_string(routeId);

            for (int capacity = static_cast<int>(operationCfg.capacityMin);
                 capacity <= static_cast<int>(operationCfg.capacityMax); capacity += 10) {
                string varName = "sc" + to_string(scIdx) + "_E_r" + to_string(routeId) + "_cap" + to_string(capacity);
                auto it = masterIdxByName.find(varName);
                if (it != masterIdxByName.end()) {
                    constr.coeffs[it->second] = 1.0;
                }
            }

            constrs.push_back(constr);

            // --- Facility-aware energy budget valid inequality ---
            // socDelta * sum(cap * z_cap) + sum(charge_f * w_f) >= totalC - fastChargeFree
            if (!ebTotalC.empty() && scIdx < ebTotalC.size()
                && routeId < static_cast<int>(ebTotalC[scIdx].size())) {
                double totalC = ebTotalC[scIdx][routeId];
                double fastFree = ebFastFree[scIdx][routeId];
                double rhs = totalC - fastFree;
                if (rhs > 1e-9) {
                    ProblemDataConstr ebConstr;
                    ebConstr.coeffs.assign(masterVars.size(), 0.0);
                    ebConstr.sense = '>';
                    ebConstr.rhs = rhs;
                    ebConstr.name = "energyBudget_sc" + to_string(scIdx) + "_r" + to_string(routeId);

                    // Capacity variables: coeff = cap * socDelta
                    for (int cap = static_cast<int>(operationCfg.capacityMin);
                         cap <= static_cast<int>(operationCfg.capacityMax); cap += 10) {
                        string vn = "sc" + to_string(scIdx) + "_E_r" + to_string(routeId)
                                  + "_cap" + to_string(cap);
                        auto it = masterIdxByName.find(vn);
                        if (it != masterIdxByName.end())
                            ebConstr.coeffs[it->second] = static_cast<double>(cap) * socDelta;
                    }

                    // Facility variables: coeff = accumulated max charge at this facility
                    const auto& names = ebFacNames[scIdx][routeId];
                    const auto& charges = ebFacCharges[scIdx][routeId];
                    for (size_t fi = 0; fi < names.size(); ++fi) {
                        auto it = masterIdxByName.find(names[fi]);
                        if (it != masterIdxByName.end())
                            ebConstr.coeffs[it->second] += charges[fi];
                    }

                    constrs.push_back(ebConstr);
                }
            }
        }
    }

    return constrs;
}
static void BuildWirelessNoGoodFeasibilityCut(
    const vector<double>& zValues, LShapeCutInfo& cutInfo)
{
    int selectedCount = 0;
    for (double value : zValues) {
        if (value > 0.5) {
            ++selectedCount;
        }
    }

    cutInfo.yCoeffs.assign(zValues.size(), 0.0);
    for (size_t i = 0; i < zValues.size(); ++i) {
        cutInfo.yCoeffs[i] = (zValues[i] > 0.5) ? -1.0 : 1.0;
    }
    cutInfo.rhs = 1.0 - static_cast<double>(selectedCount);
    cutInfo.isOptimalityCut = false;
    cutInfo.sense = '>';
    cutInfo.constant = 0.0;
}

static bool BuildWirelessOperationalSubproblem(const string& dataRootFolder, const WirelessScenarioConfig& scenario,
    size_t scenarioIdx,
    const vector<ProblemDataVar>& masterVars, GRBModel& model,
    LShapeSubProblemContext& context,
    GRBLinExpr& aggregateObj, size_t& facilityRhsSerial,
    vector<BatteryRhsConstr>& batteryRhsOut,
    vector<int>& routeIdByEBOut,
    const vector<int>* activeEbIndices = nullptr,
    const vector<int>* routeChargerStation = nullptr,
    bool relaxChargerCapacity = false,
    const ModelData* cachedData = nullptr)
{
    ModelData data = cachedData ? *cachedData : ModelData(dataRootFolder, scenario);

    OperationCfg operationCfg = data.GetOperationCfg();
    ChargerCfg chargerCfg = data.GetChargerCfg();
    SchedulingCfg schedulingCfg = data.GetSchedulingCfg();
    NetworkCfg networkCfg = data.GetNetworkCfg();

    unordered_map<string, size_t> masterIdxByName;
    masterIdxByName.reserve(masterVars.size());
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        masterIdxByName[masterVars[idx].name] = idx;
    }

    auto& facilityRhsConstrs = context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup);
    const string namePrefix = "sc" + to_string(scenarioIdx) + "_";

    // Output routeIdByEB for the caller
    routeIdByEBOut = schedulingCfg.routeIdByEBs;

    // Battery RHS constraints: routeE is computed from master zValues and set as RHS.
    size_t batteryRhsSerial = 0;

    vector<GRBVar> chargeNight_b; // [eb]
    vector<vector<unordered_map<int, GRBVar>>> charge_bns; // [eb][trip][station]
    vector<vector<unordered_map<int, unordered_map<int, GRBVar>>>> charge_bnl; // [eb][trip][start][end]
    vector<vector<vector<GRBVar>>> energyDep_bns; // [eb][trip][station]
    vector<vector<vector<GRBVar>>> energyArr_bns; // [eb][trip][station]

    size_t numEB = schedulingCfg.numEB;

    // Filter EBs: only include those belonging to this station group (if specified)
    vector<int> ebIndices;
    if (activeEbIndices != nullptr) {
        ebIndices = *activeEbIndices;
    } else {
        ebIndices.reserve(numEB);
        for (int ebIdx = 0; ebIdx < static_cast<int>(numEB); ++ebIdx) {
            ebIndices.push_back(ebIdx);
        }
    }

    chargeNight_b.reserve(numEB);
    for (int eb : schedulingCfg.EBs) {
        string varName = namePrefix + "c_Night_b" + to_string(eb);
        chargeNight_b.push_back(model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, varName));
    }

    charge_bns.reserve(numEB);
    energyDep_bns.reserve(numEB);
    energyArr_bns.reserve(numEB);

    for (int ebIdx = 0; ebIdx < numEB; ebIdx++) {
        int eb = schedulingCfg.EBs[ebIdx];
        size_t numEBTrip = schedulingCfg.tripEBs[ebIdx].size();
        vector<unordered_map<int, GRBVar>> charge_b;
        vector<vector<GRBVar>> energyDep_b;
        vector<vector<GRBVar>> energyArr_b;
        charge_b.reserve(numEBTrip);
        energyDep_b.reserve(numEBTrip);
        energyArr_b.reserve(numEBTrip);

        for (int tripIdx = 0; tripIdx < numEBTrip; tripIdx++) {
            int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
            size_t numEBTripStations = schedulingCfg.stationTrips[trip].size();
            unordered_map<int, GRBVar> charge_bn;
            vector<GRBVar> energyDep_bn;
            vector<GRBVar> energyArr_bn;
            energyDep_bn.reserve(numEBTripStations);
            energyArr_bn.reserve(numEBTripStations);

            for (int stationIdx = 0; stationIdx < numEBTripStations; stationIdx++) {
                int station = schedulingCfg.stationTrips[trip][stationIdx];
                string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station) + "_i" + to_string(stationIdx);
                energyDep_bn.push_back(model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, namePrefix + "e_Dep" + sub));
                energyArr_bn.push_back(model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, namePrefix + "e_Arr" + sub));
                charge_bn[station] = model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, namePrefix + "c" + sub);
            }
            charge_b.push_back(charge_bn);
            energyDep_b.push_back(energyDep_bn);
            energyArr_b.push_back(energyArr_bn);
        }
        charge_bns.push_back(charge_b);
        energyDep_bns.push_back(energyDep_b);
        energyArr_bns.push_back(energyArr_b);
    }

    // dynamic wireless charging
    charge_bnl.reserve(numEB);
    for (int ebIdx = 0; ebIdx < numEB; ebIdx++) {
        auto eb = schedulingCfg.EBs[ebIdx];
        size_t numEBTrip = schedulingCfg.tripEBs[ebIdx].size();
        vector<unordered_map<int, unordered_map<int, GRBVar>>> charge_b;
        charge_b.reserve(numEBTrip);
        for (int tripIdx = 0; tripIdx < numEBTrip; tripIdx++) {
            auto tripDep = schedulingCfg.tripEBs[ebIdx][tripIdx];
            size_t numEBTripStations = schedulingCfg.stationTrips[tripDep].size();
            unordered_map<int, unordered_map<int, GRBVar>> charge_bn;
            for (int stationIdx = 0; stationIdx < numEBTripStations; stationIdx++) {
                int stationDep = schedulingCfg.stationTrips[tripDep][stationIdx];
                int tripArr;
                int stationArr;
                int stationArrIdx;
                if (stationIdx == schedulingCfg.stationTrips[tripDep].size() - 1) {
                    if (tripIdx == schedulingCfg.tripEBs[ebIdx].size() - 1) {
                        continue;
                    }
                    tripArr = schedulingCfg.tripEBs[ebIdx][tripIdx + 1];
                    stationArrIdx = 0;
                    stationArr = schedulingCfg.stationTrips[tripArr][stationArrIdx];
                } else {
                    tripArr = tripDep;
                    stationArrIdx = stationIdx + 1;
                    stationArr = schedulingCfg.stationTrips[tripArr][stationArrIdx];
                }

                string sub = "_b" + to_string(eb) + "_n" + to_string(tripDep) + "_s" + to_string(stationDep) +
                             "_i" + to_string(stationIdx) + "To_n" + to_string(tripArr) + "_s" + to_string(stationArr) + "_i" + to_string(stationArrIdx);
                charge_bn[stationDep][stationArr] = model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, namePrefix + "c" + sub);
            }
            charge_b.push_back(charge_bn);
        }
        charge_bnl.push_back(charge_b);
    }

    model.update();

    // Per-station mode: prohibit charging at non-bound chargerPhys stations
    if (routeChargerStation) {
        for (int ebIdx : ebIndices) {
            int routeId = schedulingCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
            int boundStation = (*routeChargerStation)[static_cast<size_t>(routeId)];
            for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); ++tripIdx) {
                int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
                for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[trip].size(); ++stationIdx) {
                    int station = schedulingCfg.stationTrips[trip][stationIdx];
                    if (networkCfg.chargerPhys.count(station) && station != boundStation) {
                        model.addConstr(charge_bns[ebIdx][tripIdx].at(station) == 0.0,
                            namePrefix + "noCharge_b" + to_string(schedulingCfg.EBs[ebIdx])
                            + "_n" + to_string(trip) + "_s" + to_string(station));
                    }
                }
            }
        }
    }

    // objective function
    GRBLinExpr obj = 0;

    GRBLinExpr sum_day = 0;
    for (int ebIdx : ebIndices) {
        for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
            for (const auto& [_, chargeVar] : charge_bns[ebIdx][tripIdx]) {
                sum_day += chargeVar;
            }
        }
    }

    for (int ebIdx : ebIndices) {
        size_t numEBTrip = schedulingCfg.tripEBs[ebIdx].size();
        for (int tripIdx = 0; tripIdx < numEBTrip; tripIdx++) {
            int tripDep = schedulingCfg.tripEBs[ebIdx][tripIdx];
            size_t numEBTripStations = schedulingCfg.stationTrips[tripDep].size();
            for (int stationIdx = 0; stationIdx < numEBTripStations; stationIdx++) {
                int stationDep = schedulingCfg.stationTrips[tripDep][stationIdx];
                int tripArr;
                int stationArr;
                if (stationIdx == numEBTripStations - 1) {
                    if (tripIdx == numEBTrip - 1) {
                        continue;
                    }
                    tripArr = schedulingCfg.tripEBs[ebIdx][tripIdx + 1];
                    stationArr = schedulingCfg.stationTrips[tripArr][0];
                } else {
                    tripArr = tripDep;
                    stationArr = schedulingCfg.stationTrips[tripArr][stationIdx + 1];
                }

                sum_day += charge_bnl[ebIdx][tripIdx][stationDep][stationArr];
            }
        }
    }

    GRBLinExpr sum_night = 0;
    for (int ebIdx : ebIndices) {
        sum_night += chargeNight_b[ebIdx];
    }

    obj += operationCfg.priceOfPowerDay * sum_day + operationCfg.priceOfPowerNight * sum_night;

    // Battery cost is in the master problem, not in sub-problem objective.

    // energy range — battery capacity (routeE) passed via RHS constraints
    const double socMax = operationCfg.socMax;
    const double socMin = operationCfg.socMin;
    for (int ebIdx : ebIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        const int routeId = schedulingCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
        size_t numEBTrip = schedulingCfg.tripEBs[ebIdx].size();
        for (int tripIdx = 0; tripIdx < numEBTrip; tripIdx++) {
            int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
            size_t numEBTripStations = schedulingCfg.stationTrips[trip].size();
            for (int stationIdx = 0; stationIdx < numEBTripStations; stationIdx++) {
                int station = schedulingCfg.stationTrips[trip][stationIdx];
                string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station) + "_i" + to_string(stationIdx);
                GRBVar arrE = energyArr_bns[ebIdx][tripIdx][stationIdx];
                GRBVar depE = energyDep_bns[ebIdx][tripIdx][stationIdx];

                // arrE >= routeE * socMin  →  arrE >= 0, RHS = socMin * routeE
                batteryRhsOut.push_back({model.addConstr(arrE >= 0.0,
                    MakeWirelessBatteryRhsConstrName(routeId, batteryRhsSerial++)), routeId, socMin, 0.0});
                // arrE <= routeE * socMax  →  arrE <= 0, RHS = socMax * routeE
                batteryRhsOut.push_back({model.addConstr(arrE <= 0.0,
                    MakeWirelessBatteryRhsConstrName(routeId, batteryRhsSerial++)), routeId, socMax, 0.0});
                // depE >= routeE * socMin  →  depE >= 0, RHS = socMin * routeE
                batteryRhsOut.push_back({model.addConstr(depE >= 0.0,
                    MakeWirelessBatteryRhsConstrName(routeId, batteryRhsSerial++)), routeId, socMin, 0.0});
                // depE <= routeE * socMax  →  depE <= 0, RHS = socMax * routeE
                batteryRhsOut.push_back({model.addConstr(depE <= 0.0,
                    MakeWirelessBatteryRhsConstrName(routeId, batteryRhsSerial++)), routeId, socMax, 0.0});
            }
        }
    }

    // --- Fast-charging station constraints ---
    int boundStation = -1;
    if (routeChargerStation && !ebIndices.empty()) {
        int firstRouteId = schedulingCfg.routeIdByEBs.at(static_cast<size_t>(ebIndices[0]));
        boundStation = (*routeChargerStation)[static_cast<size_t>(firstRouteId)];
    }

    if (!relaxChargerCapacity) {
        // Full model: time-indexed charger occupancy (EBs compete for charger slots)
        unordered_map<int, unordered_map<int, unordered_map<int, unordered_map<int, GRBVar>>>> x_bns_t;
        unordered_map<int, vector<GRBLinExpr>> chargerOccupy;
        for (int station : networkCfg.chargerPhys) {
            if (routeChargerStation && station != boundStation) continue;
            chargerOccupy[station].resize(static_cast<size_t>(operationCfg.timeMax - operationCfg.timeMin));
        }

        for (int ebIdx : ebIndices) {
            int eb = schedulingCfg.EBs[ebIdx];
            for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
                int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
                for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[trip].size(); stationIdx++) {
                    int station = schedulingCfg.stationTrips[trip][stationIdx];
                    if (!networkCfg.chargerPhys.count(station)) continue;
                    if (routeChargerStation && station != boundStation) continue;
                    int arrTime = schedulingCfg.stationArrTimes[trip][stationIdx];
                    int depTime = schedulingCfg.stationDepTimes[trip][stationIdx];
                    for (int time = arrTime; time < depTime; time++) {
                        x_bns_t[eb][trip][station][time] = model.addVar(0, 1, 0, GRB_BINARY,
                            namePrefix + "x_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station)
                            + "_i" + to_string(stationIdx) + "_t" + to_string(time));
                    }
                }
            }
        }

        for (int ebIdx : ebIndices) {
            int eb = schedulingCfg.EBs[ebIdx];
            for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
                int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
                for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[trip].size(); stationIdx++) {
                    int station = schedulingCfg.stationTrips[trip][stationIdx];
                    if (!networkCfg.chargerPhys.count(station)) continue;
                    if (routeChargerStation && station != boundStation) continue;
                    int arrTime = schedulingCfg.stationArrTimes[trip][stationIdx];
                    int depTime = schedulingCfg.stationDepTimes[trip][stationIdx];
                    for (int time = arrTime; time < depTime; time++)
                        chargerOccupy[station][time - operationCfg.timeMin] += x_bns_t[eb][trip][station][time];
                }
            }
        }

        for (int station : networkCfg.chargerPhys) {
            if (routeChargerStation && station != boundStation) continue;
            auto it = networkCfg.chargerNums.find(station);
            int chargerCount = (it != networkCfg.chargerNums.end()) ? it->second : 1;
            for (int time = operationCfg.timeMin; time < operationCfg.timeMax; time++) {
                int timeIdx = time - operationCfg.timeMin;
                if (chargerOccupy[station][timeIdx].size() > 0)
                    model.addConstr(chargerOccupy[station][timeIdx] <= chargerCount,
                        namePrefix + "constChargerNum_s" + to_string(station) + "_t" + to_string(time));
            }
        }

        for (int ebIdx : ebIndices) {
            int eb = schedulingCfg.EBs[ebIdx];
            for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
                int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
                for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[trip].size(); stationIdx++) {
                    int station = schedulingCfg.stationTrips[trip][stationIdx];
                    if (!networkCfg.chargerPhys.count(station)) continue;
                    if (routeChargerStation && station != boundStation) continue;
                    string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station) + "_i" + to_string(stationIdx);
                    GRBVar charge = charge_bns[ebIdx][tripIdx][station];
                    GRBLinExpr occupyTime;
                    for (int time = schedulingCfg.stationArrTimes[trip][stationIdx];
                             time < schedulingCfg.stationDepTimes[trip][stationIdx]; time++)
                        occupyTime += x_bns_t[eb][trip][station][time];
                    model.addConstr(charge <= chargerCfg.chargeRate[0] * occupyTime, "constrChargeFast" + sub);
                }
            }
        }
    } else {
        // Relaxed: no charger capacity limits, charge freely within dwell time
        for (int ebIdx : ebIndices) {
            int eb = schedulingCfg.EBs[ebIdx];
            for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
                int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
                for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[trip].size(); stationIdx++) {
                    int station = schedulingCfg.stationTrips[trip][stationIdx];
                    if (!networkCfg.chargerPhys.count(station)) continue;
                    if (routeChargerStation && station != boundStation) continue;
                    int stayTime = schedulingCfg.stationDepTimes[trip][stationIdx]
                                 - schedulingCfg.stationArrTimes[trip][stationIdx];
                    string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station)
                               + "_i" + to_string(stationIdx);
                    GRBVar charge = charge_bns[ebIdx][tripIdx][station];
                    model.addConstr(charge <= chargerCfg.chargeRate[0] * static_cast<double>(stayTime),
                        "constrChargeFastRelaxed" + sub);
                }
            }
        }
    }

    // c_bns in static wireless charging stations.
    // Wireless facility availability is represented by RHS constraints tied to master z values.
    for (int ebIdx : ebIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
            int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[trip].size(); stationIdx++) {
                int station = schedulingCfg.stationTrips[trip][stationIdx];
                if (networkCfg.chargerPhys.count(station)) {
                    continue;
                }

                const string masterVarName = "w_station_s" + to_string(station);
                const size_t masterIdx = masterIdxByName.at(masterVarName);

                string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station) + "_i" + to_string(stationIdx);
                GRBVar charge = charge_bns[ebIdx][tripIdx][station];
                const int stayTime = schedulingCfg.stationDepTimes[trip][stationIdx] - schedulingCfg.stationArrTimes[trip][stationIdx];
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

    // c_bnl in dynamic wireless charging links.
    // Link availability is represented by RHS constraints tied to master z values.
    for (int ebIdx : ebIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
            int tripDep = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[tripDep].size(); stationIdx++) {
                int stationDep = schedulingCfg.stationTrips[tripDep][stationIdx];
                int tripArrIdx;
                int stationArrIdx;
                if (stationIdx == schedulingCfg.stationTrips[tripDep].size() - 1) {
                    if (tripIdx == schedulingCfg.tripEBs[ebIdx].size() - 1) {
                        continue;
                    }
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

                string subCharge = "_b" + to_string(eb) + "_n" + to_string(tripDep) + "_s" + to_string(stationDep) +
                                   "_i" + to_string(stationIdx) + "To_n" + to_string(tripArr) + "_s" + to_string(stationArr) + "_i" + to_string(stationArrIdx);
                GRBVar linkCharge = charge_bnl[ebIdx][tripIdx][stationDep][stationArr];
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

    // energy of the first and the last trips — battery capacity via RHS
    for (int ebIdx : ebIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        const int routeId = schedulingCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
        const auto& trips = schedulingCfg.tripEBs[ebIdx];
        int startTripIdx = 0;
        int startTrip = trips[startTripIdx];
        int startStationIdx = 0;
        int startStation = schedulingCfg.stationTrips[startTrip][startStationIdx];
        string subStart = "_b" + to_string(eb) + "_n" + to_string(startTrip) + "_s" + to_string(startStation) + "_i" + to_string(startStationIdx);
        double distStart = networkCfg.startDistanceEBs[ebIdx];
        // startEnergy == beta_start * routeE - C_start
        // Create: startEnergy == 0.0, set RHS = beta_start * routeE - C_start
        double C_start = distStart * operationCfg.curbWeight * operationCfg.mu * operationCfg.g / 3.6;
        double beta_start = socMax - distStart * operationCfg.weightPerEnergy * operationCfg.mu * operationCfg.g / 3.6;
        GRBVar startEnergy = energyArr_bns[ebIdx][startTripIdx][startStationIdx];
        batteryRhsOut.push_back({model.addConstr(startEnergy == 0.0,
            MakeWirelessBatteryRhsConstrName(routeId, batteryRhsSerial++)), routeId, beta_start, -C_start});

        int endTripIdx = trips.size() - 1;
        int endTrip = trips[endTripIdx];
        int endStationIdx = schedulingCfg.stationTrips[endTrip].size() - 1;
        int endStation = schedulingCfg.stationTrips[endTrip][endStationIdx];
        string subEnd = "_b" + to_string(eb) + "_n" + to_string(endTrip) + "_s" + to_string(endStation) + "_i" + to_string(endStationIdx);
        double distEnd = networkCfg.endDistanceEBs[ebIdx];
        // endEnergy >= beta_end * routeE + C_end
        // Create: endEnergy >= 0.0, set RHS = beta_end * routeE + C_end
        double C_end = distEnd * operationCfg.curbWeight * operationCfg.mu * operationCfg.g / 3.6;
        double beta_end = socMin + distEnd * operationCfg.weightPerEnergy * operationCfg.mu * operationCfg.g / 3.6;
        GRBVar endEnergy = energyDep_bns[ebIdx][endTripIdx][endStationIdx];
        batteryRhsOut.push_back({model.addConstr(endEnergy >= 0.0,
            MakeWirelessBatteryRhsConstrName(routeId, batteryRhsSerial++)), routeId, beta_end, C_end});
    }

    // inner station energy connection
    for (int ebIdx : ebIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
            int trip = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[trip].size(); stationIdx++) {
                int station = schedulingCfg.stationTrips[trip][stationIdx];
                string sub = "_b" + to_string(eb) + "_n" + to_string(trip) + "_s" + to_string(station) + "_i" + to_string(stationIdx);
                GRBVar arrE = energyArr_bns[ebIdx][tripIdx][stationIdx];
                GRBVar depE = energyDep_bns[ebIdx][tripIdx][stationIdx];
                if (networkCfg.chargerPhys.count(station)) {
                    GRBVar charge = charge_bns[ebIdx][tripIdx][station];
                    model.addConstr(depE == arrE + charge * chargerCfg.chargeEfficiency[0],
                                    "e_InnerStation" + sub);
                } else {
                    GRBVar charge = charge_bns[ebIdx][tripIdx][station];
                    model.addConstr(depE == arrE + charge * chargerCfg.chargeEfficiency[1],
                                    "e_InnerStation" + sub);
                }
            }
        }
    }

    // inter station energy connection — battery capacity via RHS
    for (int ebIdx : ebIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        const int routeId = schedulingCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
        for (int tripIdx = 0; tripIdx < schedulingCfg.tripEBs[ebIdx].size(); tripIdx++) {
            int tripDep = schedulingCfg.tripEBs[ebIdx][tripIdx];
            for (int stationIdx = 0; stationIdx < schedulingCfg.stationTrips[tripDep].size(); stationIdx++) {
                int stationDep = schedulingCfg.stationTrips[tripDep][stationIdx];
                string subDep = "_b" + to_string(eb) + "_n" + to_string(tripDep) + "_s" + to_string(stationDep) + "_i" + to_string(stationIdx);
                GRBVar depE = energyDep_bns[ebIdx][tripIdx][stationIdx];
                int tripArrIdx;
                int stationArrIdx;
                if (stationIdx == schedulingCfg.stationTrips[tripDep].size() - 1) {
                    if (tripIdx == schedulingCfg.tripEBs[ebIdx].size() - 1) {
                        continue;
                    }
                    tripArrIdx = tripIdx + 1;
                    stationArrIdx = 0;
                } else {
                    tripArrIdx = tripIdx;
                    stationArrIdx = stationIdx + 1;
                }

                int tripArr = schedulingCfg.tripEBs[ebIdx][tripArrIdx];
                int stationArr = schedulingCfg.stationTrips[tripArr][stationArrIdx];
                GRBVar arrE = energyArr_bns[ebIdx][tripArrIdx][stationArrIdx];
                double dist = networkCfg.linkDistances[stationDep][stationArr];
                // arrE - depE - linkCharge*eff == beta_inter * routeE - C_inter
                // Create: arrE - depE - linkCharge*eff == 0.0, set RHS = beta_inter * routeE - C_inter
                double C_inter = dist * operationCfg.curbWeight * operationCfg.mu * operationCfg.g / 3.6;
                double beta_inter = -dist * operationCfg.weightPerEnergy * operationCfg.mu * operationCfg.g / 3.6;
                GRBVar linkCharge = charge_bnl[ebIdx][tripIdx][stationDep][stationArr];
                batteryRhsOut.push_back({model.addConstr(
                    arrE - depE - linkCharge * chargerCfg.chargeEfficiency[2] == 0.0,
                    MakeWirelessBatteryRhsConstrName(routeId, batteryRhsSerial++)), routeId, beta_inter, -C_inter});
            }
        }
    }

    // overnight charging — battery capacity via RHS
    for (int ebIdx : ebIndices) {
        int eb = schedulingCfg.EBs[ebIdx];
        const int routeId = schedulingCfg.routeIdByEBs.at(static_cast<size_t>(ebIdx));
        const auto& trips = schedulingCfg.tripEBs[ebIdx];
        int endTripIdx = trips.size() - 1;
        int endTrip = trips[endTripIdx];
        int stationIdx = schedulingCfg.stationTrips[endTrip].size() - 1;
        int station = schedulingCfg.stationTrips[endTrip][stationIdx];
        string subEnd = "_b" + to_string(eb) + "_n" + to_string(endTrip) + "_s" + to_string(station) + "_i" + to_string(stationIdx);
        GRBVar endDepE = energyDep_bns[ebIdx][endTripIdx][stationIdx];
        GRBVar nightCharge = chargeNight_b[ebIdx];
        double dist = networkCfg.endDistanceEBs[ebIdx];
        // nightCharge*eff + endDepE == beta_night * routeE + C_night
        // Create: nightCharge*eff + endDepE == 0.0, set RHS = beta_night * routeE + C_night
        double C_night = dist * operationCfg.curbWeight * operationCfg.mu * operationCfg.g / 3.6;
        double beta_night = socMax + dist * operationCfg.weightPerEnergy * operationCfg.mu * operationCfg.g / 3.6;
        batteryRhsOut.push_back({model.addConstr(
            nightCharge * chargerCfg.chargeEfficiency[0] + endDepE == 0.0,
            "wireless_battery_rhs_r" + to_string(routeId) + "_overnight_b" + to_string(eb)), routeId, beta_night, C_night});
    }

    aggregateObj += scenario.probability * obj;
    model.update();
    return true;
}

static bool BuildWirelessChargingLShapedSubProblem(
    const string& dataRootFolder, const vector<WirelessScenarioConfig>& scenarios, size_t scenarioIdx,
    const vector<ProblemDataVar>& masterVars, GRBModel& subModel, LShapeSubProblemContext& context,
    vector<BatteryRhsConstr>& batteryRhsOut, vector<int>& routeIdByEBOut,
    const vector<int>* activeEbIndices = nullptr,
    const vector<int>* routeChargerStation = nullptr,
    bool relaxChargerCapacity = false,
    const ModelData* cachedData = nullptr)
{
    context.Clear();
    context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup).clear();

    GRBLinExpr obj = 0.0;
    size_t facilityRhsSerial = 0;
    if (scenarioIdx >= scenarios.size()) {
        return false;
    }
    if (!BuildWirelessOperationalSubproblem(dataRootFolder, scenarios[scenarioIdx], scenarioIdx,
            masterVars, subModel, context, obj, facilityRhsSerial, batteryRhsOut, routeIdByEBOut,
            activeEbIndices, routeChargerStation, relaxChargerCapacity, cachedData)) {
        return false;
    }
    subModel.setObjective(obj, GRB_MINIMIZE);

    subModel.update();
    return true;
}

WirelessReformDataInit::WirelessReformDataInit(
    std::string dataFolder)
    : dataFolder(std::move(dataFolder)) {}

void WirelessReformDataInit::DataInit(ProblemData& data)
{
    InitWirelessMasterData(data, dataFolder, "integer L-shaped decomposition");
}

std::vector<ProblemDataConstr> WirelessReformDataInit::ConstrInit(ProblemData& data)
{
    return BuildWirelessMasterConstrs(data);
}

std::vector<double> WirelessReformDataInit::BuildWarmStartMasterValues(
    const ProblemData& data) const
{
    const auto& masterVars = data.getData<std::vector<ProblemDataVar>>("masterVars");
    const auto& scenarios = data.getData<std::vector<WirelessScenarioConfig>>("wireless_scenarios");
    const string& dataFolder = data.getData<std::string>("wireless_data_root_folder");

    vector<double> values(masterVars.size(), 1.0);

    // For battery capacity variables, set only one capacity per route to 1
    // Find the middle capacity as default
    const auto& modelDataCache = data.getData<vector<ModelData>>("wireless_model_data_cache");
    for (size_t scIdx = 0; scIdx < scenarios.size(); ++scIdx) {
        const auto& scenarioData = modelDataCache[scIdx];
        const OperationCfg& operationCfg = scenarioData.GetOperationCfg();
        const SchedulingCfg& schedulingCfg = scenarioData.GetSchedulingCfg();

        for (int routeId = 0; routeId < schedulingCfg.numRoutes; ++routeId) {
            // Set all capacities for this route to 0 first
            for (int capacity = static_cast<int>(operationCfg.capacityMin);
                 capacity <= static_cast<int>(operationCfg.capacityMax); capacity += 10) {
                string varName = "sc" + to_string(scIdx) + "_E_r" + to_string(routeId) + "_cap" + to_string(capacity);
                for (size_t i = 0; i < masterVars.size(); ++i) {
                    if (masterVars[i].name == varName) {
                        values[i] = 0.0;
                        break;
                    }
                }
            }
            // Set the middle capacity to 1
            int midCapacity = static_cast<int>((operationCfg.capacityMin + operationCfg.capacityMax) / 2 / 10) * 10;
            string midVarName = "sc" + to_string(scIdx) + "_E_r" + to_string(routeId) + "_cap" + to_string(midCapacity);
            for (size_t i = 0; i < masterVars.size(); ++i) {
                if (masterVars[i].name == midVarName) {
                    values[i] = 1.0;
                    break;
                }
            }
        }
    }

    return values;
}
bool WirelessReformDataInit::IsWarmStartMasterFeasible(const ProblemData& data,
    const std::vector<double>& zValues, double tolerance) const
{
    return true;
}

int WirelessStationGroupSubProblem::ScenarioCount(const ProblemData& problemData) const
{
    return static_cast<int>(problemData.getData<std::vector<StationGroupInfo>>("wireless_station_groups").size());
}

void WirelessStationGroupSubProblem::InitSubProblem(const ProblemData& problemData, int subProblemIndex,
    GRBModel& subModel, LShapeSubProblemContext& context)
{
    const auto& stationGroups = problemData.getData<std::vector<StationGroupInfo>>("wireless_station_groups");
    const size_t sgIdx = static_cast<size_t>(subProblemIndex);
    if (sgIdx >= stationGroups.size()) {
        throw std::runtime_error("Station group index out of range.");
    }
    this->sgInfo = stationGroups[sgIdx];

    const auto& dataRootFolder = problemData.getData<std::string>("wireless_data_root_folder");
    const auto& scenarios = problemData.getData<std::vector<WirelessScenarioConfig>>("wireless_scenarios");
    const auto& masterVars = problemData.getData<std::vector<ProblemDataVar>>("masterVars");
    bool perStation = false;
    try { perStation = problemData.getData<bool>("wireless_per_station_mode"); } catch (...) {}
    const vector<int>* routeCharger = nullptr;
    if (perStation) {
        try {
            routeCharger = &problemData.getData<std::vector<std::vector<int>>>("wireless_route_charger_station")
                .at(static_cast<size_t>(this->sgInfo.scenarioIndex));
        } catch (...) {}
    }

    const auto& modelDataCache = problemData.getData<vector<ModelData>>("wireless_model_data_cache");
    const ModelData* cachedData = (static_cast<size_t>(this->sgInfo.scenarioIndex) < modelDataCache.size())
        ? &modelDataCache[static_cast<size_t>(this->sgInfo.scenarioIndex)] : nullptr;
    bool built = BuildWirelessChargingLShapedSubProblem(dataRootFolder, scenarios,
        static_cast<size_t>(this->sgInfo.scenarioIndex), masterVars, subModel, context,
        batteryRhsConstrs, routeIdByEB, &sgInfo.ebIndices, routeCharger,
        this->relaxChargerCapacity, cachedData);
    if (!built) {
        throw std::runtime_error("Failed to build wireless charging L-shaped sub-problem.");
    }

    lowerBoundReady = false;
    qLowerBound = 0.0;
    lowerBoundCutAdded = false;

    // Build flat array index for capacity master vars (one-time: [scenario][routeId] → varIdx)
    if (capacityVarIdx.empty()) {
        capacityVarIdx = BuildCapacityVarIndex(masterVars);
    }

    auto& facilityRhsConstrs = context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup);
    SetWirelessFacilityRhsConstrs(facilityRhsConstrs, 0.0);
    numRoutes = routeIdByEB.size() > 0 ? *max_element(routeIdByEB.begin(), routeIdByEB.end()) + 1 : 1;
    UpdateBatteryRhsConstrs(batteryRhsConstrs, vector<double>(static_cast<size_t>(numRoutes), 0.0));
    subModel.set(GRB_IntParam_Method, 1);
    subModel.update();

    InitRelaxedSubProblem(problemData, subModel);
}

static void CollectWirelessFacilityRhsConstrs(GRBModel& model, vector<GRBConstr>& out)
{
    out.clear();
    const int numConstrs = model.get(GRB_IntAttr_NumConstrs);
    GRBConstr* constrs = model.getConstrs();
    for (int idx = 0; idx < numConstrs; ++idx) {
        size_t masterIdx = 0;
        if (TryParseWirelessFacilityRhsMasterIndex(constrs[idx].get(GRB_StringAttr_ConstrName), masterIdx)) {
            out.push_back(constrs[idx]);
        }
    }
    delete[] constrs;
}

void WirelessStationGroupSubProblem::InitRelaxedSubProblem(
    const ProblemData& problemData, GRBModel& subModel)
{
    (void)problemData;
    relaxedModel = std::make_unique<GRBModel>(subModel.relax());
    // Copy solver params from parent model (presolve/method/threads)
    relaxedModel->set(GRB_IntParam_Presolve, subModel.get(GRB_IntParam_Presolve));
    relaxedModel->set(GRB_IntParam_Method, subModel.get(GRB_IntParam_Method));
    relaxedModel->set(GRB_IntParam_Threads, subModel.get(GRB_IntParam_Threads));
    relaxedFacilityConstrs.clear();
    CollectWirelessFacilityRhsConstrs(*relaxedModel, relaxedFacilityConstrs);

    // Collect battery RHS constraints from relaxed model by matching names
    relaxedBatteryConstrs.clear();
    relaxedBatteryConstrs.reserve(batteryRhsConstrs.size());
    for (const auto& brc : batteryRhsConstrs) {
        BatteryRhsConstr relaxedBrc;
        relaxedBrc.constr = relaxedModel->getConstrByName(brc.constr.get(GRB_StringAttr_ConstrName));
        relaxedBrc.routeId = brc.routeId;
        relaxedBrc.beta = brc.beta;
        relaxedBrc.constant = brc.constant;
        relaxedBatteryConstrs.push_back(relaxedBrc);
    }
    relaxedModel->update();
}

Status WirelessStationGroupSubProblem::SolveRelaxedModel(
    const ProblemData& problemData, const std::vector<double>& zValues,
    LShapeCutInfo& cutInfo, double& subObj)
{
    try {
        const auto& masterVars = problemData.getData<std::vector<ProblemDataVar>>("masterVars");
        if (zValues.size() != masterVars.size() || !relaxedModel) {
            std::cerr << "Relaxed model not initialized" << std::endl;
            return ERROR;
        }

        // Update facility RHS
        for (auto& constr : relaxedFacilityConstrs) {
            size_t masterIdx = 0;
            if (TryGetWirelessFacilityRhsMasterIndex(constr, zValues.size(), masterIdx)) {
                constr.set(GRB_DoubleAttr_RHS, zValues[masterIdx]);
            }
        }

        // Update battery RHS
        vector<double> routeE = ComputeRouteEPerRoute(zValues,
            static_cast<size_t>(sgInfo.scenarioIndex), numRoutes,
            capacityVarIdx);
        UpdateBatteryRhsConstrs(relaxedBatteryConstrs, routeE);

        relaxedModel->set(GRB_IntParam_Method, 1);
        relaxedModel->update();
        relaxedModel->optimize();

        const int status = relaxedModel->get(GRB_IntAttr_Status);
        if (status == GRB_OPTIMAL) {
            subObj = relaxedModel->get(GRB_DoubleAttr_ObjVal);

            cutInfo.isOptimalityCut = true;
            cutInfo.sense = '>';
            cutInfo.rhs = 0.0;
            cutInfo.yCoeffs.assign(zValues.size(), 0.0);
            cutInfo.constant = subObj;

            // Facility duals
            for (auto& constr : relaxedFacilityConstrs) {
                size_t masterIdx = 0;
                if (TryGetWirelessFacilityRhsMasterIndex(constr, zValues.size(), masterIdx)) {
                    const double pi = constr.get(GRB_DoubleAttr_Pi);
                    cutInfo.yCoeffs[masterIdx] += pi;
                    cutInfo.constant -= pi * zValues[masterIdx];
                }
            }

            // Battery duals
            ExtractBatteryRhsDuals(relaxedBatteryConstrs, zValues,
                static_cast<size_t>(sgInfo.scenarioIndex), cutInfo,
                capacityVarIdx);

            return OK;
        }

        if (status == GRB_INFEASIBLE || status == GRB_INF_OR_UNBD || status == GRB_UNBOUNDED) {
            // FarkasProof available from first solve for infeasible continuous models
            bool gotFarkas = false;
            try {
                const double beta = relaxedModel->get(GRB_DoubleAttr_FarkasProof);
                if (std::isfinite(beta) && beta > 1e-9) {
                    cutInfo.isOptimalityCut = false;
                    cutInfo.sense = '>';
                    cutInfo.rhs = beta;
                    cutInfo.yCoeffs.assign(zValues.size(), 0.0);
                    cutInfo.constant = 0.0;

                    for (auto& constr : relaxedFacilityConstrs) {
                        size_t masterIdx = 0;
                        if (TryGetWirelessFacilityRhsMasterIndex(constr, zValues.size(), masterIdx)) {
                            const double lambda = constr.get(GRB_DoubleAttr_FarkasDual);
                            cutInfo.yCoeffs[masterIdx] += lambda;
                            cutInfo.constant -= lambda * zValues[masterIdx];
                        }
                    }

                    ExtractBatteryRhsFarkasDuals(relaxedBatteryConstrs, zValues,
                        static_cast<size_t>(sgInfo.scenarioIndex), cutInfo,
                        capacityVarIdx);
                    subObj = GRB_INFINITY;
                    gotFarkas = true;
                }
            } catch (const GRBException&) {}
            if (!gotFarkas) {
                std::unordered_set<size_t> relevantSet;

                // Facility vars: from facility RHS constraints in this subproblem
                for (auto& constr : relaxedFacilityConstrs) {
                    size_t masterIdx = 0;
                    if (TryGetWirelessFacilityRhsMasterIndex(constr, zValues.size(), masterIdx)) {
                        relevantSet.insert(masterIdx);
                    }
                }

                // Battery capacity vars: only for routes in this station group, this scenario
                const size_t sgScIdx = static_cast<size_t>(sgInfo.scenarioIndex);
                for (int rid : sgInfo.routeIds) {
                    for (size_t j = 0; j < masterVars.size(); ++j) {
                        const string& name = masterVars[j].name;
                        if (name.rfind("sc", 0) != 0) continue;
                        size_t ePos = name.find("_E_r");
                        if (ePos == string::npos) continue;
                        size_t capPos = name.find("_cap", ePos + 4);
                        if (capPos == string::npos) continue;
                        int varSc = stoi(name.substr(2, ePos - 2));
                        if (static_cast<size_t>(varSc) != sgScIdx) continue;
                        int varRid = stoi(name.substr(ePos + 4, capPos - (ePos + 4)));
                        if (varRid == rid) {
                            relevantSet.insert(j);
                        }
                    }
                }

                if (relevantSet.empty()) {
                    BuildWirelessNoGoodFeasibilityCut(zValues, cutInfo);
                } else {
                    std::vector<size_t> relevantIndices(relevantSet.begin(), relevantSet.end());
                    int selectedCount = 0;
                    for (size_t i : relevantIndices) {
                        if (zValues[i] > 0.5) ++selectedCount;
                    }
                    cutInfo.isOptimalityCut = false;
                    cutInfo.sense = '>';
                    cutInfo.yCoeffs.assign(zValues.size(), 0.0);
                    cutInfo.constant = 0.0;
                    for (size_t i : relevantIndices) {
                        cutInfo.yCoeffs[i] = (zValues[i] > 0.5) ? -1.0 : 1.0;
                    }
                    cutInfo.rhs = 1.0 - static_cast<double>(selectedCount);
                }
            } // if (!gotFarkas)
            subObj = GRB_INFINITY;
            return OK;
        }

        std::cerr << "Relaxed model unexpected status: " << status << std::endl;
        return ERROR;
    } catch (const GRBException& e) {
        std::cerr << "Relaxed solve failed: " << e.getMessage() << std::endl;
        return ERROR;
    }
}

void WirelessStationGroupSubProblem::SetLowerBound(double lowerBound)
{
    qLowerBound = lowerBound;
    lowerBoundReady = true;
    lowerBoundCutAdded = false;
}

void WirelessStationGroupSubProblem::UpdateSubProblem(const ProblemData& problemData, GRBModel& subModel,
    LShapeSubProblemContext& context, const std::vector<double>& zValues)
{
    const auto& masterVars = problemData.getData<std::vector<ProblemDataVar>>("masterVars");
    if (zValues.size() != masterVars.size()) {
        throw std::runtime_error("Wireless L-shaped master value size mismatch.");
    }

    // Update facility RHS constraints
    auto& facilityRhsConstrs = context.EnsureConstrGroup(kWirelessFacilityRhsConstrGroup);
    SetWirelessFacilityRhsConstrs(facilityRhsConstrs, zValues);

    // Compute routeE per route from zValues and update battery RHS constraints
    vector<double> routeE = ComputeRouteEPerRoute(zValues,
        static_cast<size_t>(sgInfo.scenarioIndex), numRoutes,
        capacityVarIdx);
    UpdateBatteryRhsConstrs(batteryRhsConstrs, routeE);

    subModel.update();
}

Status WirelessStationGroupSubProblem::SolveSubProblem(const ProblemData& problemData, GRBModel& subModel,
    LShapeSubProblemContext& context, const std::vector<double>& zValues,
    LShapeCutInfo& cutInfo, double& subObj)
{
    subModel.optimize();
    int status = subModel.get(GRB_IntAttr_Status);

    if (status == GRB_OPTIMAL) {
        subObj = subModel.get(GRB_DoubleAttr_ObjVal);

        // Integer optimality cut (MIP can't provide LP duals)
        int oneCount = 0;
        for (double v : zValues) if (v > 0.5) ++oneCount;
        double lowerBound = lowerBoundReady ? qLowerBound : 0.0;
        double delta = std::max(0.0, subObj - lowerBound);

        cutInfo.isOptimalityCut = true;
        cutInfo.sense = '>';
        cutInfo.rhs = 0.0;
        cutInfo.constant = lowerBound + delta * (1.0 - static_cast<double>(oneCount));
        cutInfo.yCoeffs.assign(zValues.size(), 0.0);
        for (size_t i = 0; i < zValues.size(); ++i) {
            cutInfo.yCoeffs[i] = (zValues[i] > 0.5) ? delta : -delta;
        }
        return OK;
    }

    if (status == GRB_INFEASIBLE || status == GRB_INF_OR_UNBD || status == GRB_UNBOUNDED) {
        // Sub-problem is LP — get Farkas duals (Method=1 gives infeasibility cert)
        const auto& masterVars = problemData.getData<std::vector<ProblemDataVar>>("masterVars");
        try {
            const double farkasProof = subModel.get(GRB_DoubleAttr_FarkasProof);
            if (std::isfinite(farkasProof) && farkasProof > 1e-9) {
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
                ExtractBatteryRhsFarkasDuals(batteryRhsConstrs, zValues,
                    static_cast<size_t>(sgInfo.scenarioIndex), cutInfo,
                    capacityVarIdx);

                subObj = GRB_INFINITY;
                return OK;
            }
        } catch (const GRBException&) {
            // FarkasProof not available, fall back to no-good
        }

        BuildWirelessNoGoodFeasibilityCut(zValues, cutInfo);
        subObj = GRB_INFINITY;
        return OK;
    }

    std::cerr << "Wireless L-shaped sub-problem ended with unexpected status: " << status << std::endl;
    return ERROR;
}

Status WirelessStationGroupSubProblem::SolveRelaxedSubProblem(const ProblemData& problemData, GRBModel& subModel,
    LShapeSubProblemContext& context, const std::vector<double>& zValues,
    LShapeCutInfo& cutInfo, double& subObj)
{
    (void)subModel;
    (void)context;
    return SolveRelaxedModel(problemData, zValues, cutInfo, subObj);
}

std::unique_ptr<ISubProblem_LShape> WirelessStationGroupSubProblem::Clone() const
{
    auto c = std::make_unique<WirelessStationGroupSubProblem>();
    c->sgInfo = sgInfo;
    c->lowerBoundReady = lowerBoundReady;
    c->qLowerBound = qLowerBound;
    c->lowerBoundCutAdded = lowerBoundCutAdded;
    c->numRoutes = numRoutes;
    c->routeIdByEB = routeIdByEB;
    return c;
}
