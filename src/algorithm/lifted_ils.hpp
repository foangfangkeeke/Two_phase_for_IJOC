#pragma once

#include "lshape.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class LiftedILS : public IMILPAlgorithmStrategy {
public:
    LiftedILS(ProblemType problemType, std::string dataFolder = "test_data",
        int maxIters = 500, double tol = 1e-6, bool useMonotoneCuts = false);

    Status Initialize() override;
    Status Solve() override;
    ~LiftedILS() override;

private:
    struct ScenarioSubProblemState;

    struct CutEval {
        GRBLinExpr expr = 0.0;
        double lhsAtCurrent = 0.0;
        std::string name;
    };

    CutEval BuildContinuousCut(const LShapeCutInfo& cutInfo, const std::vector<double>& zCurrent,
        int iter, int subProblemIndex) const;

    Status EvaluateSubProblems(const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj,
        std::vector<double>& scenarioSubObjs, std::vector<LShapeCutInfo>& scenarioCutInfos);

    Status ApplyWarmStart();
    void UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue);
    void PrintBestSolution() const;

    ProblemType problemType;
    std::string dataFolder;
    int maxIters;
    double tolerance;
    bool useMonotoneCuts;

    std::unique_ptr<GRBEnv> env;
    std::shared_ptr<GRBModel> model;
    std::unique_ptr<ProblemData> problemData;
    std::unique_ptr<IDataInit_LShape> dataIniter;
    std::vector<std::unique_ptr<ScenarioSubProblemState>> subProblems;
    std::vector<GRBVar> zVars;
    GRBVar theta;
    std::vector<GRBVar> scenarioThetaVars;

    double globalLowerBound;
    double bestUpperBound;
    double bestLowerBound;
    std::vector<double> incumbentZValues;
    double incumbentSecondStageValue;
    std::chrono::steady_clock::time_point algorithmStart;
};
