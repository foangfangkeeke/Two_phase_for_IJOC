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

// Per-station-group subproblem info
struct StationGroupInfo {
    int stationGroupId;           // charger station ID
    int scenarioIndex;
    std::vector<int> routeIds;    // routes that visit this station
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

class WirelessStationGroupSubProblem : public ISubProblem_LShape {
public:
    void SetStationGroupInfo(const StationGroupInfo& info) { sgInfo = info; }
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
    void InitRelaxedSubProblem(const ProblemData& problemData, GRBModel& subModel);
    Status SolveRelaxedModel(const ProblemData& problemData, const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj);

    StationGroupInfo sgInfo;
    bool lowerBoundReady = false;
    double qLowerBound = 0.0;
    bool lowerBoundCutAdded = false;
    bool relaxChargerCapacity = false;
    int numRoutes = 0;
    std::vector<BatteryRhsConstr> batteryRhsConstrs;
    std::vector<int> routeIdByEB;
    std::unique_ptr<GRBModel> relaxedModel;
    std::vector<GRBConstr> relaxedFacilityConstrs;
    std::vector<BatteryRhsConstr> relaxedBatteryConstrs;

    // [scenarioIdx][routeId] → [(capacity, masterVarIndex), ...]  flat array, no hashing
    std::vector<std::vector<std::vector<std::pair<int, size_t>>>> capacityVarIdx;
};
