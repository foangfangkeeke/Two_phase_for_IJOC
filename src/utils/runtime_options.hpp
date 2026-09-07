#pragma once

struct RuntimeOptions {
    double facilityCostScale = 1.0;
    double energyScale = 1.0;
    int scenarioCount = 5;
    int chargerCapacity = 5;
};

inline RuntimeOptions& GetRuntimeOptions()
{
    static RuntimeOptions options;
    return options;
}

inline void SetRuntimeOptions(const RuntimeOptions& options)
{
    GetRuntimeOptions() = options;
}
