#pragma once

#include "config/solver_config.hpp"
#include "domain/wireless_data.hpp"
#include "model/model_helpers.hpp"
#include "solver/copt_solver.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace wireless::algorithm {

struct BatteryLevelRef {
    int capacity = 0;
    size_t masterIndex = 0;
};

struct RouteRelaxationResult {
    solver::Status status = solver::Status::Unknown;
    double objective = 0.0;
    double seconds = 0.0;
    std::vector<double> coefficients;
    double constant = 0.0;
};

class RouteRelaxation {
public:
    RouteRelaxation(const InstanceData& instance, size_t scenarioIndex, size_t routeIndex,
        const std::vector<model::DwcLink>& candidates,
        const std::vector<BatteryLevelRef>& batteryLevels,
        size_t masterVariableCount, const SolverConfig& solverConfig);

    RouteRelaxationResult Evaluate(const std::vector<double>& masterValues,
        double remainingSeconds);

    size_t ScenarioIndex() const { return scenarioIndex_; }
    size_t RouteIndex() const { return routeIndex_; }
    const std::vector<size_t>& RelevantFacilities() const { return relevantFacilities_; }
    const std::vector<BatteryLevelRef>& BatteryLevels() const { return batteryLevels_; }

private:
    struct LinkConstraint {
        Var variable;
        Constraint constraint;
        size_t masterIndex = 0;
    };

    size_t scenarioIndex_ = 0;
    size_t routeIndex_ = 0;
    size_t masterVariableCount_ = 0;
    std::vector<BatteryLevelRef> batteryLevels_;
    std::vector<size_t> relevantFacilities_;
    std::unique_ptr<Envr> environment_;
    std::unique_ptr<Model> model_;
    std::vector<LinkConstraint> links_;
};

} // namespace wireless::algorithm
