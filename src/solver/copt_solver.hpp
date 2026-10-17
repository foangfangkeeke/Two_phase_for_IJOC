#pragma once

#include "config/solver_config.hpp"

#include "coptcpp_inc/coptcpp_pch.h"

#include <string>

namespace solver {

enum class Status {
    Optimal,
    Infeasible,
    Unbounded,
    TimeLimit,
    Interrupted,
    Unknown
};

struct Result {
    Status status = Status::Unknown;
    bool hasSolution = false;
    double objective = 0.0;
    double bound = 0.0;
    double relativeGap = 0.0;
    double seconds = 0.0;
};

void Configure(Model& model, const SolverConfig& config, bool enableLog = false);
Result Solve(Model& model);
std::string ToString(Status status);

} // namespace solver
