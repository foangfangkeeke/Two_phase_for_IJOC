#include "model/extensive_form.hpp"

#include "model/model_helpers.hpp"
#include "model/scenario_model.hpp"

#include <iostream>

namespace wireless::model {

solver::Result SolveExtensiveForm(const InstanceData& instance, const RunConfig& runConfig,
    const SolverConfig& solverConfig)
{
    Envr environment = solver::CreateEnvironment();
    Model model = environment.CreateModel("wireless_extensive_form");
    FacilityVariables facilityVariables;
    Expr objective(0.0);

    std::vector<DwcLink> candidates;
    if (runConfig.allowWirelessCharging) {
        candidates = SelectDwcCandidates(instance, runConfig.dwcTopN);
        for (DwcLink link : candidates) {
            const std::string name = DwcVariableName(link);
            const double cost = DwcFacilityCost(instance, link);
            const Var facility = model.AddVar(0.0, 1.0, 0.0, COPT_BINARY, name.c_str());
            facilityVariables.emplace(name, facility);
            objective += cost * facility;
        }
    }

    for (size_t scenario = 0; scenario < instance.scenarios.size(); ++scenario) {
        BuildScenarioModel(model, instance, instance.scenarios.at(scenario), scenario,
            facilityVariables, objective);
    }
    model.SetObjective(objective, COPT_MINIMIZE);
    model.Update();
    solver::Configure(model, solverConfig);

    std::cout << "DWC candidate screening: selected=" << candidates.size() << '\n'
              << "scenario_count=" << instance.scenarios.size()
              << ", facility_vars=" << facilityVariables.size()
              << ", model_vars=" << model.GetIntAttr(COPT_INTATTR_COLS)
              << ", model_constrs=" << model.GetIntAttr(COPT_INTATTR_ROWS) << '\n';
    return solver::Solve(model);
}

} // namespace wireless::model
