#include "solver/copt_solver.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace solver {

void Configure(Model& model, const SolverConfig& config, bool enableLog)
{
    model.SetIntParam(COPT_INTPARAM_LOGTOCONSOLE, enableLog ? 1 : 0);
    model.SetDblParam(COPT_DBLPARAM_RELGAP, config.mipGap);
    model.SetDblParam(COPT_DBLPARAM_FEASTOL, 1e-6);
    model.SetDblParam(COPT_DBLPARAM_DUALTOL, 1e-6);
    model.SetDblParam(COPT_DBLPARAM_INTTOL, 1e-5);
    model.SetDblParam(COPT_DBLPARAM_TIMELIMIT, config.timeLimit);
    model.SetIntParam(COPT_INTPARAM_THREADS, config.threads);
    if (config.presolve >= 0) model.SetIntParam(COPT_INTPARAM_PRESOLVE, config.presolve);
    if (config.method >= 0) model.SetIntParam(COPT_INTPARAM_LPMETHOD, config.method);
}

Result Solve(Model& model)
{
    const auto start = std::chrono::steady_clock::now();
    model.Solve();
    const auto finish = std::chrono::steady_clock::now();

    Result result;
    result.seconds = std::chrono::duration<double>(finish - start).count();
    const int nativeStatus = model.GetIntAttr(COPT_INTATTR_STATUS);
    if (nativeStatus == COPT_STATUS_OPTIMAL) result.status = Status::Optimal;
    else if (nativeStatus == COPT_STATUS_INFEASIBLE) result.status = Status::Infeasible;
    else if (nativeStatus == COPT_STATUS_UNBOUNDED || nativeStatus == COPT_STATUS_INF_OR_UNB) {
        result.status = Status::Unbounded;
    } else if (nativeStatus == COPT_STATUS_TIMEOUT) result.status = Status::TimeLimit;
    else if (nativeStatus == COPT_STATUS_INTERRUPTED) result.status = Status::Interrupted;

    result.hasSolution = model.GetIntAttr(COPT_INTATTR_HASSOL) != 0;
    if (result.hasSolution) result.objective = model.GetDblAttr(COPT_DBLATTR_OBJVAL);
    if (result.status == Status::Optimal || result.status == Status::TimeLimit ||
        result.status == Status::Interrupted) {
        result.bound = model.GetDblAttr(COPT_DBLATTR_BESTBND);
    }
    if (result.hasSolution) {
        result.relativeGap = (result.objective - result.bound) /
            std::max(1.0, std::fabs(result.objective));
    }
    return result;
}

std::string ToString(Status status)
{
    switch (status) {
    case Status::Optimal: return "OPTIMAL";
    case Status::Infeasible: return "INFEASIBLE";
    case Status::Unbounded: return "UNBOUNDED";
    case Status::TimeLimit: return "TIME_LIMIT";
    case Status::Interrupted: return "INTERRUPTED";
    case Status::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

} // namespace solver
