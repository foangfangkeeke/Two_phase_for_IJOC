#pragma once

struct SolverConfig {
    double mipGap = 0.0;
    double timeLimit = 3600.0;
    int presolve = -1;
    int threads = 12;
    int method = -1;
    bool phaseProgressLog = false;
    bool phase2MasterLog = false;
};

SolverConfig LoadSolverConfig();
