#pragma once

#include "lshape.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>

class LShapeStation : public IMILPAlgorithmStrategy {
public:
    // twoPhase=false => lifted-ils
    // twoPhase=true  => lifted-two-phase
    LShapeStation(ProblemType problemType, std::string dataFolder = "test_data",
        bool twoPhase = false, int maxIters = 500, double tol = 1e-6);
    Status Initialize() override;
    Status Solve() override;
    ~LShapeStation() override;

    // Accept warm-start solution from an external Phase 1 relaxation.
    void SetExternalWarmStart(const std::vector<double>& zValues, double upperBound);

private:
    struct StationSubProblemState;

    struct CutEval {
        GRBLinExpr expr = 0.0;
        double lhsAtCurrent = 0.0;
        std::string name;
    };

    CutEval BuildContinuousCut(const LShapeCutInfo& cutInfo, const std::vector<double>& zCurrent,
        int iter, int subProblemIndex) const;

    Status EvaluateSubProblems(const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj,
        std::vector<double>& groupSubObjs, std::vector<LShapeCutInfo>& groupCutInfos,
        bool relaxed = false);

    Status ApplyWarmStart();
    void UpdateIncumbent(const std::vector<double>& zValues, double secondStageValue);
    void PrintBestSolution() const;
    std::string AlgorithmLabel() const;

    ProblemType problemType;
    std::string dataFolder;
    int maxIters;
    double tolerance;
    bool twoPhase;

    std::unique_ptr<GRBEnv> env;
    std::shared_ptr<GRBModel> model;
    std::unique_ptr<ProblemData> problemData;
    std::unique_ptr<IDataInit_LShape> dataIniter;
    std::vector<std::unique_ptr<StationSubProblemState>> subProblems;
    std::vector<GRBVar> zVars;
    GRBVar theta;
    std::vector<GRBVar> groupThetaVars;  // multi-cut: one theta per subproblem

    double globalLowerBound;
    double bestUpperBound;
    double bestLowerBound;
    std::vector<double> incumbentZValues;
    double incumbentSecondStageValue;
    std::chrono::steady_clock::time_point algorithmStart;

    // External warm start from a Phase 1 relaxation.
    bool externalWarmStartReady_ = false;
    std::vector<double> externalWarmStartZValues_;
    double externalWarmStartUpperBound_ = 0.0;
};
