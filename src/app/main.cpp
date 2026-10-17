#include "config/run_config.hpp"
#include "config/solver_config.hpp"
#include "domain/wireless_data.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char* argv[])
{
    try {
        const ParsedCommandLine commandLine = ParseCommandLine(argc, argv);
        if (commandLine.showHelp) {
            PrintUsage();
            return 0;
        }
        if (commandLine.config.algorithm != "data-check") {
            throw std::invalid_argument("Algorithm is not implemented on the rewrite branch yet: " +
                commandLine.config.algorithm);
        }

        const SolverConfig solver = LoadSolverConfig();
        const wireless::InstanceData instance = wireless::LoadInstance(commandLine.config);
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
