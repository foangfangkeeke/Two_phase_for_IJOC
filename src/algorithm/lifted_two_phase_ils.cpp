#include "lifted_two_phase_ils.hpp"

#include <utility>

LiftedTwoPhaseILS::LiftedTwoPhaseILS(ProblemType problemType, std::string dataFolder,
    int maxIters, double tol)
    : engine_(problemType, std::move(dataFolder), false, true, maxIters, tol)
{}

Status LiftedTwoPhaseILS::Initialize()
{
    engine_.SetSolverConfig(solverConfig);
    const Status status = engine_.Initialize();
    initialized = engine_.initialized;
    return status;
}

Status LiftedTwoPhaseILS::Solve()
{
    engine_.SetSolverConfig(solverConfig);
    return engine_.Solve();
}

LiftedTwoPhaseILS::~LiftedTwoPhaseILS() = default;
