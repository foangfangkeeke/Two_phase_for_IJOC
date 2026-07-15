#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include "benders_decomposition.hpp"

// --- Utility: identify master variable types ---
inline bool IsFacilityMasterVar(const std::string& name)
{
    return name.rfind("w_station_", 0) == 0 || name.rfind("w_link_", 0) == 0;
}

inline bool TryParseCapacityVar(const std::string& name, int& scenarioIndex, int& routeId, int& capacity)
{
    if (name.rfind("sc", 0) != 0) return false;
    const size_t ePos = name.find("_E_r");
    const size_t capPos = name.find("_cap");
    if (ePos == std::string::npos || capPos == std::string::npos || capPos <= ePos + 4) return false;
    scenarioIndex = std::stoi(name.substr(2, ePos - 2));
    routeId = std::stoi(name.substr(ePos + 4, capPos - (ePos + 4)));
    capacity = std::stoi(name.substr(capPos + 4));
    return true;
}

// --- Station-group strengthened no-good cut ---
// Scope: station-group facility union (vs per-route in Paper1).
// Σ_{j: w_j=0, w_j in stationGroupFacilities} w_j + Σ_{r in stationGroup, cap > selected} z_{r,cap} >= 1
inline BendersCutInfo BuildStationGroupStrengthenedNoGoodCut(
    const std::vector<ProblemDataVar>& masterVars,
    const std::vector<double>& zValues,
    int scenarioIndex,
    const std::vector<int>& stationGroupRouteIds,
    const std::vector<std::string>& stationGroupFacilityVarNames)
{
    std::unordered_set<std::string> facilitySet(
        stationGroupFacilityVarNames.begin(), stationGroupFacilityVarNames.end());

    // Find selected capacity for each route in this station group
    std::unordered_map<int, int> selectedCapacityByRoute;  // routeId -> selected capacity
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        int vs, vr, cap;
        if (TryParseCapacityVar(masterVars[idx].name, vs, vr, cap)
            && vs == scenarioIndex && zValues[idx] > 0.5) {
            selectedCapacityByRoute[vr] = cap;
        }
    }

    BendersCutInfo cut;
    cut.isOptimalityCut = false;
    cut.sense = '>';
    cut.rhs = 1.0;
    cut.constant = 0.0;
    cut.yCoeffs.assign(zValues.size(), 0.0);

    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        const std::string& name = masterVars[idx].name;
        if (IsFacilityMasterVar(name)) {
            // Facility unopened AND relevant to this station group -> can open it
            if (facilitySet.count(name) > 0 && zValues[idx] <= 0.5) {
                cut.yCoeffs[idx] = 1.0;
            }
            continue;
        }

        int vs, vr, cap;
        if (TryParseCapacityVar(name, vs, vr, cap) && vs == scenarioIndex) {
            // Check if this route is in the station group
            bool inGroup = false;
            for (int rid : stationGroupRouteIds) {
                if (rid == vr) { inGroup = true; break; }
            }
            if (!inGroup) continue;

            auto it = selectedCapacityByRoute.find(vr);
            if (it != selectedCapacityByRoute.end() && cap > it->second) {
                cut.yCoeffs[idx] = 1.0;
            }
        }
    }

    return cut;
}

// --- Monotonicity-strengthened optimality cut ---
// Same indicator as the feasibility cut.
// theta >= Q* - delta * indicator, where delta = Q* - L.
inline BendersCutInfo BuildMonotonicityStrengthenedOptimalityCut(
    const std::vector<ProblemDataVar>& masterVars,
    const std::vector<double>& zValues, int scenarioIndex,
    const std::vector<int>& scenarioRouteIds,
    const std::vector<std::string>& scenarioFacilityVarNames,
    double subObj, double lowerBound)
{
    std::unordered_set<std::string> facilitySet(
        scenarioFacilityVarNames.begin(), scenarioFacilityVarNames.end());
    std::unordered_map<int, int> selectedCapacityByRoute;
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        int vs, vr, cap;
        if (TryParseCapacityVar(masterVars[idx].name, vs, vr, cap)
            && vs == scenarioIndex && zValues[idx] > 0.5)
            selectedCapacityByRoute[vr] = cap;
    }
    double delta = std::max(0.0, subObj - lowerBound);
    BendersCutInfo cut;
    cut.isOptimalityCut = true; cut.sense = '>'; cut.rhs = 0.0;
    cut.constant = subObj;
    cut.yCoeffs.assign(zValues.size(), 0.0);
    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        const std::string& name = masterVars[idx].name;
        if (IsFacilityMasterVar(name)) {
            if (facilitySet.count(name) > 0 && zValues[idx] <= 0.5)
                cut.yCoeffs[idx] = -delta;
            continue;
        }
        int vs, vr, cap;
        if (TryParseCapacityVar(name, vs, vr, cap) && vs == scenarioIndex) {
            bool inScenario = false;
            for (int rid : scenarioRouteIds)
                if (rid == vr) { inScenario = true; break; }
            if (!inScenario) continue;
            auto it = selectedCapacityByRoute.find(vr);
            if (it != selectedCapacityByRoute.end() && cap > it->second)
                cut.yCoeffs[idx] = -delta;
        }
    }
    return cut;
}

// --- Cut signature for deduplication ---
inline std::string MakeFeasibilityCutSignature(const BendersCutInfo& cutInfo)
{
    std::ostringstream out;
    out << std::setprecision(17) << cutInfo.sense << '|' << cutInfo.rhs << '|'
        << cutInfo.constant;
    for (double coeff : cutInfo.yCoeffs) out << '|' << coeff;
    return out.str();
}
