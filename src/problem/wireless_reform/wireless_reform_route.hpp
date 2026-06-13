#pragma once
#include <string>
#include <vector>
#include <utility>
#include "lshape.hpp"

// RHS constraint for battery capacity: RHS = beta * routeE + constant
struct RouteBatteryRhsConstr {
    GRBConstr constr;
    int routeId;
    double beta;
    double constant;
};

// Per-route model data
struct RouteInfo {
    int routeId = 0;
    std::vector<int> ebIndices;  // EB indices in original schedulingCfg ordering
    int numEBs = 0;              // number of EBs in this route
};

// Sub-problem for a single (scenario, route) pair — pure LP (all continuous)
class RouteSubProblem : public ISubProblem_LShape {
public:
    void SetRouteInfo(const RouteInfo& ri) { routeInfo = ri; }

    int ScenarioCount(const ProblemData& problemData) const override;
    void InitSubProblem(const ProblemData& problemData, int scenarioIndex, GRBModel& subModel,
        LShapeSubProblemContext& context) override;
    void UpdateSubProblem(const ProblemData& problemData, GRBModel& subModel,
        LShapeSubProblemContext& context, const std::vector<double>& zValues) override;
    Status SolveSubProblem(const ProblemData& problemData, GRBModel& subModel,
        LShapeSubProblemContext& context, const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj) override;
    Status SolveRelaxedSubProblem(const ProblemData& problemData, GRBModel& subModel,
        LShapeSubProblemContext& context, const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj) override;
    void SetLowerBound(double lowerBound) override;

    std::unique_ptr<ISubProblem_LShape> Clone() const override;

private:
    int scenarioIndex = 0;
    RouteInfo routeInfo;
    bool lowerBoundReady = false;
    double qLowerBound = 0.0;
    bool lowerBoundCutAdded = false;
    std::vector<RouteBatteryRhsConstr> batteryRhsConstrs;

    // Flat array index: [scenario][routeId] → [(capacity, masterVarIndex), ...]
    // Eliminates O(N×M) string scanning in dual extraction.
    using CapacityVarIdx = std::vector<std::vector<std::vector<std::pair<int, size_t>>>>;
    CapacityVarIdx capacityVarIdx;
};
