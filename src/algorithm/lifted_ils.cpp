#include "lifted_ils.hpp"

#include <utility>

LiftedILS::LiftedILS(ProblemType problemType, std::string dataFolder,
    int maxIters, double tol)
    : engine_(problemType, std::move(dataFolder), false, maxIters, tol)
{}

Status LiftedILS::Initialize()
{
    engine_.SetSolverConfig(solverConfig);
    const Status status = engine_.Initialize();
    initialized = engine_.initialized;
    return status;
}

Status LiftedILS::Solve()
{
    engine_.SetSolverConfig(solverConfig);
    return engine_.Solve();
}

LiftedILS::~LiftedILS() = default;
