#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include "lshape.hpp"

// Battery RHS constraint: RHS = beta * routeE + constant
struct BatteryRhsConstr {
    GRBConstr constr;
    int routeId;
    double beta;
    double constant;
};

// Per-scenario operational subproblem info
struct ScenarioSubproblemInfo {
    int scenarioSubproblemId;
    int scenarioIndex;
    std::vector<int> routeIds;    // routes included in this scenario subproblem
    std::vector<int> ebIndices;   // all EBs belonging to those routes
};

class WirelessReformDataInit : public IDataInit_LShape {
public:
    explicit WirelessReformDataInit(std::string dataFolder = "test_data");
    void DataInit(ProblemData& data) override;
    std::vector<ProblemDataConstr> ConstrInit(ProblemData& data) override;
    std::vector<double> BuildWarmStartMasterValues(const ProblemData& data) const override;
    bool IsWarmStartMasterFeasible(const ProblemData& data, const std::vector<double>& zValues,
        double tolerance) const override;

private:
    std::string dataFolder;
};

class WirelessScenarioSubProblem : public ISubProblem_LShape {
public:
    void SetScenarioSubproblemInfo(const ScenarioSubproblemInfo& info) { scenarioInfo = info; }
    void SetRelaxChargerCapacity(bool relax) { relaxChargerCapacity = relax; }

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
    Status SolveRelaxedModel(const ProblemData& problemData, GRBModel& subModel,
        const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj);

    ScenarioSubproblemInfo scenarioInfo;
    bool lowerBoundReady = false;
    double qLowerBound = 0.0;
    bool lowerBoundCutAdded = false;
    bool relaxChargerCapacity = false;
    int numRoutes = 0;
    std::vector<BatteryRhsConstr> batteryRhsConstrs;
    std::vector<int> routeIdByEB;

    // [scenarioIdx][routeId] â†?[(capacity, masterVarIndex), ...]  flat array, no hashing
    std::vector<std::vector<std::vector<std::pair<int, size_t>>>> capacityVarIdx;
};
