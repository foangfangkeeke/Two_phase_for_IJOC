#pragma once

#include "config/run_config.hpp"
#include "config/solver_config.hpp"
#include "domain/wireless_data.hpp"

namespace wireless::algorithm {

void AuditFixedPlan(const InstanceData& instance, const RunConfig& config,
    const SolverConfig& solverConfig);

} // namespace wireless::algorithm
