#pragma once

#include "milp_solver.hpp"
#include "benders_decomposition.hpp"

#include <memory>
#include <vector>
#include <string>

class ProblemData;

using LShapeCutInfo = BendersCutInfo;
using LShapeSubProblemContext = BendersSubProblemContext;

class IDataInit_LShape {
public:
    virtual void DataInit(ProblemData& data) = 0;
    virtual std::vector<ProblemDataConstr> ConstrInit(ProblemData& data) = 0;
    virtual std::vector<double> BuildWarmStartMasterValues(const ProblemData& data) const = 0;
    virtual bool IsWarmStartMasterFeasible(const ProblemData& data, const std::vector<double>& zValues,
        double tolerance) const = 0;
    virtual ~IDataInit_LShape() = default;
};

class ISubProblem_LShape {
public:
    virtual int ScenarioCount(const ProblemData& problemData) const = 0;
    virtual void InitSubProblem(const ProblemData& problemData, int scenarioIndex, GRBModel& subModel,
        LShapeSubProblemContext& context) = 0;
    virtual void UpdateSubProblem(const ProblemData& problemData, GRBModel& subModel,
        LShapeSubProblemContext& context, const std::vector<double>& zValues) = 0;
    virtual Status SolveSubProblem(const ProblemData& problemData, GRBModel& subModel,
        LShapeSubProblemContext& context, const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj) = 0;
    virtual Status SolveRelaxedSubProblem(const ProblemData& problemData, GRBModel& subModel,
        LShapeSubProblemContext& context, const std::vector<double>& zValues,
        LShapeCutInfo& cutInfo, double& subObj) = 0;
    virtual void SetLowerBound(double lowerBound) {}
    virtual std::unique_ptr<ISubProblem_LShape> Clone() const = 0;
    virtual ~ISubProblem_LShape() = default;
};
