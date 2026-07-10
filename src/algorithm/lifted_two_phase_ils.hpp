#pragma once

#include "lshape_station.hpp"

#include <string>

class LiftedTwoPhaseILS : public IMILPAlgorithmStrategy {
public:
    LiftedTwoPhaseILS(ProblemType problemType, std::string dataFolder = "test_data",
        int maxIters = 500, double tol = 1e-6);

    Status Initialize() override;
    Status Solve() override;
    ~LiftedTwoPhaseILS() override;

private:
    LShapeStation engine_;
};
