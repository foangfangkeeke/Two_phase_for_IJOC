#include "lshape_relaxed_station.hpp"
#include "lshape_relaxed.hpp"
#include "lshape_station.hpp"

#include <iostream>
#include <stdexcept>

LShapeRelaxedStation::LShapeRelaxedStation(ProblemType problemType,
    std::string dataFolder, int maxIters, double tol)
    : problemType(problemType), dataFolder(std::move(dataFolder)),
      maxIters(maxIters), tolerance(tol)
{}

Status LShapeRelaxedStation::Initialize()
{
    if (initialized) return OK;

    // ── Phase 1: sg-relax ──
    LShapeRelaxed phase1(problemType, dataFolder, maxIters, tolerance);
    phase1.SetSolverConfig(solverConfig);

    Status s = phase1.Initialize();
    if (s != OK) {
        std::cerr << "Phase 1 (sg-relax) initialization failed" << std::endl;
        return s;
    }

    s = phase1.Solve();
    if (s != OK) {
        std::cerr << "Phase 1 (sg-relax) solve failed" << std::endl;
        return s;
    }

    // Capture Phase 1 results
    phase1ZValues = phase1.GetIncumbentZValues();
    phase1UpperBound = phase1.GetBestUpperBound();

    if (phase1ZValues.empty()) {
        std::cerr << "Phase 1 did not produce an incumbent solution" << std::endl;
        return ERROR;
    }

    std::cout << std::endl << "── Phase 2: sg-full (full charger capacity) ──" << std::endl;

    // ── Phase 2: sg-full warm-started from Phase 1 ──
    auto station = std::make_unique<LShapeStation>(
        problemType, dataFolder,
        true,   // useImprovedCuts
        false,  // twoPhase=false → directly solve MIP sub-problems (no LP relaxation phase)
        maxIters, tolerance,
        false); // perStationMode=false → sg-full
    station->SetSolverConfig(solverConfig);

    s = station->Initialize();
    if (s != OK) {
        std::cerr << "Phase 2 (sg-full) initialization failed" << std::endl;
        return s;
    }

    station->SetExternalWarmStart(phase1ZValues, phase1UpperBound);
    phase2 = std::move(station);

    initialized = true;
    return OK;
}

Status LShapeRelaxedStation::Solve()
{
    if (!phase2) {
        std::cerr << "Phase 2 not initialized" << std::endl;
        return ERROR;
    }
    return phase2->Solve();
}

LShapeRelaxedStation::~LShapeRelaxedStation() = default;
