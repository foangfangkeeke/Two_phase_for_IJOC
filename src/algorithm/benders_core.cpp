#include "benders_decomposition.hpp"

#include <cmath>
#include <stdexcept>

void InitializeBendersMasterModel(GRBModel& model, const std::vector<ProblemDataVar>& masterVarData,
    const std::vector<ProblemDataConstr>& masterConstrs, std::vector<GRBVar>& masterVars,
    GRBVar& theta, bool requireBinaryMasterVars)
{
    masterVars.clear();
    masterVars.reserve(masterVarData.size());

    GRBLinExpr objective = 0.0;
    for (const auto& var : masterVarData) {
        if (requireBinaryMasterVars && var.type != GRB_BINARY) {
            throw std::invalid_argument("Benders-style master problem requires binary first-stage variables");
        }

        GRBVar masterVar = model.addVar(var.lb, var.ub, var.obj, var.type, var.name);
        masterVars.push_back(masterVar);
        objective += var.obj * masterVar;
    }

    theta = model.addVar(0.0, GRB_INFINITY, 0.0, GRB_CONTINUOUS, "theta");
    objective += theta;

    for (const auto& constr : masterConstrs) {
        if (constr.coeffs.size() != masterVars.size()) {
            throw std::invalid_argument("Master constraint coefficient size mismatch in Benders initialization");
        }

        GRBLinExpr lhs = 0.0;
        for (size_t idx = 0; idx < masterVars.size(); ++idx) {
            lhs += constr.coeffs[idx] * masterVars[idx];
        }
        model.addConstr(lhs, constr.sense, constr.rhs, constr.name);
    }

    model.setObjective(objective, GRB_MINIMIZE);
    model.update();
}

BendersMasterSolution GetBendersMasterSolution(GRBModel& model, const std::vector<GRBVar>& masterVars,
    const GRBVar& theta)
{
    BendersMasterSolution solution;
    solution.values.assign(masterVars.size(), 0.0);

    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        double value = masterVars[idx].get(GRB_DoubleAttr_X);
        const char varType = masterVars[idx].get(GRB_CharAttr_VType);
        if (varType == GRB_BINARY || varType == GRB_INTEGER) {
            value = std::round(value);
        }
        solution.values[idx] = value;
        solution.firstStageValue += masterVars[idx].get(GRB_DoubleAttr_Obj) * solution.values[idx];
    }

    solution.thetaValue = theta.get(GRB_DoubleAttr_X);
    solution.objectiveValue = model.get(GRB_DoubleAttr_ObjVal);
    return solution;
}

BendersCutEvaluation BuildBendersCutEvaluation(const BendersCutInfo& cutInfo,
    const std::vector<GRBVar>& masterVars, const std::vector<double>& masterValues)
{
    if (cutInfo.yCoeffs.size() != masterVars.size() || masterValues.size() != masterVars.size()) {
        throw std::invalid_argument("Benders cut size mismatch with master variables");
    }

    BendersCutEvaluation evaluation;
    evaluation.expr = cutInfo.constant;
    evaluation.lhsAtCurrent = cutInfo.constant;

    for (size_t idx = 0; idx < masterVars.size(); ++idx) {
        evaluation.expr += cutInfo.yCoeffs[idx] * masterVars[idx];
        evaluation.lhsAtCurrent += cutInfo.yCoeffs[idx] * masterValues[idx];
    }

    return evaluation;
}
