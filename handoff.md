# Handoff

For the full paper-facing methodology narrative and mathematical formulation, see [PROJECT_SUMMARY.md](PROJECT_SUMMARY.md).

## 1. Current intent

Wireless charging facility deployment and adaptive battery configuration under service-plan uncertainty. Five-algorithm ladder:

1. `solver` — direct extensive-form MILP baseline.
2. `natural-ils` — natural ILS: `w` in master, `z_ω` + `y_ω` in subproblem（naive, no LP pruning）.
3. `lifted-ils` — lifted reformulation, single-phase MIP, classic cuts.
4. `lifted-two-phase` — lifted + two-phase (LP→MIP), classic cuts.
5. `full` — lifted + two-phase + strengthened feasibility and optimality cuts（optimality pending in the current implementation）.

## 2. Final intended algorithm ladder

| CLI | Class | ProblemType | Description |
|-----|-------|-------------|-------------|
| `--alg solver` | `UsingSolver` | `WIRELESS_CHARGING_NO_PLUGIN` | Direct extensive-form MILP |
| `--alg natural-ils` | `NaturalILS` | `WIRELESS_CHARGING_NO_PLUGIN` | Natural ILS, naive, no LP pruning |
| `--alg lifted-ils` | `LiftedILS` | `WIRELESS_CAPACITY_REFORM` | Lifted, single-phase MIP |
| `--alg lifted-two-phase` | `LiftedTwoPhaseILS` | `WIRELESS_CAPACITY_REFORM` | Lifted + two-phase, classic cuts |
| `--alg full` | `FullMethod` | `WIRELESS_CAPACITY_REFORM` | Lifted + two-phase + strengthened feasibility and optimality cuts |

## 3. Target code architecture

Below is the target state. See §4 for the delta from current code.

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

Both model the same system with capacity constraints and scheduling binaries. Consistency must be verified before reporting ablation results.

### 3.3 Target key behaviors

- Solver timeout with no incumbent prints `no incumbent found`, not `no feasible solution`.
- Phase 1 and Phase 2 elapsed times reported separately; Phase 2 LB uses Gurobi `ObjBound`.
- Each scenario → one operational subproblem（no station-group decomposition）.
- Natural ILS is a naive baseline: no LP-relaxation pruning, no relaxed warm start.
- Solver uses Gurobi internal parallelism. Benders variants: one Gurobi thread per subproblem, concurrent subproblem evaluations capped at available hardware cores. If the machine budget is N physical cores, at most N subproblems are evaluated concurrently.

## 4. Current implementation status

### 4.1 Implemented

- [x] Five algorithm CLI names exposed in `main.cpp`.
- [x] `lifted-ils`, `lifted-two-phase` as self-contained implementations.
- [x] `LShapeStation` shared engine removed.
- [x] `full` as independent file.
- [x] Strengthened feasibility cut via `BuildStationGroupStrengthenedNoGoodCut`.
- [x] `no incumbent found` output for solver timeout.
- [x] Phase 1/Phase 2 timing reporting.
- [x] Old `sg-*` CLI names removed.

### 4.2 Partially implemented

- [ ] Station-group decomposition still present in `lifted_ils`, `lifted_two_phase_ils`, `lshape_relaxed_station`（should be removed）.
- [ ] Natural-ILS still has LP-relaxation pruning and relaxed warm start（should be removed to make it a naive baseline）.
- [ ] Legacy file names remain: `barp_s_integer_l_shaped` → `natural_ils`, `lshape_relaxed_station` → `full_method`, `lshape_station_cut` → `lshape_monotonicity_cut`.

### 4.3 Not yet implemented

- [ ] Monotonicity-strengthened optimality cut（reuses same indicator as feasibility cut）.
- [ ] Standardized log output across all five algorithms.
- [ ] `tower_10` scenario config.
- [ ] `PROJECT_SUMMARY.md` committed.

## 5. Known inconsistencies and risks

### 5.1 `WIRELESS_CHARGING_NO_PLUGIN` consistency

Enum name misleading. Must verify `wireless.cpp` and `wireless_reform.cpp` model the same physical system: plug-in constraints, wireless facility constraints, battery/SoC constraints, battery cost scaling, scenario data, objective components.

### 5.2 Natural-ILS purity

`barp_s_integer_l_shaped.cpp` has LP-relaxation pruning (`kPruneByUb`) and relaxed warm start. These make it not a naive baseline. Ablation comparison natural-ils → lifted-two-phase mixes two differences: (a) decomposition structure, (b) LP acceleration strategy. Remove or disable before ablation runs.

### 5.3 Station-group removal

Code still uses `stationGroups`/`groupThetaVars`/`StationGroupInfo`. Paper formulation is per-scenario. Remove these structures from `lifted_ils.cpp`, `lifted_two_phase_ils.cpp`, and `lshape_relaxed_station.cpp`; rename `BuildStationGroupStrengthenedNoGoodCut` and related utilities to scenario-level monotonicity-cut terminology.

### 5.4 Strengthened optimality cut

Same monotonicity indicator as feasibility cut. Feasibility: `indicator ≥ 1`. Optimality: `θ ≥ Q* − δ · indicator`. Currently only feasibility version implemented.

### 5.5 Solver thread policy

All methods use same hardware budget. Solver uses Gurobi internal parallelism. Benders variants: Threads=1 per subproblem, `std::async` concurrency capped at available cores.

## 6. Build and run instructions

### 6.1 Build

```bat
cmd.exe /d /s /c "call .\run.bat <nul"
```

Requires: Windows 11, MSVC 2022, CMake ≥ 3.20, Gurobi 12.0+. Output: `out/build/x64/Release/wireless.exe`.

### 6.2 CLI commands

```bash
# Full method
./wireless.exe --pb wireless_reform --alg full --data tower

# Ablation
./wireless.exe --pb wireless_reform --alg solver --data tower
./wireless.exe --pb wireless_reform --alg natural-ils --data tower
./wireless.exe --pb wireless_reform --alg lifted-ils --data tower
./wireless.exe --pb wireless_reform --alg lifted-two-phase --data tower
```

### 6.3 Config files

- `solverCfg.txt` — Gurobi default settings except MIPGap, TimeLimit, and thread policy. Thread policy: solver uses Gurobi internal parallelism; Benders variants use one thread per subproblem, with concurrent subproblem evaluations capped by the hardware-core budget（see §8）.
- `data/{type}_data/globalCfg.txt` — operational and charger parameters.
- `data/{type}_data/scenarioCfg.txt` — scenario list and probabilities.
- `data/{type}_data/{scenario}/scenarioMiscCfg.txt` — routes, EBs, timetables.

## 7. Experiment execution plan

### 7.1 Stage 1: quick screening（600s）

All five algorithms @ 600s on all instances（`tower_small` → `tower_medium` → `tower_5` → `tower_10` → `tower_20`）. 2-3 runs each. Goal: find break point where solver/natural-ils fail but `full` still works.

### 7.2 Stage 2: main table（7200s unified）

All five algorithms @ 7200s on instances around the break point, ≥3 runs per cell. Report mean ± std or min/median/max. The five-algorithm ladder itself is the ablation.

### 7.3 Per-run output fields

UB, LB, gap, wall-clock time, iteration count, Phase 1 time, Phase 2 time, LP cut count, feasibility cut count, optimality cut count, incumbent status（found/none）, termination status（converged/timeout/error）.

## 8. Solver parameter policy

All algorithms use Gurobi 12.0 default settings except MIPGap, TimeLimit, and method-specific thread allocation under a common hardware budget. All methods use the same wall-clock time limit, MIPGap, and total hardware budget.

The extensive-form solver uses Gurobi internal parallelism. Decomposition-based methods use one Gurobi thread per subproblem and cap the number of concurrent subproblem evaluations at the same hardware budget. If the machine budget is N physical cores, at most N subproblems should be evaluated concurrently.

Paper states: "All methods are given access to the same hardware. The solver uses Gurobi's internal parallelism. Benders variants use one thread per subproblem and exploit parallelism through concurrent subproblem evaluation, capped at the available core count."

## 9. Checklist

### Public path cleanup

- [x] Five algorithm names exposed in `main.cpp`.
- [x] Old `sg-*` labels replaced.
- [ ] Decide legacy `wireless --alg l2` fate.

### File split / code organization

- [x] Independent files for `lifted-ils`, `lifted-two-phase`.
- [x] `LShapeStation` removed.
- [x] `full` independent.
- [ ] Remove station-group decomposition from all Benders variants.
- [ ] Rename: `barp_s_integer_l_shaped` → `natural_ils`, `lshape_relaxed_station` → `full_method`, `lshape_station_cut` → `lshape_monotonicity_cut`.
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
- [ ] Remove LP-relaxation pruning from natural-ILS.
- [ ] Implement monotonicity-strengthened optimality cut.

### Experiment readiness

- [ ] Phase A/B/C execution.
- [ ] Ablation table.
- [ ] `tower_10` config.
- [ ] Standardized per-run output.
- [ ] ≥3 runs per cell with spread reporting.

## 10. Immediate priorities

1. Remove LP-pruning from `natural-ils` — restores naive baseline.
2. Remove station-group decomposition — unify to per-scenario subproblems.
3. Rename legacy files.
4. Implement monotonicity-strengthened optimality cut — so that `full` matches its target definition（strengthened feasibility and optimality cuts）.
5. Run experiment plan Phases A→B→C.