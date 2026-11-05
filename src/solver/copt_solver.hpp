#pragma once

#include "config/solver_config.hpp"

#include "coptcpp_inc/coptcpp_pch.h"

#include <limits>
#include <string>

namespace solver {

enum class Status {
    Optimal,
    Infeasible,
    Unbounded,
    Numerical,
    TimeLimit,
    Interrupted,
    MemoryLimit,
    Unknown
};

struct Result {
    Status status = Status::Unknown;
    bool hasSolution = false;
    double objective = std::numeric_limits<double>::infinity();
    double bound = -std::numeric_limits<double>::infinity();
    double relativeGap = std::numeric_limits<double>::infinity();
    double seconds = 0.0;
};

Envr CreateEnvironment();
void Configure(Model& model, const SolverConfig& config, bool enableLog = false);
Result Solve(Model& model);
std::string ToString(Status status);

} // namespace solver
