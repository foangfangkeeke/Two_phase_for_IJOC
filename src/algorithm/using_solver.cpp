#include "using_solver.hpp"
#include "runtime_options.hpp"
#include "wireless.hpp"
#include "tools.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string CsvEscape(const std::string& value)
{
    bool needsQuotes = false;
    for (char ch : value) {
        if (ch == ',' || ch == '"' || ch == '\n' || ch == '\r') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return value;
    std::string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') escaped += '"';
        escaped += ch;
    }
    escaped += '"';
    return escaped;
}

bool StartsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

bool TryParseScenarioIndex(const std::string& name, int& scenarioIndex)
{
    if (!StartsWith(name, "sc")) return false;
    size_t pos = 2;
    size_t end = pos;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) {
        ++end;
    }
    if (end == pos || end >= name.size() || name[end] != '_') return false;
    scenarioIndex = std::stoi(name.substr(pos, end - pos));
    return true;
}

bool TryParseIntAfter(const std::string& value, const std::string& marker, size_t startPos, int& out)
{
    const size_t markerPos = value.find(marker, startPos);
    if (markerPos == std::string::npos) return false;
    size_t pos = markerPos + marker.size();
    size_t end = pos;
    while (end < value.size() && std::isdigit(static_cast<unsigned char>(value[end]))) {
        ++end;
    }
    if (end == pos) return false;
    out = std::stoi(value.substr(pos, end - pos));
    return true;
}

bool ParseBatteryVarName(const std::string& name, int& scenarioIndex, int& routeId, int& capacity)
{
    if (!TryParseScenarioIndex(name, scenarioIndex)) return false;
    const size_t scEnd = name.find('_');
    if (scEnd == std::string::npos || name.compare(scEnd, 4, "_E_r") != 0) return false;

    size_t pos = scEnd + 4;
    size_t end = pos;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) ++end;
    if (end == pos || name.compare(end, 4, "_cap") != 0) return false;
    routeId = std::stoi(name.substr(pos, end - pos));

    pos = end + 4;
    end = pos;
    while (end < name.size() && std::isdigit(static_cast<unsigned char>(name[end]))) ++end;
    if (end == pos) return false;
    capacity = std::stoi(name.substr(pos, end - pos));
    return true;
}

struct SolverSolutionSummary {
    double facilityCost = 0.0;
    double stationFacilityCost = 0.0;
    double linkFacilityCost = 0.0;
    double batteryCost = 0.0;
    int selectedStationCount = 0;
    int selectedLinkCount = 0;
    double expectedFastChargeKwh = 0.0;
    double expectedStaticWirelessKwh = 0.0;
    double expectedDynamicWirelessKwh = 0.0;
    double expectedNightChargeKwh = 0.0;
    double expectedTotalChargeKwh = 0.0;
    double expectedOperationalCost = 0.0;
};

} // namespace

static const std::map<
    ProblemType,
    std::function<std::unique_ptr<IDataInitializationStrategy_Solver>(const std::string&, bool)>> dataInitMap = {
    {WIRELESS_CHARGING_NO_PLUGIN, [](const std::string& dataFolder, bool allowWirelessCharging){
        return std::make_unique<WirelessChargingDataInitializationStrategy_Solver>(
            dataFolder, allowWirelessCharging);
    }}
};

Status UsingSolver::Initialize()
{
    auto it = dataInitMap.find(problemType);
    if (it == dataInitMap.end()) {
        throw std::invalid_argument("Unsupported problem type");
    }
    dataIniter = it->second(dataFolder, allowWirelessCharging);

    env = std::make_unique<GRBEnv>(true);
    env->start();
    model = std::make_unique<GRBModel>(*env);
    model->set(GRB_IntParam_OutputFlag, 1);

    Status initStatus = dataIniter->DataInit(*model);
    if (initStatus != OK) {
        return initStatus;
    }
    ApplySolverConfig(*model);
    model->update();
    initialized = true;
    return OK;
}

Status UsingSolver::Solve()
{
    const auto solveStart = Tools::Clock::now();
    model->optimize();

    int solveStatus = model->get(GRB_IntAttr_Status);
    const auto solveEnd = Tools::Clock::now();
    if (solveStatus == GRB_OPTIMAL || (solveStatus == GRB_TIME_LIMIT && model->get(GRB_IntAttr_SolCount) > 0)) {
        if (solveStatus == GRB_OPTIMAL) {
            std::cout << "Solver status: OPTIMAL" << std::endl;
        } else {
            std::cout << "Solver status: TIME_LIMIT_WITH_INCUMBENT" << std::endl;
        }
        double ub = model->get(GRB_DoubleAttr_ObjVal);
        double lb = model->get(GRB_DoubleAttr_ObjBound);
        std::cout << "UB: " << ub << ", LB: " << lb << std::endl;
        const long long solveMs = Tools::ElapsedMs(solveStart, solveEnd);
        std::cout << "Solver final solve time: " << solveMs << " ms" << std::endl;
        ExportSolution(ub, lb, solveMs);
        return OK;
    }

    double lb = model->get(GRB_DoubleAttr_ObjBound);
    std::cout << "Solver status: NO_INCUMBENT_OR_ERROR" << std::endl;
    std::cout << "UB: inf, LB: " << lb << " (no incumbent found)" << std::endl;
    std::cerr << "Not solved to optimality, status: " << solveStatus << std::endl;
    return ERROR;
}

UsingSolver::UsingSolver(ProblemType problemType, std::string dataFolder, bool allowWirelessCharging)
    : problemType(problemType), dataFolder(dataFolder), allowWirelessCharging(allowWirelessCharging) {};

void UsingSolver::ExportSolution(double ub, double lb, long long solveMs) const
{
    if (!exportSolution && !exportSummary) return;

    const fs::path exportDir = exportDirectory.empty()
        ? fs::path("results") / dataFolder / "solver_solution"
        : fs::path(exportDirectory);
    fs::create_directories(exportDir);

    const RuntimeOptions& runtimeOptions = GetRuntimeOptions();
    const int scenarioCount = runtimeOptions.scenarioCount;
    const double probability = scenarioCount > 0 ? 1.0 / static_cast<double>(scenarioCount) : 0.0;

    if (exportSolution) {
        std::ofstream out(exportDir / "solution_metadata.csv");
        out << std::fixed << std::setprecision(6);
        out << "metric,value\n";
        out << "upper_bound," << ub << '\n';
        out << "lower_bound," << lb << '\n';
        out << "solve_elapsed_ms," << solveMs << '\n';
        out << "wireless_option," << (allowWirelessCharging ? 1 : 0) << '\n';
    }

    std::vector<double> scenarioOperationalCost(static_cast<size_t>(scenarioCount), 0.0);
    std::vector<double> fastChargeKwh(static_cast<size_t>(scenarioCount), 0.0);
    std::vector<double> staticWirelessChargeKwh(static_cast<size_t>(scenarioCount), 0.0);
    std::vector<double> dynamicWirelessChargeKwh(static_cast<size_t>(scenarioCount), 0.0);
    std::vector<double> nightChargeKwh(static_cast<size_t>(scenarioCount), 0.0);
    std::unordered_set<int> wirelessStationCandidates;
    SolverSolutionSummary summary;

    const int numVars = model->get(GRB_IntAttr_NumVars);
    GRBVar* vars = model->getVars();

    if (exportSummary) {
        for (int idx = 0; idx < numVars; ++idx) {
            const std::string name = vars[idx].get(GRB_StringAttr_VarName);
            if (StartsWith(name, "w_station_s")) {
                const std::string stationId = name.substr(std::string("w_station_s").size());
                wirelessStationCandidates.insert(std::stoi(stationId));
            }
        }
    }

    if (exportSolution) {
        std::ofstream out(exportDir / "model_variables.csv");
        out << std::fixed << std::setprecision(6);
        out << "index,var_name,value,obj,lb,ub,vtype\n";

        for (int idx = 0; idx < numVars; ++idx) {
            const std::string name = vars[idx].get(GRB_StringAttr_VarName);
            const double value = vars[idx].get(GRB_DoubleAttr_X);
            const double objCoeff = vars[idx].get(GRB_DoubleAttr_Obj);
            const double varLb = vars[idx].get(GRB_DoubleAttr_LB);
            const double varUb = vars[idx].get(GRB_DoubleAttr_UB);
            const char vtype = vars[idx].get(GRB_CharAttr_VType);
            out << idx << ',' << CsvEscape(name) << ',' << value << ','
                << objCoeff << ',' << varLb << ',' << varUb << ',' << vtype << '\n';
        }
    }

    if (exportSummary) {
        std::ofstream facilitiesOut(exportDir / "selected_facilities.csv");
        facilitiesOut << std::fixed << std::setprecision(6);
        facilitiesOut << "facility_type,facility_id,from_node,to_node,var_name,selected,unit_cost,cost\n";

        std::ofstream batteryOut(exportDir / "route_battery_summary.csv");
        batteryOut << std::fixed << std::setprecision(6);
        batteryOut << "scenario_index,route_id,probability,battery_capacity,var_name,selected,cost\n";

        for (int idx = 0; idx < numVars; ++idx) {
            const std::string name = vars[idx].get(GRB_StringAttr_VarName);
            const double value = vars[idx].get(GRB_DoubleAttr_X);
            const double objCoeff = vars[idx].get(GRB_DoubleAttr_Obj);
            const double cost = objCoeff * value;

            if (StartsWith(name, "w_station_s")) {
                if (value > 0.5) {
                    ++summary.selectedStationCount;
                    summary.facilityCost += cost;
                    summary.stationFacilityCost += cost;
                    const std::string stationId = name.substr(std::string("w_station_s").size());
                    facilitiesOut << "station," << stationId << ",,," << CsvEscape(name) << ','
                                  << value << ',' << objCoeff << ',' << cost << '\n';
                }
                continue;
            }

            if (StartsWith(name, "w_link_l")) {
                if (value > 0.5) {
                    ++summary.selectedLinkCount;
                    summary.facilityCost += cost;
                    summary.linkFacilityCost += cost;
                    const size_t prefixLen = std::string("w_link_l").size();
                    const size_t toPos = name.find("To", prefixLen);
                    const std::string fromNode = name.substr(prefixLen, toPos - prefixLen);
                    const std::string toNode = name.substr(toPos + 2);
                    facilitiesOut << "link,," << fromNode << ',' << toNode << ','
                                  << CsvEscape(name) << ',' << value << ',' << objCoeff
                                  << ',' << cost << '\n';
                }
                continue;
            }

            int scenarioIndex = 0;
            int routeId = 0;
            int capacity = 0;
            if (ParseBatteryVarName(name, scenarioIndex, routeId, capacity)) {
                summary.batteryCost += cost;
                if (scenarioIndex >= 0 && scenarioIndex < scenarioCount && value > 0.5) {
                    batteryOut << scenarioIndex << ',' << routeId << ',' << probability << ','
                               << capacity << ',' << CsvEscape(name) << ',' << value << ','
                               << cost << '\n';
                }
                continue;
            }

            if (value <= 1e-7) continue;
            if (!StartsWith(name, "sc")) continue;
            if (!TryParseScenarioIndex(name, scenarioIndex)
                || scenarioIndex < 0 || scenarioIndex >= scenarioCount) {
                continue;
            }

            if (StartsWith(name.substr(name.find('_') + 1), "c_Night_")) {
                nightChargeKwh[static_cast<size_t>(scenarioIndex)] += value;
                scenarioOperationalCost[static_cast<size_t>(scenarioIndex)] += cost;
            } else {
                const size_t firstUnderscore = name.find('_');
                const std::string localName = firstUnderscore == std::string::npos
                    ? name : name.substr(firstUnderscore + 1);
                if (!StartsWith(localName, "c_")) continue;
                scenarioOperationalCost[static_cast<size_t>(scenarioIndex)] += cost;
                if (localName.find("To_") != std::string::npos) {
                    dynamicWirelessChargeKwh[static_cast<size_t>(scenarioIndex)] += value;
                } else {
                    int station = -1;
                    if (TryParseIntAfter(localName, "_s", 0, station)
                        && wirelessStationCandidates.count(station) > 0) {
                        staticWirelessChargeKwh[static_cast<size_t>(scenarioIndex)] += value;
                    } else {
                        fastChargeKwh[static_cast<size_t>(scenarioIndex)] += value;
                    }
                }
            }
        }

        std::ofstream scenarioOut(exportDir / "scenario_summary.csv");
        scenarioOut << std::fixed << std::setprecision(6);
        scenarioOut << "subproblem_index,scenario_index,probability,status,q_weighted,"
                    << "q_unweighted,fast_charge_kwh,expected_fast_charge_kwh,"
                    << "static_wireless_charge_kwh,expected_static_wireless_charge_kwh,"
                    << "dynamic_wireless_charge_kwh,expected_dynamic_wireless_charge_kwh,"
                    << "night_charge_kwh,expected_night_charge_kwh,total_charge_kwh,"
                    << "expected_total_charge_kwh\n";
        const int solveStatus = model->get(GRB_IntAttr_Status);
        for (int scenarioIndex = 0; scenarioIndex < scenarioCount; ++scenarioIndex) {
            const size_t sc = static_cast<size_t>(scenarioIndex);
            const double totalChargeKwh = fastChargeKwh[sc] + staticWirelessChargeKwh[sc]
                + dynamicWirelessChargeKwh[sc] + nightChargeKwh[sc];
            summary.expectedOperationalCost += scenarioOperationalCost[sc];
            summary.expectedFastChargeKwh += probability * fastChargeKwh[sc];
            summary.expectedStaticWirelessKwh += probability * staticWirelessChargeKwh[sc];
            summary.expectedDynamicWirelessKwh += probability * dynamicWirelessChargeKwh[sc];
            summary.expectedNightChargeKwh += probability * nightChargeKwh[sc];
            summary.expectedTotalChargeKwh += probability * totalChargeKwh;

            scenarioOut << scenarioIndex << ',' << scenarioIndex << ',' << probability << ','
                        << solveStatus << ',' << scenarioOperationalCost[sc] << ','
                        << (probability > 0.0 ? scenarioOperationalCost[sc] / probability : 0.0)
                        << ',' << fastChargeKwh[sc] << ',' << probability * fastChargeKwh[sc]
                        << ',' << staticWirelessChargeKwh[sc] << ','
                        << probability * staticWirelessChargeKwh[sc] << ','
                        << dynamicWirelessChargeKwh[sc] << ','
                        << probability * dynamicWirelessChargeKwh[sc] << ','
                        << nightChargeKwh[sc] << ',' << probability * nightChargeKwh[sc]
                        << ',' << totalChargeKwh << ',' << probability * totalChargeKwh << '\n';
        }

        std::ofstream summaryOut(exportDir / "solution_summary.csv");
        summaryOut << std::fixed << std::setprecision(6);
        summaryOut << "metric,value\n";
        summaryOut << "algorithm,solver\n";
        summaryOut << "data_folder," << CsvEscape(dataFolder) << '\n';
        summaryOut << "scenario_count," << runtimeOptions.scenarioCount << '\n';
        summaryOut << "cap," << runtimeOptions.chargerCapacity << '\n';
        summaryOut << "facility_cost_scale," << runtimeOptions.facilityCostScale << '\n';
        summaryOut << "energy_scale," << runtimeOptions.energyScale << '\n';
        summaryOut << "wireless_option," << (allowWirelessCharging ? 1 : 0) << '\n';
        summaryOut << "best_upper_bound," << ub << '\n';
        summaryOut << "best_lower_bound," << lb << '\n';
        summaryOut << "final_gap,"
                   << ((ub - lb) / std::max(1.0, std::fabs(ub))) << '\n';
        summaryOut << "facility_cost," << summary.facilityCost << '\n';
        summaryOut << "station_facility_cost," << summary.stationFacilityCost << '\n';
        summaryOut << "link_facility_cost," << summary.linkFacilityCost << '\n';
        summaryOut << "expected_battery_cost," << summary.batteryCost << '\n';
        summaryOut << "fixed_cost," << (summary.facilityCost + summary.batteryCost) << '\n';
        summaryOut << "expected_operational_cost," << summary.expectedOperationalCost << '\n';
        summaryOut << "selected_station_count," << summary.selectedStationCount << '\n';
        summaryOut << "selected_link_count," << summary.selectedLinkCount << '\n';
        summaryOut << "expected_fast_charge_kwh," << summary.expectedFastChargeKwh << '\n';
        summaryOut << "expected_static_wireless_kwh," << summary.expectedStaticWirelessKwh << '\n';
        summaryOut << "expected_dynamic_wireless_kwh," << summary.expectedDynamicWirelessKwh << '\n';
        summaryOut << "expected_night_charge_kwh," << summary.expectedNightChargeKwh << '\n';
        summaryOut << "expected_total_charge_kwh," << summary.expectedTotalChargeKwh << '\n';
    }

    delete[] vars;
    std::cout << "Solution export written to " << exportDir.string() << std::endl;
}

UsingSolver::~UsingSolver() {};
