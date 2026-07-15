#pragma once

#include "lshape.hpp"
#include "gurobi_c++.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

struct StationGroupInfo;

/// Two-phase Benders algorithm:
///   Phase 1: operational relaxation with per-route LP sub-problems.
///   Phase 2: Reuses Phase 1 master model (with accumulated cuts),
///            replaces sub-problems with station-group MIPs (full charger capacity),
///            continues Benders iterations with one theta variable per station group.
class LShapeRelaxedStation : public IMILPAlgorithmStrategy {
public:
    LShapeRelaxedStation(ProblemType problemType, std::string dataFolder = "test_data",
        int maxIters = 500, double tol = 1e-6);
    Status Initialize() override;
    Status Solve() override;
    ~LShapeRelaxedStation() override;

private:
    struct StationSubProblemState;

    ProblemType problemType;
    std::string dataFolder;
    int maxIters;
    double tolerance;

    // Phase 1 results
    std::vector<double> phase1ZValues;
    double phase1UpperBound = 0.0;

    // Phase 2: master reused from Phase 1 (env & data must outlive model)
    std::unique_ptr<GRBEnv> env_;
    std::unique_ptr<ProblemData> problemDataOwned_;
    std::shared_ptr<GRBModel> model_;
    std::vector<GRBVar> zVars_;
    std::vector<GRBVar> groupThetaVars_;  // one theta per Phase 2 station group
    GRBVar theta_;
    ProblemData* problemData_ = nullptr;

    // Phase 2: station-group sub-problems
    std::vector<std::unique_ptr<StationSubProblemState>> subProblems_;

    // Benders state
    double globalLowerBound_ = 0.0;
    double bestUpperBound_ = std::numeric_limits<double>::infinity();
    double bestLowerBound_ = -std::numeric_limits<double>::infinity();
    std::vector<double> incumbentZValues_;
    double incumbentQ_ = 0.0;
    std::chrono::steady_clock::time_point fullAlgorithmStart_;
    long long phase1ElapsedMs_ = 0;
    long long phase2ElapsedMs_ = 0;
    std::chrono::steady_clock::time_point phase2Start_;
};
