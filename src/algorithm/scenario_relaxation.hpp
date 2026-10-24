#pragma once

#include "config/solver_config.hpp"
#include "domain/wireless_data.hpp"
#include "model/model_helpers.hpp"
#include "solver/copt_solver.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace wireless::algorithm {

struct ScenarioRelaxationResult {
    solver::Status status = solver::Status::Unknown;
    double objective = 0.0;
    double seconds = 0.0;
    std::vector<double> coefficients;
    double constant = 0.0;
};

class ScenarioRelaxation {
public:
    ScenarioRelaxation(const InstanceData& instance, size_t scenarioIndex,
        const std::vector<model::DwcLink>& candidates,
        const std::vector<std::vector<size_t>>& batteryMasterIndices,
        const std::vector<int>& maximumBatteryCapacities,
        size_t masterVariableCount, const SolverConfig& solverConfig,
        int threads);

    ScenarioRelaxationResult Evaluate(const std::vector<double>& masterValues,
        double remainingSeconds);

private:
    struct LinkConstraint {
        Var variable;
        Constraint constraint;
        size_t masterIndex = 0;
    };

    size_t masterVariableCount_ = 0;
    std::unique_ptr<Envr> environment_;
    std::unique_ptr<Model> model_;
    std::vector<LinkConstraint> links_;
};

} // namespace wireless::algorithm
