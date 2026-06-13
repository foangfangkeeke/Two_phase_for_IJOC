#pragma once

#include "lshape.hpp"

#include <memory>
#include <string>
#include <vector>

/// Two-phase Benders algorithm:
///   Phase 1: sg-relax (no charger capacity, per-route LP sub-problems)
///   Phase 2: sg-full (full charger capacity x_bns_t, station-group MIP sub-problems)
///
/// Phase 2 is warm-started from the Phase 1 incumbent solution.
class LShapeRelaxedStation : public IMILPAlgorithmStrategy {
public:
    LShapeRelaxedStation(ProblemType problemType, std::string dataFolder = "test_data",
        int maxIters = 500, double tol = 1e-6);
    Status Initialize() override;
    Status Solve() override;
    ~LShapeRelaxedStation() override;

private:
    ProblemType problemType;
    std::string dataFolder;
    int maxIters;
    double tolerance;

    // Phase 1 results, used to warm-start Phase 2
    std::vector<double> phase1ZValues;
    double phase1UpperBound = 0.0;

    // Phase 2: station-group Benders (sg-full, no per-station mode)
    std::unique_ptr<IMILPAlgorithmStrategy> phase2;
};
