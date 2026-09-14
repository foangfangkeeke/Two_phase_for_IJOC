#include "natural_ils.hpp"
#include "lifted_ils.hpp"
#include "lifted_two_phase_ils.hpp"
#include "gurobi_c++.h"
#include "milp_solver.hpp"
#include "runtime_options.hpp"
#include "solver_config.hpp"
#include "tools.hpp"
#include "using_solver.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

using namespace std;

namespace {

string BuildCaseLabel(const string& dataType, const RuntimeOptions& runtimeOptions)
{
    string scaleLabel = dataType;
    if (dataType == "tower_base" || dataType == "tower") {
        scaleLabel = "tower_base";
    } else if (dataType.rfind("tower_", 0) == 0) {
        scaleLabel = dataType.substr(6);
    }
    return scaleLabel + "_" + to_string(runtimeOptions.scenarioCount) +
        "_cap" + to_string(runtimeOptions.chargerCapacity);
}

void RedirectProcessOutputToLog(const filesystem::path& logPath)
{
#ifdef _WIN32
    const std::string path = logPath.string();
    HANDLE logHandle = CreateFileA(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logHandle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot open log file for writing: " + path);
    }

    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(logHandle), _O_TEXT);
    if (fd == -1) {
        CloseHandle(logHandle);
        throw std::runtime_error("Cannot attach log file handle: " + path);
    }

    if (_dup2(fd, _fileno(stdout)) != 0 || _dup2(fd, _fileno(stderr)) != 0) {
        _close(fd);
        throw std::runtime_error("Cannot redirect stdout/stderr to log file: " + path);
    }
    _close(fd);
#else
    freopen(logPath.string().c_str(), "w", stdout);
    freopen(logPath.string().c_str(), "a", stderr);
#endif
    cout.clear();
    cerr.clear();
}

} // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    cout << fixed << setprecision(6);

    string alg = "solver";
    string pb = "wireless";
    string dataType = "test";
    bool exportSolution = false;
    bool exportSummary = false;
    bool allowWirelessCharging = true;
    bool rspOptionSet = false;
    bool rspOptionValue = true;
    int phaseProgressLogOverride = -1;
    int phase2MasterLogOverride = -1;
    RuntimeOptions runtimeOptions;
    string exportDirectory;
    for (int idx = 1; idx < argc; ++idx) {
        string arg = argv[idx];
        if (arg == "--alg" && idx + 1 < argc) {
            alg = argv[++idx];
        } else if (arg == "--pb" && idx + 1 < argc) {
            pb = argv[++idx];
        } else if (arg == "--data" && idx + 1 < argc) {
            dataType = argv[++idx];
        } else if (arg == "--wireless-option" && idx + 1 < argc) {
            allowWirelessCharging = std::stoi(argv[++idx]) != 0;
        } else if (arg == "--rsp" && idx + 1 < argc) {
            rspOptionSet = true;
            rspOptionValue = std::stoi(argv[++idx]) != 0;
        } else if (arg == "--facility-cost-scale" && idx + 1 < argc) {
            runtimeOptions.facilityCostScale = std::stod(argv[++idx]);
        } else if (arg == "--energy-scale" && idx + 1 < argc) {
            runtimeOptions.energyScale = std::stod(argv[++idx]);
        } else if (arg == "--scenario-count" && idx + 1 < argc) {
            runtimeOptions.scenarioCount = std::stoi(argv[++idx]);
        } else if (arg == "--cap" && idx + 1 < argc) {
            runtimeOptions.chargerCapacity = std::stoi(argv[++idx]);
        } else if (arg == "--export-solution") {
            exportSolution = true;
        } else if (arg == "--export-summary") {
            exportSummary = true;
        } else if (arg == "--export-dir" && idx + 1 < argc) {
            exportDirectory = argv[++idx];
        } else if (arg == "--phase-progress-log" && idx + 1 < argc) {
            phaseProgressLogOverride = std::stoi(argv[++idx]);
        } else if (arg == "--phase2-master-log" && idx + 1 < argc) {
            phase2MasterLogOverride = std::stoi(argv[++idx]);
        }
    }

    const string algLower = Tools::ToLower(alg);
    const string pbLower = Tools::ToLower(pb);
    const string dataFolder = dataType + "_data";
    if (runtimeOptions.facilityCostScale <= 0.0 || runtimeOptions.energyScale <= 0.0 ||
        runtimeOptions.scenarioCount <= 0 || runtimeOptions.chargerCapacity <= 0) {
        throw std::invalid_argument("Runtime numeric options must be positive");
    }
    SetRuntimeOptions(runtimeOptions);
    const auto UseRsp = [rspOptionSet, rspOptionValue](bool defaultValue) {
        return rspOptionSet ? rspOptionValue : defaultValue;
    };
    SolverConfig solverConfig = LoadSolverConfig();
    if (phaseProgressLogOverride >= 0) {
        solverConfig.phaseProgressLog = phaseProgressLogOverride != 0;
    }
    if (phase2MasterLogOverride >= 0) {
        solverConfig.phase2MasterLog = phase2MasterLogOverride != 0;
    }

    // ── Log file setup ──
    const string caseLabel = BuildCaseLabel(dataType, runtimeOptions);
    const filesystem::path logDir = filesystem::path("logs") / caseLabel;
    filesystem::create_directories(logDir);
    const auto now = chrono::system_clock::now();
    const time_t tt = chrono::system_clock::to_time_t(now);
    tm tmBuf;
    localtime_s(&tmBuf, &tt);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tmBuf);
    const filesystem::path logPath =
        logDir / (caseLabel + "_" + alg + "_" + pb + "_" + ts + ".log");

    cout << "Log file: " << logPath.string() << endl;
    fflush(stdout);
    RedirectProcessOutputToLog(logPath);

    // ... (algorithm dispatch below; stdout/stderr are redirected to the log file)
    cout << "Runtime options: facility_cost_scale=" << runtimeOptions.facilityCostScale
         << ", energy_scale=" << runtimeOptions.energyScale
         << ", scenario_count=" << runtimeOptions.scenarioCount
         << ", cap=" << runtimeOptions.chargerCapacity << endl;

    if (pbLower == "wireless") {
        MILPSolver solver;
        solver.SetSolverConfig(solverConfig);

        if (algLower == "solver") {
            auto algorithm = std::make_unique<UsingSolver>(
                WIRELESS_CHARGING_NO_PLUGIN, dataFolder, allowWirelessCharging);
            algorithm->SetExportSolution(exportSolution);
            algorithm->SetExportSummary(exportSummary);
            algorithm->SetExportDirectory(exportDirectory);
            solver.SetAlgorithm(std::move(algorithm));
        } else if (algLower == "natural-ils") {
            solver.SetAlgorithm(std::make_unique<WirelessNoPlugInIntegerLShaped>(
                dataFolder, 2147483647, 1e-6, UseRsp(true)));
        } else {
            cout << "invalid algorithm " << alg << " for problem " << pb << endl;
            cout << "valid wireless algorithms: solver, natural-ils" << endl;
            return 1;
        }

        try {
            return solver.Run() == OK ? 0 : 1;
        } catch (const GRBException& e) {
            cout << "Gurobi error: code=" << e.getErrorCode()
                 << ", message=" << e.getMessage() << endl;
            return 1;
        } catch (const std::exception& e) {
            cout << "Error: " << e.what() << endl;
            return 1;
        }
    }

    if (pbLower == "wireless_reform") {
        MILPSolver solver;
        solver.SetSolverConfig(solverConfig);

        if (algLower == "lifted-ils" || algLower == "lifted-monotone") {
            const bool useMonotoneCuts = algLower == "lifted-monotone";
            solver.SetAlgorithm(std::make_unique<LiftedILS>(
                WIRELESS_CAPACITY_REFORM, dataFolder, 5000, 1e-6, useMonotoneCuts, UseRsp(true)));
        } else if (algLower == "lifted-two-phase" || algLower == "full") {
            const bool useMonotoneCuts = algLower == "full";
            auto algorithm = std::make_unique<LiftedTwoPhaseILS>(
                WIRELESS_CAPACITY_REFORM, dataFolder, 5000, 1e-6, UseRsp(true), useMonotoneCuts);
            algorithm->SetExportSolution(exportSolution);
            algorithm->SetExportSummary(exportSummary);
            algorithm->SetExportDirectory(exportDirectory);
            solver.SetAlgorithm(std::move(algorithm));
        } else {
            cout << "invalid algorithm " << alg << " for problem " << pb << endl;
            cout << "valid wireless_reform algorithms: lifted-ils, lifted-monotone, lifted-two-phase, full" << endl;
            return 1;
        }

        try {
            return solver.Run() == OK ? 0 : 1;
        } catch (const GRBException& e) {
            cout << "Gurobi error: code=" << e.getErrorCode()
                 << ", message=" << e.getMessage() << endl;
            return 1;
        } catch (const std::exception& e) {
            cout << "Error: " << e.what() << endl;
            return 1;
        }
    }

    cout << "No experiments registered for problem " << pb << endl;
    cout << "Usage: wireless.exe --pb <problem> --alg <algorithm> --data <test|tower>"
         << " [--facility-cost-scale <positive>] [--energy-scale <positive>]"
         << " [--scenario-count <positive>] [--cap <positive>]"
         << " [--rsp <0|1>]"
         << " [--phase-progress-log <0|1>] [--phase2-master-log <0|1>]"
         << " [--export-solution] [--export-summary] [--export-dir <path>]" << endl;
    return 1;
}
