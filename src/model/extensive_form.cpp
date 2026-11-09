#include "model/extensive_form.hpp"

#include "model/model_helpers.hpp"
#include "model/scenario_model.hpp"

#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace wireless::model {

solver::Result SolveExtensiveForm(const InstanceData& instance, const RunConfig& runConfig,
    const SolverConfig& solverConfig)
{
    Envr environment = solver::CreateEnvironment();
    Model model = environment.CreateModel("wireless_extensive_form");
    FacilityVariables facilityVariables;
    std::vector<std::vector<std::vector<Var>>> batteryChoices;
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
        ScenarioVariables variables = BuildScenarioModel(model, instance,
            instance.scenarios.at(scenario), scenario, facilityVariables, objective);
        if (runConfig.dataName.rfind("R28_", 0) == 0) {
            batteryChoices.push_back(std::move(variables.batteryChoicesByRoute));
        }
    }
    model.SetObjective(objective, COPT_MINIMIZE);
    model.Update();
    SolverConfig extensiveConfig = solverConfig;
    extensiveConfig.mipGap = solverConfig.globalGap;
    solver::Configure(model, extensiveConfig);

    std::cout << "DWC candidate screening: selected=" << candidates.size() << '\n'
              << "scenario_count=" << instance.scenarios.size()
              << ", facility_vars=" << facilityVariables.size()
              << ", model_vars=" << model.GetIntAttr(COPT_INTATTR_COLS)
              << ", model_constrs=" << model.GetIntAttr(COPT_INTATTR_ROWS) << '\n';
    const solver::Result result = solver::Solve(model);
    if (runConfig.dataName.rfind("R28_", 0) == 0 && result.hasSolution) {
        std::set<std::string> selectedFacilities;
        for (auto& [name, variable] : facilityVariables) {
            if (variable.Get(COPT_DBLINFO_VALUE) > 0.5) selectedFacilities.insert(name);
        }
        const std::vector<int> levels = BatteryCapacityLevels(instance.operation,
            static_cast<int>(instance.operation.maximumBatteryCapacity));
        std::ostringstream out;
        out << std::setprecision(std::numeric_limits<double>::max_digits10)
            << "Search incumbent: {\"source\":\"ef\",\"design\":{\"facilities\":[";
        bool first = true;
        for (const std::string& facility : selectedFacilities) {
            if (!first) out << ',';
            out << std::quoted(facility);
            first = false;
        }
        out << "],\"batteries\":[";
        for (size_t scenario = 0; scenario < batteryChoices.size(); ++scenario) {
            if (scenario != 0) out << ',';
            out << '[';
            for (size_t route = 0; route < batteryChoices.at(scenario).size(); ++route) {
                if (route != 0) out << ',';
                int selectedCapacity = 0;
                const auto& choices = batteryChoices.at(scenario).at(route);
                for (size_t level = 0; level < choices.size(); ++level) {
                    Var variable = choices.at(level);
                    if (variable.Get(COPT_DBLINFO_VALUE) > 0.5) {
                        selectedCapacity = levels.at(level);
                    }
                }
                if (selectedCapacity == 0) {
                    throw std::runtime_error("EF incumbent has no selected route battery");
                }
                out << selectedCapacity;
            }
            out << ']';
        }
        out << "]}}\n";
        std::cout << out.str()
                  << "Incumbent cost distribution: source=ef, total="
                  << result.objective << '\n';
    }
    return result;
}

} // namespace wireless::model
