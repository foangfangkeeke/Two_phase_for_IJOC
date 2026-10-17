#pragma once

#include "config/run_config.hpp"
#include "config/solver_config.hpp"
#include "domain/wireless_data.hpp"
#include "solver/copt_solver.hpp"

namespace wireless::algorithm {

solver::Result SolveLiftedIls(const InstanceData& instance,
    const RunConfig& runConfig, const SolverConfig& solverConfig);

} // namespace wireless::algorithm
