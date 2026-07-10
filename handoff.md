# Handoff

## Current intent

The project is being reorganized around a defensible paper narrative for wireless charging facility deployment and adaptive battery configuration under service-plan uncertainty.

The intended experimental / algorithm ladder is:

1. `solver`: direct extensive-form MILP baseline.
2. `natural-ils`: natural integer L-shaped baseline with master `w` only and scenario subproblem jointly optimizing battery configuration `z_omega` and charging operation `y_omega`.
3. `lifted-ils`: lifted reformulation, with scenario-dependent battery variables lifted to the master, but no two-phase relaxation.
4. `lifted-two-phase`: lifted reformulation plus LP-first / MIP-verification two-phase structure, without strengthened cuts.
5. `full`: lifted reformulation plus two-phase structure plus strengthened cuts.

Important modeling interpretation:

- `w` is the only cross-scenario here-and-now strategic decision.
- Battery variables `z_{omega,r,cap}` are scenario-dependent adaptive recourse decisions.
- Lifting `z_omega` into the master is an exact reformulation / decomposition device, not a change in decision timing.
- The master is not a pure first-stage problem; it contains first-stage facility variables and lifted scenario-dependent battery variables.
- Plug-in / fast chargers are exogenous existing charging opportunities and only appear as operational subproblem constraints.

## Current code status

Current uncommitted work mainly performs naming / entry-point cleanup and output clarification:

- `src/main.cpp`
  - Public `wireless_reform` CLI now exposes:
    - `solver`
    - `natural-ils`
    - `lifted-ils`
    - `lifted-two-phase`
    - `full`
  - `wireless_reform --alg solver` maps to `UsingSolver(WIRELESS_CHARGING_NO_PLUGIN, dataFolder)`.
  - This is intentional: `reform` is a decomposition reformulation, not a separate direct-solver problem type.
  - Invalid problem / algorithm choices now return exit code `1`.

- `src/algorithm/using_solver.cpp`
  - Solver timeout with no incumbent now prints `no incumbent found`, not `no feasible solution`.

- `src/algorithm/lshape_relaxed_station.cpp`
  - Adds Phase 1 elapsed time and total two-phase elapsed time reporting.
  - Updates Phase 2 lower-bound tracking to use Gurobi `ObjBound` instead of the current incumbent objective value.

- `src/algorithm/lshape_relaxed_station.hpp`
  - Comments now describe Phase 2 as station-group MIP verification, not LP.

- `src/algorithm/lshape_station.hpp`
  - Comments now align variants with `lifted-ils` and `lifted-two-phase`.

- `PROJECT_SUMMARY.md`
  - Untracked project narrative / task document.
  - It records the current methodology logic and the desired algorithm ladder.

## Build command

On Windows, do not run `run.bat` directly through PowerShell.

Use:

```bat
cmd.exe /d /s /c "call .\run.bat <nul"
```

`run.bat` was adjusted so `SCRIPT_DIR` does not keep the trailing backslash, because the trailing backslash can break quoted CMake arguments.

## Should step 1 delete invalid code logic?

Yes, but only for logic that is already confirmed invalid or misleading.

The first cleanup step should delete or hide:

- obsolete public algorithm names such as `sg-full`, `sg-relax`, `sg-relax-station`, and `sg-perstation`;
- any direct `WIRELESS_CAPACITY_REFORM` solver path, because reformulation is the ILS/decomposition restructuring, not a new direct solver;
- comments or docs implying that `reform` is a separate direct formulation;
- result wording that treats `UB = inf` as infeasibility rather than no incumbent found.

The first cleanup step should not yet delete:

- classes that are still needed to implement `lifted-ils`, `lifted-two-phase`, or `full`;
- internal station-group code used by the full method;
- older baseline code before verifying it is not used by `solver` or `natural-ils`;
- any historical implementation that may be needed for ablation until its replacement is confirmed.

In short: first remove invalid public logic and misleading paths, then remove internal dead code after dependency tracing.

## Next-step plan

## Checklist

### Public path cleanup

- [x] Expose the five paper-facing algorithm names in `main.cpp`.
- [x] Map `wireless_reform --alg solver` to the original direct EF-MILP baseline.
- [x] Remove the misleading direct `WIRELESS_CAPACITY_REFORM` solver path.
- [x] Replace user-facing `sg-relax` / `sg-full` labels in the two-phase wrapper.
- [ ] Decide whether to keep or remove legacy `wireless --alg l2`.
- [ ] Decide whether to keep `wireless` as a legacy problem alias after all experiments migrate to `wireless_reform`.

### File split / code organization

- [ ] Split materially different algorithms into independent files instead of relying on large flag branches.
- [x] Add independent public entry classes/files for `lifted-ils` and `lifted-two-phase`.
- [x] Make `lifted-ils` a self-contained implementation instead of a wrapper around `LShapeStation`.
- [x] Remove unused strengthened/per-station branches from the lifted-ILS engine.
- [ ] Keep shared utilities in common files only when the behavior is genuinely identical across variants.
- [ ] Separate `lifted-ils` from `lifted-two-phase` if the Phase 1 relaxation branch remains substantial.
- [ ] Keep `full` separate from ablation variants because it has the sequential Phase 1/Phase 2 wrapper and strengthened cuts.
- [ ] Keep station-group data structures as implementation internals unless they become part of a public algorithm interface.
- [ ] Update `CMakeLists.txt` whenever a new algorithm file is introduced.

### Terminology and output cleanup

- [x] Print `no incumbent found` instead of `no feasible solution` when `UB = inf`.
- [x] Report Phase 1, Phase 2, and total two-phase elapsed time.
- [x] Rename `LShapeStation` runtime output from `Station-group Benders` to lifted-ILS labels.
- [ ] Standardize all algorithm headers and result logs across `solver`, `natural-ils`, `lifted-ils`, `lifted-two-phase`, and `full`.
- [ ] Rename internal comments that still make station-group terminology sound like a paper-level formulation rather than an implementation unit.

### Model consistency checks

- [ ] Verify whether `WIRELESS_CHARGING_NO_PLUGIN` is only a legacy enum name or actually changes plug-in / fast-charger modeling.
- [ ] Compare objective terms between `wireless.cpp` and `wireless_reform.cpp`.
- [ ] Compare battery cost scaling by scenario fleet size.
- [ ] Compare plug-in / fast-charger capacity constraints.
- [ ] Compare wireless station / link availability constraints.
- [ ] Compare SoC and energy conservation constraints.

### Algorithm contribution checks

- [x] Lifted reformulation is represented by lifted scenario-dependent battery variables in the master.
- [x] Two-phase structure is represented by Phase 1 operational relaxation followed by Phase 2 MIP verification.
- [x] Strengthened feasibility cut is implemented through station-group strengthened no-good cuts.
- [ ] Implement monotonicity-strengthened optimality cut if it will be claimed as part of the strengthened-cut contribution.
- [ ] Add ablation output that separates `lifted-ils`, `lifted-two-phase`, and `full`.

### Experiment readiness

- [ ] Use a unified final-paper time limit, currently planned as 7200s.
- [ ] Ensure `UB = inf` rows are labeled as no incumbent, not infeasible.
- [ ] Avoid numerical gap reporting when no UB exists unless explicitly defined.
- [ ] Print enough fields for automated tables: UB, LB, gap, time, iterations, cut counts, Phase 1 time, Phase 2 time, incumbent status.

### Step 1: Clean public algorithm paths

Goal: make the five algorithm names the only supported experiment-facing names.

Tasks:

- Keep `solver`, `natural-ils`, `lifted-ils`, `lifted-two-phase`, and `full`.
- Remove or reject old `sg-*` algorithm names from `main.cpp`.
- Confirm `wireless_reform --alg solver` maps to the original direct EF-MILP baseline.
- Confirm `wireless_reform --alg natural-ils` maps to the original natural ILS baseline.
- Keep old `wireless --alg solver/l2` only as legacy aliases if useful, or remove later after experiments are migrated.

### Step 2: Align names, logs, and docs

Goal: make runtime output match the paper structure.

Tasks:

- Rename log labels from `sg-relax`, `sg-full`, and station-group-first language to:
  - Phase 1 operational relaxation
  - Phase 2 MIP verification
  - lifted ILS
  - lifted two-phase
  - full method
- Keep station-group terminology as implementation detail only.
- Ensure all timeout / no-incumbent messages are consistent.

### Step 3: Verify baseline model consistency

Goal: make the experimental comparison defensible.

Tasks:

- Compare `wireless.cpp` baseline and `wireless_reform.cpp` lifted variants for:
  - plug-in / fast-charger constraints;
  - wireless facility constraints;
  - battery capacity and SoC constraints;
  - battery cost scaling by scenario fleet size;
  - scenario data and probabilities;
  - objective components.
- Decide whether `WIRELESS_CHARGING_NO_PLUGIN` is just a legacy enum name or actually lacks plug-in logic.
- If the baseline omits plug-in logic while the proposed method includes it, either align the code or separate the comparison clearly.

### Step 4: Implement missing contribution logic if claimed

Goal: make the three claimed contributions match code and experiments.

Current contribution mapping:

- Lifted reformulation: implemented through lifted `z_omega` in the master.
- Two-phase method: implemented through Phase 1 relaxation followed by Phase 2 MIP verification.
- Strengthened cuts:
  - strengthened feasibility cut appears implemented;
  - monotonicity-strengthened optimality cut is not yet fully implemented if the code still uses a classic Hamming-distance integer optimality cut.

Before claiming both strengthened feasibility and optimality cuts as one proved contribution, implement and test the monotonicity-strengthened optimality cut.

### Step 5: Prepare experimental output

Goal: make results table generation straightforward.

Tasks:

- Standardize printed fields:
  - algorithm name;
  - dataset;
  - UB;
  - LB;
  - gap;
  - incumbent status;
  - iteration count;
  - Phase 1 time;
  - Phase 2 time;
  - total time;
  - number of LP cuts;
  - number of integer feasibility cuts;
  - number of integer optimality cuts.
- Use 7200s as the final paper time limit unless a separate run-to-convergence table is explicitly used.

## Immediate recommendation

Do Step 1 next.

Do not start by deleting entire source files. Start by deleting or rejecting invalid entry points and misleading code paths, then use `rg` to trace which internal classes are still reachable from the five intended algorithms.
