#include "barp_s_integer_l_shaped.hpp"
#include "gurobi_c++.h"
#include "milp_solver.hpp"
#include "solver_config.hpp"
#include "tools.hpp"
#include "using_solver.hpp"

#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;

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

    if (pbLower == "wireless") {
        MILPSolver solver;
        solver.SetSolverConfig(LoadSolverConfig());

        if (algLower == "solver") {
            solver.SetAlgorithm(std::make_unique<UsingSolver>(WIRELESS_CHARGING_NO_PLUGIN, dataFolder));
        } else if (algLower == "l2") {
            solver.SetAlgorithm(std::make_unique<WirelessNoPlugInIntegerLShaped>(dataFolder));
        } else {
            cout << "invalid algorithm " << alg << " for problem " << pb << endl;
            return 0;
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
    return 0;
}
