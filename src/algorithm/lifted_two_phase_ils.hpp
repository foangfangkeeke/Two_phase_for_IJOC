#pragma once

#include "lshape.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class LiftedTwoPhaseILS : public IMILPAlgorithmStrategy {
public:
    LiftedTwoPhaseILS(ProblemType problemType, std::string dataFolder = "test_data",
        int maxIters = 500, double tol = 1e-6);

    Status Initialize() override;
    Status Solve() override;
    ~LiftedTwoPhaseILS() override;

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
        std::vector<double>& scenarioSubObjs, std::vector<LShapeCutInfo>& scenarioCutInfos,
        bool relaxed);

    Status ApplyWarmStart();
    void UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue);
    void PrintBestSolution() const;

    ProblemType problemType;
    std::string dataFolder;
    int maxIters;
    double tolerance;

    std::unique_ptr<GRBEnv> env;
    std::shared_ptr<GRBModel> model;
    std::unique_ptr<ProblemData> problemDataOwned;
    ProblemData* problemData = nullptr;
    std::unique_ptr<IDataInit_LShape> dataIniter;
    std::vector<std::unique_ptr<ScenarioSubProblemState>> subProblems;
    std::vector<GRBVar> zVars;
    GRBVar theta;
    std::vector<GRBVar> scenarioThetaVars;
    std::vector<double> phase1ZValues;

    long long phase1ElapsedMs = 0;
    std::chrono::steady_clock::time_point fullAlgorithmStart;
    std::chrono::steady_clock::time_point phase2Start;

    double globalLowerBound;
    double bestUpperBound;
    double bestLowerBound;
    std::vector<double> incumbentZValues;
    double incumbentSecondStageValue;
};
