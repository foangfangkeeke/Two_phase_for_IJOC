#pragma once

#include "basic_solver.hpp"
#include "milp_solver.hpp"

#include <memory>
#include <string>

class IDataInitializationStrategy_Solver {
public:
    virtual Status DataInit(GRBModel& model) = 0;
    virtual ~IDataInitializationStrategy_Solver() = default;
};

class UsingSolver final : public IMILPAlgorithmStrategy {
public:
    UsingSolver(ProblemType problemType, std::string dataFolder = "test_data",
        bool allowWirelessCharging = true);
    Status Initialize();
    Status Solve();
    void SetExportSolution(bool enabled) { exportSolution = enabled; }
    void SetExportSummary(bool enabled) { exportSummary = enabled; }
    void SetExportDirectory(std::string path) { exportDirectory = std::move(path); }
    ~UsingSolver();

private:
    ProblemType problemType;
    std::string dataFolder;
    bool allowWirelessCharging;
    bool exportSolution = false;
    bool exportSummary = false;
    std::string exportDirectory;

    std::unique_ptr<GRBEnv> env;
    std::unique_ptr<GRBModel> model;
    std::unique_ptr<IDataInitializationStrategy_Solver> dataIniter;

    void ExportSolution(double ub, double lb, long long solveMs) const;
};
