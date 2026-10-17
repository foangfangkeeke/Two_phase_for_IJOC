#include "config/run_config.hpp"

#include <iostream>
#include <stdexcept>

namespace {

std::string NextValue(int argc, char* argv[], int& index)
{
    if (++index >= argc) {
        throw std::invalid_argument(std::string(argv[index - 1]) + " requires a value");
    }
    return argv[index];
}

int ParsePositiveInt(const std::string& value, const std::string& option)
{
    size_t parsed = 0;
    const int result = std::stoi(value, &parsed);
    if (parsed != value.size() || result <= 0) {
        throw std::invalid_argument(option + " must be a positive integer");
    }
    return result;
}

bool ParseBool(const std::string& value, const std::string& option)
{
    if (value == "0") return false;
    if (value == "1") return true;
    throw std::invalid_argument(option + " must be 0 or 1");
}

} // namespace

ParsedCommandLine ParseCommandLine(int argc, char* argv[])
{
    ParsedCommandLine result;
    for (int index = 1; index < argc; ++index) {
        const std::string option = argv[index];
        if (option == "--help" || option == "-h") {
            result.showHelp = true;
        } else if (option == "--pb") {
            result.config.problem = NextValue(argc, argv, index);
        } else if (option == "--alg") {
            result.config.algorithm = NextValue(argc, argv, index);
        } else if (option == "--data") {
            result.config.dataName = NextValue(argc, argv, index);
        } else if (option == "--scenario-count") {
            result.config.scenarioCount = ParsePositiveInt(NextValue(argc, argv, index), option);
        } else if (option == "--scenario-start") {
            result.config.scenarioStart = std::stoi(NextValue(argc, argv, index));
        } else if (option == "--cap") {
            result.config.chargerCapacity = ParsePositiveInt(NextValue(argc, argv, index), option);
        } else if (option == "--dwc-top-n") {
            result.config.dwcTopN = ParsePositiveInt(NextValue(argc, argv, index), option);
        } else if (option == "--facility-cost-scale") {
            result.config.facilityCostScale = std::stod(NextValue(argc, argv, index));
        } else if (option == "--energy-scale") {
            result.config.energyScale = std::stod(NextValue(argc, argv, index));
        } else if (option == "--phase2-gap") {
            result.config.phase2Gap = std::stod(NextValue(argc, argv, index));
        } else if (option == "--wireless-option") {
            result.config.allowWirelessCharging = ParseBool(NextValue(argc, argv, index), option);
        } else if (option == "--preprocess-bounds") {
            result.config.preprocessBounds = ParseBool(NextValue(argc, argv, index), option);
        } else if (option == "--rsp") {
            result.config.useRsp = ParseBool(NextValue(argc, argv, index), option);
        } else if (option == "--phase-progress-log") {
            result.config.phaseProgressLog = ParseBool(NextValue(argc, argv, index), option);
        } else if (option == "--phase2-master-log") {
            result.config.phase2MasterLog = ParseBool(NextValue(argc, argv, index), option);
        } else if (option == "--battery-repair-cuts") {
            result.config.batteryRepairCuts = ParseBool(NextValue(argc, argv, index), option);
        } else if (option == "--repair-facilities") {
            result.config.repairFacilities = ParseBool(NextValue(argc, argv, index), option);
        } else if (option == "--export-solution") {
            result.config.exportSolution = true;
        } else if (option == "--export-summary") {
            result.config.exportSummary = true;
        } else if (option == "--export-dir") {
            result.config.exportDirectory = NextValue(argc, argv, index);
        } else {
            throw std::invalid_argument("Unknown option: " + option);
        }
    }

    RunConfig& config = result.config;
    if (config.scenarioStart < 0 || config.facilityCostScale <= 0.0 ||
        config.energyScale <= 0.0 ||
        (config.phase2Gap < 0.0 && config.phase2Gap != -1.0)) {
        throw std::invalid_argument("Invalid numeric runtime option");
    }
    if (config.problem == "wireless_reform_no_wpt") config.allowWirelessCharging = false;
    if (config.repairFacilities && !config.batteryRepairCuts) {
        throw std::invalid_argument("--repair-facilities requires --battery-repair-cuts 1");
    }
    return result;
}

void PrintUsage()
{
    std::cout
        << "Usage: wireless.exe --alg <algorithm> --data <name> [options]\n"
        << "Algorithms currently available: data-check, ef, natural-ils, lifted-ils, lifted-two-phase\n"
        << "Options:\n"
        << "  --pb <wireless_reform|wireless_reform_no_wpt>\n"
        << "  --scenario-count <positive integer>\n"
        << "  --scenario-start <nonnegative integer>\n"
        << "  --cap <positive integer>\n"
        << "  --dwc-top-n <positive integer>\n"
        << "  --facility-cost-scale <positive number>\n"
        << "  --energy-scale <positive number>\n"
        << "  --wireless-option <0|1>\n"
        << "  --help, -h\n";
}
