#pragma once

#include <filesystem>
#include <string>

struct RunConfig {
    std::string problem = "wireless_reform";
    std::string algorithm = "data-check";
    std::string dataName = "tower_small";
    int scenarioCount = 5;
    int scenarioStart = 0;
    int chargerCapacity = 5;
    int dwcTopN = 50;
    double facilityCostScale = 1.0;
    double energyScale = 1.0;
    int batteryCapacityStep = 20;
    bool allowWirelessCharging = true;
    bool preprocessBounds = false;
    bool useRsp = false;
    int rspWorkers = 0;
    int rspThreads = 0;
    bool phaseProgressLog = false;
    bool phase2MasterLog = false;
    bool exportSolution = false;
    bool exportSummary = false;
    std::filesystem::path exportDirectory;
};

struct ParsedCommandLine {
    RunConfig config;
    bool showHelp = false;
};

ParsedCommandLine ParseCommandLine(int argc, char* argv[]);
void PrintUsage();
