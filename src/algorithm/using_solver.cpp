#include "using_solver.hpp"
#include "wireless.hpp"
#include "tools.hpp"

#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

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
    env->set("LogFile", "gurobi_log.txt");
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
    if (!exportSolution) return;

    const fs::path exportDir = exportDirectory.empty()
        ? fs::path("results") / dataFolder / "solver_solution"
        : fs::path(exportDirectory);
    fs::create_directories(exportDir);

    {
        std::ofstream out(exportDir / "solution_metadata.csv");
        out << std::fixed << std::setprecision(6);
        out << "metric,value\n";
        out << "upper_bound," << ub << '\n';
        out << "lower_bound," << lb << '\n';
        out << "solve_elapsed_ms," << solveMs << '\n';
        out << "wireless_option," << (allowWirelessCharging ? 1 : 0) << '\n';
    }

    {
        std::ofstream out(exportDir / "model_variables.csv");
        out << std::fixed << std::setprecision(6);
        out << "index,var_name,value,obj,lb,ub,vtype\n";

        const int numVars = model->get(GRB_IntAttr_NumVars);
        GRBVar* vars = model->getVars();
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
        delete[] vars;
    }

    std::cout << "Solution export written to " << exportDir.string() << std::endl;
}

UsingSolver::~UsingSolver() {};
