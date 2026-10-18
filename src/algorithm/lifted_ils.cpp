#include "algorithm/lifted_ils.hpp"

#include "model/model_helpers.hpp"
#include "model/scenario_model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace wireless::algorithm {
namespace {

constexpr int kBatteryStep = 20;
constexpr int kMaximumIterations = 5000;
constexpr double kCutTolerance = 1e-6;

struct BatteryMasterLevel {
    int capacity = 0;
    size_t flatIndex = 0;
    Var variable;

    BatteryMasterLevel(int capacity, size_t flatIndex, Var variable)
        : capacity(capacity), flatIndex(flatIndex), variable(std::move(variable)) {}
};

using RouteBatteryLevels = std::vector<std::vector<BatteryMasterLevel>>;

struct LiftedMaster {
    std::vector<Var> designVariables;
    std::vector<double> costs;
    std::vector<Var> facilityVariables;
    std::vector<RouteBatteryLevels> batteryVariables;
    std::vector<Var> scenarioTheta;
    Var theta;

    explicit LiftedMaster(Var theta) : theta(std::move(theta)) {}
};

struct ScenarioSubproblem {
    std::unique_ptr<Envr> environment;
    std::unique_ptr<Model> model;
    std::vector<Var> facilityVariables;
    model::ScenarioVariables scenarioVariables;
    std::vector<size_t> relevantMasterIndices;
};

double ElapsedSeconds(const std::chrono::steady_clock::time_point& start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::vector<double> ReadValues(std::vector<Var>& variables)
{
    std::vector<double> values;
    values.reserve(variables.size());
    for (Var& variable : variables) {
        values.push_back(std::round(variable.Get(COPT_DBLINFO_VALUE)));
    }
    return values;
}

Expr BuildMismatch(const std::vector<Var>& variables,
    const std::vector<double>& point, const std::vector<size_t>& relevant)
{
    Expr mismatch(0.0);
    for (size_t index : relevant) {
        if (point.at(index) > 0.5) mismatch += 1.0 - variables.at(index);
        else mismatch += variables.at(index);
    }
    return mismatch;
}

std::set<model::DwcLink> ScenarioLinks(const ScenarioData& scenario)
{
    std::set<model::DwcLink> links;
    for (const auto& stations : scenario.schedule.stationsByTrip) {
        for (size_t index = 0; index + 1 < stations.size(); ++index) {
            links.emplace(stations.at(index), stations.at(index + 1));
        }
    }
    return links;
}

LiftedMaster BuildMaster(Model& master, const InstanceData& instance,
    const std::vector<model::DwcLink>& candidates)
{
    Expr objective(0.0);
    std::vector<Var> designVariables;
    std::vector<double> costs;
    std::vector<Var> facilities;
    for (model::DwcLink link : candidates) {
        const std::string name = model::DwcVariableName(link);
        const double cost = model::DwcFacilityCost(instance, link);
        const Var variable = master.AddVar(0.0, 1.0, 0.0, COPT_BINARY, name.c_str());
        designVariables.push_back(variable);
        facilities.push_back(variable);
        costs.push_back(cost);
        objective += cost * variable;
    }

    std::vector<RouteBatteryLevels> batteryVariables;
    batteryVariables.resize(instance.scenarios.size());
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        const ScheduleData& schedule = instance.scenarios.at(scenario).schedule;
        const std::vector<int> busesByRoute = model::CountBusesByRoute(schedule);
        batteryVariables.at(scenario).resize(schedule.routeNames.size());
        for (size_t route = 0; route < schedule.routeNames.size(); ++route) {
            Expr selectOne(0.0);
            const double unitCost = instance.scenarios.at(scenario).descriptor.probability *
                busesByRoute.at(route) * instance.operation.batteryUnitPrice;
            for (int capacity = static_cast<int>(instance.operation.minimumBatteryCapacity);
                 capacity <= static_cast<int>(instance.operation.maximumBatteryCapacity);
                 capacity += kBatteryStep) {
                const std::string name = "sc" + std::to_string(scenario) + "_E_r" +
                    std::to_string(route) + "_cap" + std::to_string(capacity);
                const Var variable = master.AddVar(
                    0.0, 1.0, 0.0, COPT_BINARY, name.c_str());
                const size_t flatIndex = designVariables.size();
                const double cost = unitCost * capacity;
                designVariables.push_back(variable);
                costs.push_back(cost);
                batteryVariables.at(scenario).at(route).emplace_back(
                    capacity, flatIndex, variable);
                objective += cost * variable;
                selectOne += variable;
            }
            master.AddConstr(selectOne == 1.0,
                ("selectCap_sc" + std::to_string(scenario) + "_r" +
                    std::to_string(route)).c_str());
        }
    }

    const Var theta = master.AddVar(0.0, COPT_INFINITY, 0.0, COPT_CONTINUOUS, "theta");
    objective += theta;
    LiftedMaster result(theta);
    result.designVariables = std::move(designVariables);
    result.costs = std::move(costs);
    result.facilityVariables = std::move(facilities);
    result.batteryVariables = std::move(batteryVariables);

    Expr scenarioThetaSum(0.0);
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        const Var scenarioTheta = master.AddVar(0.0, COPT_INFINITY, 0.0,
            COPT_CONTINUOUS, ("scenario_theta_p2_" + std::to_string(scenario)).c_str());
        result.scenarioTheta.push_back(scenarioTheta);
        scenarioThetaSum += scenarioTheta;
    }
    master.AddConstr(scenarioThetaSum <= result.theta, "theta_scenario_sum_p2");
    master.SetObjective(objective, COPT_MINIMIZE);
    master.Update();
    return result;
}

ScenarioSubproblem BuildSubproblem(const InstanceData& instance, size_t scenarioIndex,
    const std::vector<model::DwcLink>& candidates, const LiftedMaster& master,
    const SolverConfig& solverConfig)
{
    ScenarioSubproblem state;
    state.environment = std::make_unique<Envr>();
    state.model = std::make_unique<Model>(state.environment->CreateModel(
        ("lifted_scenario_" + std::to_string(scenarioIndex)).c_str()));
    model::FacilityVariables facilities;
    for (model::DwcLink candidate : candidates) {
        const std::string name = model::DwcVariableName(candidate);
        const Var variable = state.model->AddVar(
            0.0, 1.0, 0.0, COPT_CONTINUOUS, name.c_str());
        facilities.emplace(name, variable);
        state.facilityVariables.push_back(variable);
    }
    Expr objective(0.0);
    state.scenarioVariables = model::BuildScenarioModel(*state.model, instance,
        instance.scenarios.at(scenarioIndex), scenarioIndex, facilities, objective,
        {false, true});
    for (auto& route : state.scenarioVariables.batteryChoicesByRoute) {
        for (Var& choice : route) choice.SetType(COPT_CONTINUOUS);
    }
    state.model->SetObjective(objective, COPT_MINIMIZE);
    state.model->Update();
    solver::Configure(*state.model, solverConfig, false);
    state.model->SetIntParam(COPT_INTPARAM_LPMETHOD, 1);

    const std::set<model::DwcLink> scenarioLinks = ScenarioLinks(
        instance.scenarios.at(scenarioIndex));
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (scenarioLinks.count(candidates.at(index)) != 0) {
            state.relevantMasterIndices.push_back(index);
        }
    }
    for (const auto& route : master.batteryVariables.at(scenarioIndex)) {
        for (const BatteryMasterLevel& level : route) {
            state.relevantMasterIndices.push_back(level.flatIndex);
        }
    }
    return state;
}

solver::Result EvaluateSubproblem(ScenarioSubproblem& subproblem,
    const LiftedMaster& master, size_t scenarioIndex,
    const std::vector<double>& values, double timeLimit)
{
    for (size_t facility = 0; facility < subproblem.facilityVariables.size(); ++facility) {
        Var& variable = subproblem.facilityVariables.at(facility);
        variable.Set(COPT_DBLINFO_LB, values.at(facility));
        variable.Set(COPT_DBLINFO_UB, values.at(facility));
    }
    for (size_t route = 0; route < subproblem.scenarioVariables.batteryChoicesByRoute.size(); ++route) {
        auto& subLevels = subproblem.scenarioVariables.batteryChoicesByRoute.at(route);
        const auto& masterLevels = master.batteryVariables.at(scenarioIndex).at(route);
        for (size_t level = 0; level < subLevels.size(); ++level) {
            const double value = values.at(masterLevels.at(level).flatIndex);
            subLevels.at(level).Set(COPT_DBLINFO_LB, value);
            subLevels.at(level).Set(COPT_DBLINFO_UB, value);
        }
    }
    subproblem.model->SetDblParam(COPT_DBLPARAM_TIMELIMIT, std::max(0.001, timeLimit));
    subproblem.model->Update();
    return solver::Solve(*subproblem.model);
}

double FirstStageCost(const LiftedMaster& master, const std::vector<double>& values)
{
    double cost = 0.0;
    for (size_t index = 0; index < values.size(); ++index) {
        cost += master.costs.at(index) * values.at(index);
    }
    return cost;
}

void PrintBounds(double upperBound, double lowerBound)
{
    std::cout << ", UB=";
    if (!std::isfinite(upperBound)) {
        std::cout << "inf, LB=" << lowerBound << ", gap=1";
        return;
    }
    std::cout << upperBound << ", LB=" << lowerBound << ", gap="
              << (upperBound - lowerBound) / std::max(1.0, std::fabs(upperBound)) * 100.0
              << '%';
}

} // namespace

solver::Result SolveLiftedIls(const InstanceData& instance,
    const RunConfig& runConfig, const SolverConfig& solverConfig)
{
    if (runConfig.useRsp) {
        throw std::invalid_argument("The rewritten Lifted ILS does not include RSP");
    }
    const auto start = std::chrono::steady_clock::now();
    const std::vector<model::DwcLink> candidates = runConfig.allowWirelessCharging
        ? model::SelectDwcCandidates(instance, runConfig.dwcTopN)
        : std::vector<model::DwcLink>();
    Envr environment;
    Model masterModel = environment.CreateModel("lifted_ils_master");
    LiftedMaster master = BuildMaster(masterModel, instance, candidates);
    solver::Configure(masterModel, solverConfig, solverConfig.phase2MasterLog);

    std::vector<ScenarioSubproblem> subproblems;
    subproblems.reserve(instance.scenarios.size());
    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        subproblems.push_back(BuildSubproblem(
            instance, scenario, candidates, master, solverConfig));
    }
    std::cout << "Lifted ILS initialized: master_vars=" << master.designVariables.size()
              << ", scenarios=" << subproblems.size() << ", RSP=off\n";

    double upperBound = std::numeric_limits<double>::infinity();
    double lowerBound = -std::numeric_limits<double>::infinity();
    solver::Result finalResult;
    for (int iteration = 0; iteration < kMaximumIterations; ++iteration) {
        double remaining = solverConfig.timeLimit - ElapsedSeconds(start);
        if (remaining <= 0.0) {
            finalResult.status = solver::Status::TimeLimit;
            break;
        }
        masterModel.SetDblParam(COPT_DBLPARAM_TIMELIMIT, remaining);
        const solver::Result masterResult = solver::Solve(masterModel);
        if (!masterResult.hasSolution) {
            finalResult.status = masterResult.status;
            break;
        }
        lowerBound = std::max(lowerBound, masterResult.bound);
        const std::vector<double> values = ReadValues(master.designVariables);
        std::vector<double> thetaValues;
        for (Var& variable : master.scenarioTheta) {
            thetaValues.push_back(variable.Get(COPT_DBLINFO_VALUE));
        }
        const double thetaValue = master.theta.Get(COPT_DBLINFO_VALUE);

        bool infeasible = false;
        int feasibilityCuts = 0;
        int optimalityCuts = 0;
        double recourse = 0.0;
        double subproblemSeconds = 0.0;
        for (size_t scenario = 0; scenario < subproblems.size(); ++scenario) {
            remaining = solverConfig.timeLimit - ElapsedSeconds(start);
            if (remaining <= 0.0) {
                finalResult.status = solver::Status::TimeLimit;
                return finalResult;
            }
            solver::Result result = EvaluateSubproblem(
                subproblems.at(scenario), master, scenario, values, remaining);
            subproblemSeconds += result.seconds;
            const Expr mismatch = BuildMismatch(master.designVariables, values,
                subproblems.at(scenario).relevantMasterIndices);
            if (result.status == solver::Status::Infeasible) {
                masterModel.AddConstr(mismatch >= 1.0,
                    ("lifted_feas_" + std::to_string(iteration) + "_" +
                        std::to_string(scenario)).c_str());
                ++feasibilityCuts;
                infeasible = true;
            } else if (result.status == solver::Status::Optimal) {
                recourse += result.objective;
                if (thetaValues.at(scenario) + kCutTolerance < result.objective) {
                    masterModel.AddConstr(master.scenarioTheta.at(scenario) >=
                        result.objective * (1.0 - mismatch),
                        ("lifted_opt_" + std::to_string(iteration) + "_" +
                            std::to_string(scenario)).c_str());
                    ++optimalityCuts;
                }
            } else {
                return result;
            }
        }
        masterModel.Update();
        if (!infeasible) {
            upperBound = std::min(upperBound, FirstStageCost(master, values) + recourse);
        }
        std::cout << "Lifted ILS iter " << iteration;
        if (infeasible) std::cout << ", feasibility cut x" << feasibilityCuts;
        else std::cout << ", theta=" << thetaValue
                       << ", Q(z)=" << recourse
                       << ", optimality cut x" << optimalityCuts;
        PrintBounds(upperBound, lowerBound);
        std::cout << ", master_ms=" << masterResult.seconds * 1000.0
                  << ", subproblem_ms=" << subproblemSeconds * 1000.0
                  << ", elapsed_ms=" << ElapsedSeconds(start) * 1000.0 << '\n';

        const double targetGap = runConfig.phase2Gap >= 0.0
            ? runConfig.phase2Gap : solverConfig.mipGap;
        if (std::isfinite(upperBound) &&
            (upperBound - lowerBound) / std::max(1.0, std::fabs(upperBound)) <= targetGap) {
            finalResult.status = solver::Status::Optimal;
            finalResult.hasSolution = true;
            finalResult.objective = upperBound;
            finalResult.bound = lowerBound;
            finalResult.relativeGap = (upperBound - lowerBound) /
                std::max(1.0, std::fabs(upperBound));
            finalResult.seconds = ElapsedSeconds(start);
            std::cout << "Lifted ILS converged.\n";
            return finalResult;
        }
    }

    finalResult.hasSolution = std::isfinite(upperBound);
    finalResult.objective = upperBound;
    finalResult.bound = lowerBound;
    finalResult.seconds = ElapsedSeconds(start);
    return finalResult;
}

} // namespace wireless::algorithm
