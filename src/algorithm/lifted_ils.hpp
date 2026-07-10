#pragma once

#include "lshape_station.hpp"

#include <string>

class LiftedILS : public IMILPAlgorithmStrategy {
public:
    LiftedILS(ProblemType problemType, std::string dataFolder = "test_data",
        int maxIters = 500, double tol = 1e-6);

    Status Initialize() override;
    Status Solve() override;
    ~LiftedILS() override;

private:
    LShapeStation engine_;
};
