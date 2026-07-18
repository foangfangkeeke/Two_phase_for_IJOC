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

Codex/agent builds should not invoke `run.bat` by default. Use the validated direct Ninja build command instead:

```powershell
cmd.exe /d /s /c "`"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat`" && cd /d C:\faq_work\Papers\Two_phase_wireless_charging_and_integer_lshape && cmake.exe -S . -B out/build/x64-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe && cmake.exe --build out/build/x64-ninja --config Release"
```

Requires: Windows 11, MSVC 2022, CMake ≥ 3.20, Gurobi 12.0+. Output: `out/build/x64/Release/wireless.exe`.

### 5.2 CLI commands

```bash
# Baseline — original formulation (wireless.cpp)
./wireless.exe --pb wireless --alg solver --data tower
./wireless.exe --pb wireless --alg natural-ils --data tower

# Reformulated methods (wireless_reform.cpp)
./wireless.exe --pb wireless_reform --alg lifted-ils --data tower
./wireless.exe --pb wireless_reform --alg lifted-two-phase --data tower
./wireless.exe --pb wireless_reform --alg full --data tower
```

> Baseline methods (`solver`, `natural-ils`) run under `--pb wireless`. Reformulated methods (`lifted-ils`, `lifted-two-phase`, `full`) run under `--pb wireless_reform`.

### 5.3 Config files

- `solverCfg.txt` — Gurobi default settings except MIPGap, TimeLimit, and thread policy. Thread policy: solver uses Gurobi internal parallelism; Benders variants use one thread per subproblem, with concurrent subproblem evaluations capped by the hardware-core budget（see §7）.
- `data/{type}_data/globalCfg.txt` — operational and charger parameters.
- `data/{type}_data/scenarioCfg.txt` — scenario list and probabilities.
- `data/{type}_data/{scenario}/scenarioMiscCfg.txt` — routes, EBs, timetables.

## 6. Experiment execution plan

### 6.1 Table 1: network-scale ablation

Run all five algorithms on the three-scenario network-scale ladder:

| Instance | Solver | Natural ILS | Lifted ILS | Lifted Two-phase | Full |
|----------|--------|-------------|------------|------------------|------|
| `tower_small` | run | run | run | run | run |
| `tower_medium` | run | - | - | run | run |
| `tower` | run | - | - | run | run |

`-` means not run because the method failed to close the gap on a smaller instance within the time limit. Current script still executes only the methods that remain useful for later stages according to its stop rules.

### 6.2 Table 2: scenario-scale scalability

Scenario-scale runs use the full Tower network and start from `tower_5`; the 3-scenario `tower` row is reused from Table 1.

| # Scenarios | Extensive-form Solver | Lifted Two-phase ILS | Full Method |
|------------:|-----------------------|----------------------|-------------|
| 3 | see Table 1 | see Table 1 | see Table 1 |
| 5 | run if Table 1 tower completed | run if Table 1 tower completed | run if Table 1 tower completed |
| 10 | run if previous scenario setting completed | run if previous scenario setting completed | run if previous scenario setting completed |
| 20 | run if previous scenario setting completed | run if previous scenario setting completed | run if previous scenario setting completed |
| 40 | run if previous scenario setting completed | run if previous scenario setting completed | run if previous scenario setting completed |

Notes:
- `experiment.sh` starts Experiment 2 from `tower_5`, not `tower`, to avoid rerunning the Table 1 `tower` case.
- An algorithm enters Experiment 2 only if it completed `tower` in Experiment 1.
- The extensive-form solver may use a memory-conservative configuration for scenario-scale runs when the default configuration runs out of memory.

### 6.3 Per-run output fields

UB, LB, global gap, total wall-clock time, master solve time, Phase 1 time, Phase 2 time, iteration count, LP cut count, feasibility cut count, optimality cut count, incumbent status（found/none）, model-level termination status, outer-loop termination status（converged/timeout/error）.

## 7. Solver parameter policy

All algorithms use Gurobi 12.0 default settings except MIPGap, TimeLimit, and method-specific thread allocation under a common hardware budget.

MIPGap settings:
- **Stage 1 (600s screening)**: all Gurobi MIPGap = 0.
- **Stage 2 (7200s main table)**: EF Solver MIPGap = 0.01%, Benders master MIPGap = 0, Phase 2 subproblem MIPGap = 0.
- Benders master MIPGap is always 0. This guarantees `ObjBound = master最优值`, so the outer-loop LB is not distorted by master imprecision. The master is a relaxation and its optimal value is a valid global LB.
- Phase 1 operational relaxation is an LP and does not use MIPGap.
- Outer-loop global convergence tolerance = `solverConfig.mipGap` (0% for Stage 1, 0.01% for Stage 2). Gap is computed as `(UB − LB) / max{1, |UB|}`. A numerical gap is reported only when a finite incumbent exists (`UB < ∞`); otherwise report `—` or `undefined`.
- Outer-loop lower bound is obtained from the Benders master `ObjBound`, not from the master incumbent objective value.

TimeLimit settings:
- All methods use the same wall-clock time limit.
- For the EF Solver, TimeLimit is set directly on the Gurobi model.
- For decomposition methods, the outer loop tracks elapsed wall-clock time at each iteration and exits when the limit is reached.

The extensive-form solver uses Gurobi internal parallelism. Decomposition-based methods use one Gurobi thread per subproblem and cap the number of concurrent subproblem evaluations at the same hardware budget. If the machine budget is N physical cores, at most N subproblems should be evaluated concurrently.

Paper states: "All methods are given access to the same hardware. The solver uses Gurobi's internal parallelism. Benders variants use one thread per subproblem and exploit parallelism through concurrent subproblem evaluation, capped at the available core count."

## 8. Checklist

### Public path cleanup

- [x] Five algorithm names exposed in `main.cpp`.
- [x] Old `sg-*` labels replaced.
- [x] Rename `--alg l2` → `--alg natural-ils` under `--pb wireless`.
- [x] Remove `solver` and `natural-ils` from `--pb wireless_reform`.

### File split / code organization

- [x] Independent files for `lifted-ils`, `lifted-two-phase`.
- [x] `LShapeStation` removed.
- [x] `full` independent.
- [x] Remove station-group decomposition from all Benders variants.
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
- [x] Implement monotonicity-strengthened optimality cut.

### Experiment readiness

- [ ] Stage 1 & Stage 2 execution.
- [ ] `tower_10` config.
- [ ] Standardized per-run output.
- [ ] ≥3 runs per cell with spread reporting.
- [x] Benders master MIPGap fixed at 0; outer-loop LB via `ObjBound`; outer-loop convergence tolerance via `solverConfig.mipGap`.

## 9. Full-method implementation notes

### 9.1 Effective implementation optimization

The only optimization currently supported by clean evidence and suitable for the main exact algorithm is Phase 1 route-subproblem initialization parallelization.

- Previous Phase 1 route SP initialization was serial inside `LShapeRelaxed::Initialize()`.
- The useful change is to create each `GRBEnv` / `GRBModel` serially, then run `InitSubProblem()` with bounded parallelism.
- Clean `tower_20` evidence: route SP initialization dropped from roughly `34-36s` to `5-6s` for 560 route subproblems.
- Total runtime still has substantial master/SP solve variance, but the initialization improvement itself is real and does not change the mathematical algorithm.

### 9.2 Decisions for the exact algorithm

- Do not use partial SP evaluation in Phase 2. It becomes a delayed/incomplete separation heuristic, requires extra tuning and final full verification, and is harder to justify as the main exact algorithm.
- Do not use nonzero MIPGap for Phase 2 SP MIPs. Exact SP separation is required for cut validity, UB evaluation, and clean convergence logic.
- Do not force aggressive presolve as a default optimization. Experiments did not show stable speedup, and LP/Farkas-related paths make the policy harder to explain.
- Do not prioritize MIP starts for Phase 2 SPs. A previous-round SP solution is feasible only for the previous `z`; after the master changes `z`, Gurobi may need to repair or discard the start, so expected benefit is uncertain.
- Do not overlap Phase 2 master optimization with scenario-SP initialization as a main-line change. It may hide some init time, but it can also slow the master by competing for CPU, memory bandwidth, and Gurobi resources.

### 9.3 RSP role

Keep RSP as an algorithmic step. Its main role is not only speed: it handles the feasible-region gap between Phase 1 and Phase 2. Phase 1 uses a relaxed/aggregated representation, so a Phase 1 master solution may be infeasible for Phase 2 scenario MIPs. RSP can detect infeasibility before expensive SP MIP solves and generate feasibility cuts.

Current checked logs mostly show RSP producing optimality cuts (`status=0`, `isOptCut=1`) rather than infeasibility cuts. This means the robustness role is not visible on the current data set, but it should remain in the algorithm and be explained briefly in the paper-stage narrative.

### 9.4 Known issues and handling

- Large `tower_40` scenario MIPs: earlier runs were dominated by scenarios with large route/global demand increases, which created very large row/column counts. The handling is to keep generated benchmark scenarios local and moderate rather than using global or half-global positive-demand cases.
- Phase 1 checkpoint validity: checkpoint files are data-specific. After regenerating `data/tower_40_data`, old files under `debug/full_phase1/tower_40_data` must not be reused.
- Phase 2 checkpoint debugging: keep `phase1.Initialize()` and only replace the slow `phase1.Solve()` path when debugging Phase 2. Directly skipping all Phase 1 initialization requires rebuilding too much Gurobi/model state by hand.
- Negative reported gap: if `LB > UB` appears, first check whether master `z` values are rounded before subproblem evaluation, whether LB was updated after new cuts while UB still comes from an older incumbent, and whether master/subproblem MIPGap settings are inconsistent with the intended exact solve.

No additional code-change TODO is carried from this debug pass. The remaining work is experiment execution according to the plan in Section 6.
