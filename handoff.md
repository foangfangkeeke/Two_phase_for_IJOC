# Handoff

For the full paper-facing methodology narrative and mathematical formulation, see [PROJECT_SUMMARY.md](PROJECT_SUMMARY.md).

## 1. Current intent

Wireless charging facility deployment and adaptive battery configuration under service-plan uncertainty. Five-algorithm ladder:

1. `solver` — direct extensive-form MILP baseline.
2. `natural-ils` — natural ILS: `w` in master, `z_ω` + `y_ω` in subproblem（naive, no LP pruning）.
3. `lifted-ils` — lifted reformulation, single-phase MIP, classic cuts.
4. `lifted-two-phase` — lifted + two-phase (LP→MIP), classic cuts.
5. `full` — lifted + two-phase + strengthened feasibility and optimality cuts.

## 2. Final intended algorithm ladder

| CLI | Class | ProblemType | Description |
|-----|-------|-------------|-------------|
| `--alg solver` | `UsingSolver` | `WIRELESS_CHARGING_NO_PLUGIN` | Direct extensive-form MILP |
| `--alg natural-ils` | `NaturalILS` | `WIRELESS_CHARGING_NO_PLUGIN` | Natural ILS, naive, no LP pruning |
| `--alg lifted-ils` | `LiftedILS` | `WIRELESS_CAPACITY_REFORM` | Lifted, single-phase MIP |
| `--alg lifted-two-phase` | `LiftedTwoPhaseILS` | `WIRELESS_CAPACITY_REFORM` | Lifted + two-phase, classic cuts |
| `--alg full` | `FullMethod` | `WIRELESS_CAPACITY_REFORM` | Lifted + two-phase + strengthened feasibility and optimality cuts |

## 3. Target code architecture

Below is the intended code architecture; implementation progress is tracked in §8.

### 3.1 Algorithm files (`src/algorithm/`)

| File | Role |
|------|------|
| `using_solver.cpp/hpp` | Solver baseline |
| `natural_ils.cpp/hpp` | Natural ILS baseline（naive, no LP pruning） |
| `lifted_ils.cpp/hpp` | Lifted ILS（self-contained） |
| `lifted_two_phase_ils.cpp/hpp` | Lifted two-phase ILS（self-contained） |
| `full_method.cpp/hpp` | Full method: Phase 1 route relaxation + Phase 2 per-scenario MIP + strengthened feasibility and optimality cuts |
| `lshape_relaxed.cpp/hpp` | Phase 1 route relaxation component（internal） |
| `lshape_monotonicity_cut.hpp` | Scenario-level monotonicity-strengthened feasibility/optimality cut utilities（target name） |
| `benders_core.cpp` | Shared Benders utilities |
| `benders_decomposition.hpp` | Shared interfaces |

### 3.2 Problem types and model mapping

| Enum | Source | Key features |
|------|--------|-------------|
| `WIRELESS_CHARGING_NO_PLUGIN` | `wireless.cpp` | Full plug-in capacity + scheduling binaries. Enum name misleading; model contains plug-in constraints. |
| `WIRELESS_CAPACITY_REFORM` | `wireless_reform.cpp` | Same physical model, different data initialization. `reform` is a decomposition device, not a separate problem type. |

Both model the same physical system with capacity constraints and scheduling binaries (verified).

### 3.3 Target key behaviors

- Solver timeout with no incumbent prints `no incumbent found`, not `no feasible solution`.
- Phase 1 and Phase 2 elapsed times reported separately.
- Target: each scenario → one operational subproblem（no station-group decomposition）.
- Target: Natural ILS is a naive baseline — no LP-relaxation pruning, no relaxed warm start.
- Solver uses Gurobi internal parallelism. Benders variants: one Gurobi thread per subproblem, concurrent subproblem evaluations capped at available hardware cores. If the machine budget is N physical cores, at most N subproblems are evaluated concurrently.

## 4. Implementation notes

### 4.1 `WIRELESS_CHARGING_NO_PLUGIN` enum

Enum name misleading — the model contains plug-in capacity constraints. Both `wireless.cpp` and `wireless_reform.cpp` model the same physical system (verified).

### 4.2 Solver thread policy

All methods share the same hardware budget. Solver uses Gurobi internal parallelism. Benders variants: Threads=1 per subproblem, `std::async` concurrency capped at available cores.

## 5. Build and run instructions

### 5.1 Build

```bat
cmd.exe /d /s /c "call .\run.bat <nul"
```

Requires: Windows 11, MSVC 2022, CMake ≥ 3.20, Gurobi 12.0+. Output: `out/build/x64/Release/wireless.exe`.

### 5.2 CLI commands

```bash
# Baseline — original formulation (wireless.cpp)
./wireless.exe --pb wireless --alg solver --data tower
./wireless.exe --pb wireless --alg l2 --data tower          # natural-ils (legacy CLI name)

# Reformulated methods (wireless_reform.cpp)
./wireless.exe --pb wireless_reform --alg lifted-ils --data tower
./wireless.exe --pb wireless_reform --alg lifted-two-phase --data tower
./wireless.exe --pb wireless_reform --alg full --data tower
```

> **TODO**: under `--pb wireless`, rename `--alg l2` → `--alg natural-ils`. Under `--pb wireless_reform`, remove `solver` and `natural-ils` (both currently dispatch to `wireless.cpp` internally, inconsistent with the `wireless_reform` label). The five-algorithm experiment should run `solver` and `natural-ils` via `--pb wireless`, and `lifted-ils`/`lifted-two-phase`/`full` via `--pb wireless_reform`.

### 5.3 Config files

- `solverCfg.txt` — Gurobi default settings except MIPGap, TimeLimit, and thread policy. Thread policy: solver uses Gurobi internal parallelism; Benders variants use one thread per subproblem, with concurrent subproblem evaluations capped by the hardware-core budget（see §7）.
- `data/{type}_data/globalCfg.txt` — operational and charger parameters.
- `data/{type}_data/scenarioCfg.txt` — scenario list and probabilities.
- `data/{type}_data/{scenario}/scenarioMiscCfg.txt` — routes, EBs, timetables.

## 6. Experiment execution plan

### 6.1 Stage 1: quick screening（600s）

All five algorithms @ 600s on all instances（`tower_small` → `tower_medium` → `tower_5` → `tower_10` → `tower_20`）. 2-3 runs each. Goal: find break point where solver/natural-ils fail but `full` still works.

### 6.2 Stage 2: main table（7200s unified）

All five algorithms @ 7200s on instances around the break point, ≥3 runs per cell. Report mean ± std or min/median/max. The five-algorithm ladder itself is the ablation.

### 6.3 Per-run output fields

UB, LB, global gap, total wall-clock time, master solve time, Phase 1 time, Phase 2 time, iteration count, LP cut count, feasibility cut count, optimality cut count, incumbent status（found/none）, model-level termination status, outer-loop termination status（converged/timeout/error）.

## 7. Solver parameter policy

All algorithms use Gurobi 12.0 default settings except MIPGap, TimeLimit, and method-specific thread allocation under a common hardware budget.

MIPGap settings:
- **Stage 1 (600s screening)**: all Gurobi MIPGap = 0（EF Solver, Benders master, Phase 2 subproblem）。
- **Stage 2 (7200s main table)**: EF Solver MIPGap = 0.01%, Benders master MIPGap = 0.01%, Phase 2 subproblem MIPGap = 0.
- Phase 1 operational relaxation is an LP and does not use MIPGap.
- Outer-loop global convergence tolerance = 0.01%. Gap is computed as `(UB − LB) / max{1, |UB|}`. A numerical gap is reported only when a finite incumbent exists (`UB < ∞`); otherwise report `—` or `undefined`.
- Outer-loop lower bound is obtained from the Benders master `ObjBound`, not from the master incumbent objective value.

TimeLimit settings:
- All methods use the same wall-clock time limit.
- For the EF Solver, TimeLimit is set directly on the Gurobi model.
- For decomposition methods, the outer loop tracks elapsed wall-clock time and dynamically sets the remaining time on each Gurobi call (master and subproblems).

The extensive-form solver uses Gurobi internal parallelism. Decomposition-based methods use one Gurobi thread per subproblem and cap the number of concurrent subproblem evaluations at the same hardware budget. If the machine budget is N physical cores, at most N subproblems should be evaluated concurrently.

Paper states: "All methods are given access to the same hardware. The solver uses Gurobi's internal parallelism. Benders variants use one thread per subproblem and exploit parallelism through concurrent subproblem evaluation, capped at the available core count."

## 8. Checklist

### Public path cleanup

- [x] Five algorithm names exposed in `main.cpp`.
- [x] Old `sg-*` labels replaced.
- [ ] Rename `--alg l2` → `--alg natural-ils` under `--pb wireless`.
- [ ] Remove `solver` and `natural-ils` from `--pb wireless_reform` (both dispatch to `wireless.cpp` internally; baseline methods should live under `--pb wireless`).

### File split / code organization

- [x] Independent files for `lifted-ils`, `lifted-two-phase`.
- [x] `LShapeStation` removed.
- [x] `full` independent.
- [ ] Remove station-group decomposition from all Benders variants.
- [x] Rename: `barp_s_integer_l_shaped` → `natural_ils`, `lshape_relaxed_station` → `full_method`, `lshape_station_cut` → `lshape_monotonicity_cut`.
- [ ] Shared utilities limited to genuinely identical behavior.

### Terminology and output cleanup

- [x] `no incumbent found` output.
- [x] Phase 1/Phase 2 timing.
- [ ] Standardize log output across all five algorithms.
- [ ] Remove station-group terminology from code comments and logs.

### Model consistency

- [x] Verify `WIRELESS_CHARGING_NO_PLUGIN` vs actual plug-in modeling.
- [x] Compare objective terms between `wireless.cpp` and `wireless_reform.cpp`.
- [x] Compare battery cost scaling, plug-in constraints, wireless constraints, SoC.

### Algorithm contributions

- [x] Lifted reformulation implemented.
- [x] Two-phase structure implemented.
- [x] Strengthened feasibility cut implemented.
- [x] Remove LP-relaxation pruning from natural-ILS.
- [ ] Implement monotonicity-strengthened optimality cut.

### Experiment readiness

- [ ] Stage 1 & Stage 2 execution.
- [ ] `tower_10` config.
- [ ] Standardized per-run output.
- [ ] ≥3 runs per cell with spread reporting.
- [ ] Phase 2 sub MIPGap = 0 set on all Benders variants.
- [ ] Benders master MIPGap set to 0.01%.
- [ ] Master `ObjBound` used as outer-loop LB.
- [ ] Outer-loop convergence test implemented (0.01% tolerance, formula `(UB−LB)/max{1,|UB|}`).
- [ ] Outer-loop wall-clock TimeLimit tracking implemented for decomposition methods.

## 9. Immediate priorities

1. Remove station-group decomposition — unify to per-scenario subproblems.
2. Implement monotonicity-strengthened optimality cut — so that `full` matches its target definition (strengthened feasibility and optimality cuts).
3. Complete outer-loop TimeLimit and convergence-gap control (Phase 2 sub MIPGap = 0, master ObjBound as LB, 0.01% outer-loop test).
4. Generate `tower_10` scenario config.
5. Run experiment plan Stage 1 & Stage 2.