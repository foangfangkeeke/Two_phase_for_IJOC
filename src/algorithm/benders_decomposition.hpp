#pragma once

#include "basic_solver.hpp"
#include "milp_solver.hpp"

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct BendersCutInfo {
    std::vector<double> yCoeffs;
    double constant = 0.0;
    double rhs = 0.0;
    bool isOptimalityCut = true;
    char sense = '>';
};

struct BendersSubProblemContext {
    std::map<std::string, std::vector<GRBVar>> varGroups;
    std::map<std::string, std::vector<GRBConstr>> constrGroups;

    std::vector<GRBVar>& EnsureVarGroup(const std::string& groupName)
    {
        return varGroups[groupName];
    }

    std::vector<GRBConstr>& EnsureConstrGroup(const std::string& groupName)
    {
        return constrGroups[groupName];
    }

    const std::vector<GRBVar>& RequireVarGroup(const std::string& groupName, size_t expectedSize) const
    {
        auto it = varGroups.find(groupName);
        if (it == varGroups.end()) {
            throw std::runtime_error("Missing Benders sub-problem variable group: " + groupName);
        }
        if (it->second.size() != expectedSize) {
            throw std::runtime_error("Unexpected variable group size for: " + groupName);
        }
        return it->second;
    }

    const std::vector<GRBConstr>& RequireConstrGroup(const std::string& groupName, size_t expectedSize) const
    {
        auto it = constrGroups.find(groupName);
        if (it == constrGroups.end()) {
            throw std::runtime_error("Missing Benders sub-problem constraint group: " + groupName);
        }
        if (it->second.size() != expectedSize) {
            throw std::runtime_error("Unexpected constraint group size for: " + groupName);
        }
        return it->second;
    }

    void Clear()
    {
        varGroups.clear();
        constrGroups.clear();
    }
};
