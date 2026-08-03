#include "natural_ils.hpp"
#include "lifted_ils.hpp"
#include "lifted_two_phase_ils.hpp"
#include "full_method.hpp"
#include "gurobi_c++.h"
#include "milp_solver.hpp"
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
    for (int idx = 1; idx < argc; ++idx) {
        string arg = argv[idx];
        if (arg == "--alg" && idx + 1 < argc) {
            alg = argv[++idx];
        } else if (arg == "--pb" && idx + 1 < argc) {
            pb = argv[++idx];
        } else if (arg == "--data" && idx + 1 < argc) {
            dataType = argv[++idx];
        }
    }

    const string algLower = Tools::ToLower(alg);
    const string pbLower = Tools::ToLower(pb);
    const string dataFolder = dataType + "_data";

    // ── Log file setup ──
    const filesystem::path logDir = filesystem::path("logs") / dataType;
    filesystem::create_directories(logDir);
    const auto now = chrono::system_clock::now();
    const time_t tt = chrono::system_clock::to_time_t(now);
    tm tmBuf;
    localtime_s(&tmBuf, &tt);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tmBuf);
    const filesystem::path logPath =
        logDir / (alg + "_" + pb + "_" + ts + ".log");

    cout << "Log file: " << logPath.string() << endl;
    fflush(stdout);
    RedirectProcessOutputToLog(logPath);

    // ... (algorithm dispatch below; stdout/stderr are redirected to the log file)

    if (pbLower == "wireless") {
        MILPSolver solver;
        solver.SetSolverConfig(LoadSolverConfig());

        if (algLower == "solver") {
            solver.SetAlgorithm(std::make_unique<UsingSolver>(WIRELESS_CHARGING_NO_PLUGIN, dataFolder));
        } else if (algLower == "natural-ils") {
            solver.SetAlgorithm(std::make_unique<WirelessNoPlugInIntegerLShaped>(dataFolder));
        } else {
            cout << "invalid algorithm " << alg << " for problem " << pb << endl;
            cout << "valid wireless algorithms: solver, natural-ils" << endl;
            return 1;
        }

        try {
            return solver.Run() == OK ? 0 : 1;
        } catch (const std::exception& e) {
            cout << "Error: " << e.what() << endl;
            return 1;
        }
    }

    if (pbLower == "wireless_reform") {
        MILPSolver solver;
        solver.SetSolverConfig(LoadSolverConfig());

        if (algLower == "lifted-ils") {
            solver.SetAlgorithm(std::make_unique<LiftedILS>(
                WIRELESS_CAPACITY_REFORM, dataFolder, 5000));
        } else if (algLower == "lifted-monotone") {
            solver.SetAlgorithm(std::make_unique<LiftedILS>(
                WIRELESS_CAPACITY_REFORM, dataFolder, 5000, 1e-6, true));
        } else if (algLower == "lifted-two-phase") {
            solver.SetAlgorithm(std::make_unique<LiftedTwoPhaseILS>(
                WIRELESS_CAPACITY_REFORM, dataFolder, 5000));
        } else if (algLower == "lifted-two-phase-no-rsp") {
            solver.SetAlgorithm(std::make_unique<LiftedTwoPhaseILS>(
                WIRELESS_CAPACITY_REFORM, dataFolder, 5000, 1e-6, false));
        } else if (algLower == "full") {
            solver.SetAlgorithm(std::make_unique<FullMethod>(
                WIRELESS_CAPACITY_REFORM, dataFolder, 5000));
        } else {
            cout << "invalid algorithm " << alg << " for problem " << pb << endl;
            cout << "valid wireless_reform algorithms: lifted-ils, lifted-monotone, lifted-two-phase, lifted-two-phase-no-rsp, full" << endl;
            return 1;
        }

        try {
            return solver.Run() == OK ? 0 : 1;
        } catch (const std::exception& e) {
            cout << "Error: " << e.what() << endl;
            return 1;
        }
    }

    cout << "No experiments registered for problem " << pb << endl;
    cout << "Usage: wireless.exe --pb <problem> --alg <algorithm> --data <test|tower>" << endl;
    return 1;
}
