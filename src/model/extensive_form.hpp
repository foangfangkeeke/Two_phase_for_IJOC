#pragma once

#include "config/run_config.hpp"
#include "config/solver_config.hpp"
#include "domain/wireless_data.hpp"
#include "solver/copt_solver.hpp"

namespace wireless::model {

solver::Result SolveExtensiveForm(
    const InstanceData& instance, const RunConfig& runConfig,
    const SolverConfig& solverConfig);

} // namespace wireless::model
