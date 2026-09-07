# Algorithms and Runtime Options

## Algorithm Entries

| Problem | Algorithm | Meaning | RSP |
|---|---|---|---|
| `wireless` | `solver` | Monolithic solver benchmark | Not used |
| `wireless` | `natural-ils` | Classical integer L-shaped baseline | Default on |
| `wireless_reform` | `lifted-ils` | Lifted master/subproblem decomposition | Default on |
| `wireless_reform` | `lifted-monotone` | Lifted ILS with monotone cuts | Default on |
| `wireless_reform` | `lifted-two-phase` | Two-phase method with Phase 1 route relaxation and Phase 2 exact separation | Default on |
| `wireless_reform` | `full` | Full method with strengthened Phase 1/Phase 2 cuts | Default on |

`solver` is not an iterative decomposition algorithm, so it does not use RSP. All decomposition algorithms accept the shared RSP switch.

## Full Algorithm Logic

`full` is the strongest two-phase variant in the current code. It uses the same Phase 1 route-level relaxation framework as `lifted-two-phase`, then changes the Phase 2 exact MIP cuts to strengthened monotone cuts.

### Initialization and Phase 1

1. Build the relaxed route-level L-shaped model through `LShapeRelaxed`.
2. Read runtime data options before model construction, including scenario count, charger capacity, facility cost scale, and energy scale.
3. Build the master variables and constraints.
4. Create one route-level recourse proxy for each active `(scenario, route)`:

```text
theta_scenario_<scenario>_route_<route>
```

5. Add the Phase 1 global link:

```text
sum route_theta <= theta
```

6. Build route-level LP subproblems. These route subproblems remove the exact cross-route plug-in capacity coupling.
7. Iterate:

```text
solve Phase 1 master
solve all route LP subproblems
add route-level feasibility cuts if any route LP is infeasible
otherwise update Phase 1 incumbent UB and add route-level LP optimality cuts
stop when Phase 1 gap reaches solverConfig.mipGap
```

8. Phase 1 returns the tightened master, route theta variables, all Phase 1 cuts, the Phase 1 incumbent design, and the Phase 1 lower bound.

Phase 1 UB is not reused as a valid Phase 2 UB because Phase 1 and Phase 2 use different operational subproblems.

### Phase 1 to Phase 2 Transition

1. Keep the Phase 1 master model and all existing route-level cuts.
2. Keep all route-level theta variables.
3. Remove the Phase 1 global route-theta link:

```text
theta_route_sum
```

4. Add one Phase 2 scenario-level theta variable for each scenario subproblem:

```text
scenario_theta_p2_<idx>
```

5. Link each scenario's route-level lower approximation to its scenario theta:

```text
sum_{routes in scenario} theta_scenario_route <= scenario_theta_p2
```

6. Add the Phase 2 global link:

```text
sum scenario_theta_p2 <= theta
```

The route-level Phase 1 cuts remain disaggregated. They are only aggregated through the scenario-level link.

7. Initialize Phase 2 exact scenario MIP subproblems.
8. Apply the Phase 1 design as a MIP start for Phase 2 master variables.
9. Reset Phase 2 UB to `inf`; keep Phase 1 LB as the initial Phase 2 lower bound.

### Phase 2 Iteration

Each Phase 2 iteration follows this order:

```text
solve Phase 2 master
read ObjBound and update LB
read incumbent design z and theta
optional RSP pass
exact scenario MIP pass
update UB if all exact scenario MIPs are feasible
add exact MIP cuts
check outer gap
```

The Phase 2 master currently uses:

```text
MIPGap = solverConfig.mipGap
```

The lower bound is updated from Gurobi `ObjBound`, not from the incumbent objective.

### RSP Pass

If RSP is enabled, `full` solves each Phase 2 scenario subproblem as an LP relaxation before the exact MIP pass. This is an auxiliary separation step inside Phase 2. It is not a third phase, and it does not provide the final exact recourse evaluation.

The RSP pass uses the current Phase 2 master incumbent:

```text
zValues = current master design and battery-capacity variables
scenarioThetaVars_ = current scenario recourse proxies
```

Then it evaluates all Phase 2 scenario subproblems with:

```text
relaxed = true
```

That calls:

```text
SolveRelaxedSubProblem(...)
```

for each scenario subproblem. The RSP model keeps the Phase 2 scenario scope but relaxes the exact operational MIP.

RSP can add only LP-based cuts:

| RSP result | Cut added |
|---|---|
| LP relaxation infeasible | Standard LP feasibility cut from the relaxed subproblem |
| LP relaxation feasible and `scenario_theta_p2 < relaxed Q` | Standard LP optimality cut on `scenario_theta_p2` |

The detailed RSP control flow is:

1. Solve all relaxed scenario subproblems.
2. If a relaxed scenario subproblem is infeasible, mark `rspAnyInfeasible = true`.
3. For every infeasible relaxed scenario, add its standard LP feasibility cut to the Phase 2 master.
4. If `rspAnyInfeasible = true`, update the master and immediately continue to the next Phase 2 iteration.
5. If all relaxed scenario subproblems are feasible, compare each relaxed objective value with its current scenario theta.
6. If:

```text
scenarioThetaVars_[idx] + tolerance < relaxedSubObj[idx]
```

add a standard LP optimality cut to:

```text
scenarioThetaVars_[idx]
```

7. If only RSP optimality cuts are added, update the master but continue into the exact MIP pass in the same iteration.

Therefore, RSP has two different effects:

| RSP cut type | Same iteration exact MIP pass? | Reason |
|---|---|---|
| LP feasibility cut | No | The current incumbent already violates the LP relaxation, so it cannot be exact feasible |
| LP optimality cut | Yes | The incumbent may still be exact feasible; the LP cut only tightens the scenario recourse lower bound |

If RSP is disabled with `--rsp 0`, `full` skips this entire pass and goes directly from Phase 2 master incumbent to exact scenario MIP evaluation.

RSP does not add pointwise MIP no-good cuts or monotonicity-strengthened MIP cuts.

### Exact MIP Pass and Full Cuts

After the RSP pass is skipped or produces no infeasibility cut, `full` solves all exact scenario MIP subproblems.

If at least one exact scenario MIP is infeasible:

1. The current master incumbent is not a feasible original solution.
2. No UB is updated.
3. For each infeasible scenario, `full` adds a scenario strengthened no-good cut:

```text
BuildScenarioStrengthenedNoGoodCut(...)
```

This replaces the ordinary pointwise feasibility no-good cut in the full method.

If all exact scenario MIPs are feasible:

1. Compute exact recourse value:

```text
aggregatedQValue = sum probability-weighted exact scenario MIP costs
```

2. Compute the candidate original objective:

```text
candidateUB = fixedCost + aggregatedQValue
```

3. Update incumbent UB if `candidateUB` improves the current UB.
4. For each feasible scenario, add a monotonicity-strengthened optimality cut:

```text
BuildMonotonicityStrengthenedOptimalityCut(...)
```

The cut is added to the corresponding scenario theta:

```text
cutExpr <= scenario_theta_p2_<idx>
```

This replaces the ordinary pointwise exact MIP optimality cut in the full method.

### Cut Summary for Full

| Stage | Condition | Cut type |
|---|---|---|
| Phase 1 route LP | Infeasible route LP | Route-level LP feasibility cut |
| Phase 1 route LP | Feasible route LP | Route-level LP optimality cut on route theta |
| Phase 2 RSP | Infeasible scenario LP relaxation | Standard scenario LP feasibility cut |
| Phase 2 RSP | Feasible scenario LP relaxation and theta underestimates relaxed Q | Standard scenario LP optimality cut on scenario theta |
| Phase 2 exact MIP | Infeasible exact scenario MIP | Scenario strengthened no-good cut |
| Phase 2 exact MIP | Feasible exact scenario MIP | Monotonicity-strengthened optimality cut on scenario theta |

The full method uses cut pools for the two exact MIP monotone cut families to avoid re-adding duplicate strengthened no-good and strengthened optimality cuts.

## Runtime Options

| Option | Default | Applies to | Effect |
|---|---:|---|---|
| `--data <name>` | `test` | All algorithms | Reads `data/<name>_data` |
| `--scenario-count <N>` | `5` | All algorithms | Uses the first `N` entries in `scenarioCfg.txt` and assigns uniform probability `1/N` |
| `--cap <N>` | `5` | All algorithms | Overrides every charging station capacity with `N` at data load time |
| `--facility-cost-scale <v>` | `1` | All algorithms | Multiplies charger facility costs and inverter cost by `v` |
| `--energy-scale <v>` | `1` | All algorithms | Multiplies energy consumption coefficient `mu` by `v` |
| `--rsp <0/1>` | `1` for decomposition algorithms | Decomposition algorithms | Enables or disables RSP cuts |
| `--wireless-option <0/1>` | `1` | `solver` only | Enables or disables wireless charging variables in the monolithic solver |
| `--export-solution` | off | `solver`, `full` | Exports solution CSV |
| `--export-dir <path>` | empty | `solver`, `full` | Sets solution export directory and enables export |
| `--fixed-facilities <csv>` | empty | `full` | Fixes facility variables from a CSV before Phase 1 |

Numeric runtime options must be positive.

## Bound Logging

Iterative algorithms now print bounds each iteration. If no feasible incumbent has been found, the log prints:

```text
UB=inf, LB=<current lower bound>, gap=1
```

Once a valid incumbent exists, the reported gap is:

```text
(UB - LB) / max(1, abs(UB))
```

The artificial `gap=1` used with `UB=inf` is only a log display value. It is not used for convergence.

## Example Commands

Run the monolithic benchmark on the small tower case:

```bat
.\out\wireless.exe --pb wireless --alg solver --data tower_small --scenario-count 5 --cap 5
```

Run lifted two-phase with default RSP:

```bat
.\out\wireless.exe --pb wireless_reform --alg lifted-two-phase --data tower_small --scenario-count 5 --cap 5
```

Run full with RSP disabled:

```bat
.\out\wireless.exe --pb wireless_reform --alg full --data tower_small --scenario-count 5 --cap 5 --rsp 0
```

Run full with scaled facility cost and energy consumption:

```bat
.\out\wireless.exe --pb wireless_reform --alg full --data tower_base --scenario-count 10 --cap 5 --facility-cost-scale 3 --energy-scale 2
```

## Current Small-Case Script

`run_tower_small_rsp_matrix.bat` currently runs:

| Run | Command core |
|---|---|
| solver baseline | `--pb wireless --alg solver` |
| natural ILS | `--pb wireless --alg natural-ils` |
| lifted ILS | `--pb wireless_reform --alg lifted-ils` |
| lifted monotone | `--pb wireless_reform --alg lifted-monotone` |
| lifted two-phase | `--pb wireless_reform --alg lifted-two-phase` |
| full | `--pb wireless_reform --alg full` |

The script uses these editable defaults:

```bat
set "DATA=tower_small"
set "SCENARIO_COUNT=5"
set "CAP=5"
set "FACILITY_COST_SCALE=1"
set "ENERGY_SCALE=1"
```

RSP is not written explicitly in the script because it is enabled by default for all decomposition algorithms.

## Log Labels

Log folders include the runtime scenario count and capacity, for example:

```text
logs\tower_small_5_cap5\
logs\tower_base_10_cap5\
```

The log file name includes the case label, algorithm, problem type, and timestamp.

## Script Result Logging

Experiment scripts print one normalized result line for each run:

```text
RESULT label=<case> status=<OK|ACCEPT|FAIL|SKIP> reason=<reason> exit_code=<code> final_gap=<gap|NA> log="<path>"
```

Status meanings:

| Status | Meaning |
|---|---|
| `OK` | The executable returned `0` and the log did not report a time-limit termination |
| `ACCEPT` | The run did not finish normally, but the final parsed gap is below the script threshold |
| `FAIL` | The executable failed, timed out, or reported time limit with insufficient/no parsed gap |
| `SKIP` | A larger case was skipped because the required smaller case did not complete |

The scripts parse the latest log produced after each run starts. They detect time-limit completion only from explicit program status text such as:

```text
TIME_LIMIT_WITH_INCUMBENT
reached time limit
time limit after Phase 1
```

They do not treat a generic Gurobi parameter name such as `TimeLimit` as a run failure.

For `experiment_by_algorithm.bat`, a run with time limit or nonzero exit code is allowed to continue only if:

```text
final_gap < ACCEPT_GAP_PERCENT
```

The current default is:

```bat
set "ACCEPT_GAP_PERCENT=1.0"
```
