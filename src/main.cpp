#include "gurobi_c++.h"
#include "solver_config.hpp"
#include "tools.hpp"

#include <iomanip>
#include <iostream>
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

    string alg = "none";
    string pb = "none";
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

    cout << "No experiments registered yet." << endl;
    cout << "Usage: wireless.exe --pb <problem> --alg <algorithm> --data <test|tower>" << endl;
    return 0;
}
