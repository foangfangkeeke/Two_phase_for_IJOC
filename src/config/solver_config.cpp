#include "config/solver_config.hpp"

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::string Trim(const std::string& value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

} // namespace

SolverConfig LoadSolverConfig()
{
    std::ifstream input("solverCfg.txt");
    if (!input) throw std::runtime_error("Cannot open solverCfg.txt");

    SolverConfig config;
    std::string line;
    while (std::getline(input, line)) {
        const size_t comment = line.find('#');
        line = Trim(line.substr(0, comment));
        if (line.empty()) continue;
        const size_t separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("Invalid solver config entry: " + line);
        }
        const std::string key = Trim(line.substr(0, separator));
        const std::string value = Trim(line.substr(separator + 1));
        if (key == "mipGap") config.mipGap = std::stod(value);
        else if (key == "timeLimit") config.timeLimit = std::stod(value);
        else if (key == "presolve") config.presolve = std::stoi(value);
        else if (key == "threads") config.threads = std::stoi(value);
        else if (key == "method") config.method = std::stoi(value);
        else if (key == "phaseProgressLog") config.phaseProgressLog = std::stoi(value) != 0;
        else if (key == "phase2MasterLog") config.phase2MasterLog = std::stoi(value) != 0;
        else if (key != "mipFocus" && key != "heuristics") {
            throw std::runtime_error("Unknown solver config key: " + key);
        }
    }
    return config;
}
