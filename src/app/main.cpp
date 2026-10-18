#include "algorithm/lifted_ils.hpp"
#include "algorithm/lifted_two_phase.hpp"
#include "algorithm/natural_ils.hpp"
#include "config/run_config.hpp"
#include "config/solver_config.hpp"
#include "domain/wireless_data.hpp"
#include "model/extensive_form.hpp"
#include "solver/copt_solver.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace {

std::string BuildCaseLabel(const RunConfig& config)
{
    std::string scale = config.dataName;
    if (scale == "tower" || scale == "tower_base") scale = "tower_base";
    else if (scale.rfind("tower_", 0) == 0) scale = scale.substr(6);
    return scale + "_" + std::to_string(config.scenarioCount) +
        "_cap" + std::to_string(config.chargerCapacity);
}

void RedirectOutput(const std::filesystem::path& path)
{
#ifdef _WIN32
    const std::string nativePath = path.string();
    HANDLE handle = CreateFileA(nativePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot open log file: " + nativePath);
    }
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_TEXT);
    if (descriptor == -1) {
        CloseHandle(handle);
        throw std::runtime_error("Cannot attach log file: " + nativePath);
    }
    if (_dup2(descriptor, _fileno(stdout)) != 0 ||
        _dup2(descriptor, _fileno(stderr)) != 0) {
        _close(descriptor);
        throw std::runtime_error("Cannot redirect output to log file: " + nativePath);
    }
    _close(descriptor);
#else
    std::freopen(path.string().c_str(), "w", stdout);
    std::freopen(path.string().c_str(), "a", stderr);
#endif
    std::cout.clear();
    std::cerr.clear();
}

std::filesystem::path StartLog(const RunConfig& config)
{
    const std::string label = BuildCaseLabel(config);
    const std::filesystem::path directory = std::filesystem::path("logs") / label;
    std::filesystem::create_directories(directory);
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H-%M-%S", &localTime);
    return directory / (label + "_" + config.algorithm + "_" + config.problem +
        "_" + timestamp + ".log");
}

void PrintRuntimeOptions(const RunConfig& config)
{
    std::cout << "Runtime options: facility_cost_scale=" << config.facilityCostScale
              << ", energy_scale=" << config.energyScale
              << ", scenario_count=" << config.scenarioCount
              << ", scenario_start=" << config.scenarioStart
              << ", cap=" << config.chargerCapacity
              << ", dwc_top_n=" << config.dwcTopN
              << ", wireless_option=" << (config.allowWirelessCharging ? 1 : 0)
              << ", rsp=" << (config.useRsp ? 1 : 0)
              << ", preprocess_bounds=" << (config.preprocessBounds ? 1 : 0)
              << ", phase2_gap=" << config.phase2Gap << '\n';
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        std::cout << std::fixed << std::setprecision(6);
        const ParsedCommandLine commandLine = ParseCommandLine(argc, argv);
        if (commandLine.showHelp) {
            PrintUsage();
            return 0;
        }
        if (commandLine.config.algorithm != "data-check" &&
            commandLine.config.algorithm != "ef" &&
            commandLine.config.algorithm != "natural-ils" &&
            commandLine.config.algorithm != "lifted-ils" &&
            commandLine.config.algorithm != "lifted-two-phase" &&
            commandLine.config.algorithm != "full") {
            throw std::invalid_argument("Algorithm is not implemented on the rewrite branch yet: " +
                commandLine.config.algorithm);
        }

        const SolverConfig solver = LoadSolverConfig();
        const std::filesystem::path logPath = StartLog(commandLine.config);
        std::cout << "Log file: " << logPath.string() << std::endl;
        RedirectOutput(logPath);
        PrintRuntimeOptions(commandLine.config);
        const wireless::InstanceData instance = wireless::LoadInstance(commandLine.config);
        if (commandLine.config.algorithm == "ef") {
            const solver::Result result = wireless::model::SolveExtensiveForm(
                instance, commandLine.config, solver);
            std::cout << "EF status: " << solver::ToString(result.status) << '\n';
            if (result.hasSolution) {
                std::cout << "UB: " << result.objective << ", LB: " << result.bound
                          << ", gap=" << result.relativeGap * 100.0 << "%\n";
            }
            std::cout << "EF solve time: " << result.seconds << " s\n";
            return result.status == solver::Status::Optimal ||
                (result.status == solver::Status::TimeLimit && result.hasSolution) ? 0 : 1;
        }

        if (commandLine.config.algorithm == "natural-ils") {
            const solver::Result result = wireless::algorithm::SolveNaturalIls(
                instance, commandLine.config, solver);
            return result.status == solver::Status::Optimal ? 0 : 1;
        }

        if (commandLine.config.algorithm == "lifted-ils") {
            const solver::Result result = wireless::algorithm::SolveLiftedIls(
                instance, commandLine.config, solver);
            return result.status == solver::Status::Optimal ? 0 : 1;
        }

        if (commandLine.config.algorithm == "lifted-two-phase") {
            const solver::Result result = wireless::algorithm::SolveLiftedTwoPhase(
                instance, commandLine.config, solver);
            return result.status == solver::Status::Optimal ? 0 : 1;
        }

        if (commandLine.config.algorithm == "full") {
            const solver::Result result = wireless::algorithm::SolveFull(
                instance, commandLine.config, solver);
            return result.status == solver::Status::Optimal ? 0 : 1;
        }

        std::cout << "data=" << instance.name
                  << ", scenarios=" << instance.scenarios.size()
                  << ", energy_per_km=" << instance.operation.energyPerKm
                  << ", charger_types=" << instance.chargers.rates.size()
                  << ", time_limit=" << solver.timeLimit << '\n';
        for (size_t index = 0; index < instance.scenarios.size(); ++index) {
            const auto& scenario = instance.scenarios[index];
            std::cout << "scenario=" << index
                      << ", folder=" << scenario.descriptor.folder
                      << ", probability=" << scenario.descriptor.probability
                      << ", buses=" << scenario.schedule.busIds.size()
                      << ", routes=" << scenario.schedule.routeNames.size()
                      << ", trips=" << scenario.schedule.stationsByTrip.size()
                      << ", stations=" << scenario.network.stations.size()
                      << ", plug_in_stations=" << scenario.network.plugInStations.size()
                      << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
