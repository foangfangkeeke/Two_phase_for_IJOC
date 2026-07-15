#pragma once

#include "lshape.hpp"
#include "gurobi_c++.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class LShapeRelaxed : public IMILPAlgorithmStrategy {
public:
    LShapeRelaxed(ProblemType problemType, std::string dataFolder = "test_data",
        int maxIters = 500, double tol = 1e-6);
    Status Initialize() override;
    Status Solve() override;
    ~LShapeRelaxed() override;

    void SetQuiet(bool quiet) { quiet_ = quiet; }

    // Expose Phase 1 results and internals for Phase 2 reuse
    const std::vector<double>& GetIncumbentZValues() const { return incumbentZValues; }
    double GetBestUpperBound() const { return bestUpperBound; }
    double GetBestLowerBound() const { return bestLowerBound; }
    std::shared_ptr<GRBModel> GetModel() const { return model; }
    std::vector<GRBVar>& GetZVars() { return zVars; }
    const std::vector<std::vector<GRBVar>>& GetRouteThetaVars() const { return routeThetaVars; }
    ProblemData* GetProblemData() const { return problemData.get(); }
    std::unique_ptr<GRBEnv> ReleaseEnv() { return std::move(env); }
    std::unique_ptr<ProblemData> ReleaseProblemData() { return std::move(problemData); }

private:
    struct RouteSubProblemState;

    struct CutEval {
        GRBLinExpr expr = 0.0;
        double lhsAtCurrent = 0.0;
        std::string name;
    };

    CutEval BuildContinuousCut(const LShapeCutInfo& cutInfo, const std::vector<double>& zCurrent,
        int iter, int subProblemIndex) const;

    Status EvaluateSubProblems(const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj,
        std::vector<double>& routeSubObjs, std::vector<LShapeCutInfo>& routeCutInfos);

    Status ApplyWarmStart();
    void UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue);
    void PrintBestSolution() const;

    ProblemType problemType;
    std::string dataFolder;
    int maxIters;
    double tolerance;

    std::unique_ptr<GRBEnv> env;
    std::shared_ptr<GRBModel> model;
    std::unique_ptr<ProblemData> problemData;
    std::unique_ptr<IDataInit_LShape> dataIniter;
    std::vector<std::unique_ptr<RouteSubProblemState>> subProblems;
    std::vector<GRBVar> zVars;
    std::vector<std::vector<GRBVar>> routeThetaVars;
    GRBVar theta;

    bool quiet_ = false;
    double globalLowerBound;
    double bestUpperBound;
    double bestLowerBound;
    std::vector<double> incumbentZValues;
    double incumbentSecondStageValue;
    std::chrono::steady_clock::time_point algorithmStart;
};
