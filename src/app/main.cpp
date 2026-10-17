#include "algorithm/lifted_ils.hpp"
#include "algorithm/lifted_two_phase.hpp"
#include "algorithm/natural_ils.hpp"
#include "config/run_config.hpp"
#include "config/solver_config.hpp"
#include "domain/wireless_data.hpp"
#include "model/extensive_form.hpp"
#include "solver/copt_solver.hpp"

#include <iostream>
#include <iomanip>
#include <stdexcept>

int main(int argc, char* argv[])
{
    try {
        std::cout << std::fixed << std::setprecision(6);
        const ParsedCommandLine commandLine = ParseCommandLine(argc, argv);
        if (commandLine.showHelp) {
            PrintUsage();
            return 0;
        }
        if (commandLine.config.algorithm != "data-check" &&
            commandLine.config.algorithm != "ef" &&
            commandLine.config.algorithm != "natural-ils" &&
            commandLine.config.algorithm != "lifted-ils" &&
            commandLine.config.algorithm != "lifted-two-phase") {
            throw std::invalid_argument("Algorithm is not implemented on the rewrite branch yet: " +
                commandLine.config.algorithm);
        }

        const SolverConfig solver = LoadSolverConfig();
        const wireless::InstanceData instance = wireless::LoadInstance(commandLine.config);
        if (commandLine.config.algorithm == "ef") {
            const solver::Result result = wireless::model::SolveExtensiveForm(
                instance, commandLine.config, solver);
            std::cout << "EF status: " << solver::ToString(result.status) << '\n';
            if (result.hasSolution) {
                std::cout << "UB: " << result.objective << ", LB: " << result.bound
                          << ", gap=" << result.relativeGap * 100.0 << "%\n";
            }
            std::cout << "EF solve time: " << result.seconds << " s\n";
            return result.status == solver::Status::Optimal ||
                (result.status == solver::Status::TimeLimit && result.hasSolution) ? 0 : 1;
        }

        if (commandLine.config.algorithm == "natural-ils") {
            const solver::Result result = wireless::algorithm::SolveNaturalIls(
                instance, commandLine.config, solver);
            return result.status == solver::Status::Optimal ? 0 : 1;
        }

        if (commandLine.config.algorithm == "lifted-ils") {
            const solver::Result result = wireless::algorithm::SolveLiftedIls(
                instance, commandLine.config, solver);
            return result.status == solver::Status::Optimal ? 0 : 1;
        }

        if (commandLine.config.algorithm == "lifted-two-phase") {
            const solver::Result result = wireless::algorithm::SolveLiftedTwoPhase(
                instance, commandLine.config, solver);
            return result.status == solver::Status::Optimal ? 0 : 1;
        }

        std::cout << "data=" << instance.name
                  << ", scenarios=" << instance.scenarios.size()
                  << ", energy_per_km=" << instance.operation.energyPerKm
                  << ", charger_types=" << instance.chargers.rates.size()
                  << ", time_limit=" << solver.timeLimit << '\n';
        for (size_t index = 0; index < instance.scenarios.size(); ++index) {
            const auto& scenario = instance.scenarios[index];
            std::cout << "scenario=" << index
                      << ", folder=" << scenario.descriptor.folder
                      << ", probability=" << scenario.descriptor.probability
                      << ", buses=" << scenario.schedule.busIds.size()
                      << ", routes=" << scenario.schedule.routeNames.size()
                      << ", trips=" << scenario.schedule.stationsByTrip.size()
                      << ", stations=" << scenario.network.stations.size()
                      << ", plug_in_stations=" << scenario.network.plugInStations.size()
                      << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
