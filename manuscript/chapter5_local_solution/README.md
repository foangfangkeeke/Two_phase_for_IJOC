# Chapter 5 matched planning solutions

These files support Section 5.3's matched planning comparisons for R12-N5-F50-C4 and R28-N5-F50-C4. They are **solution values only**; this computer is not the target performance machine. Do not use the local logs for runtime, algorithm gap trajectories, or method comparisons in Sections 5.1–5.2.

## Provenance

- Model and algorithms: `codex/repair-monotone@d030988946e6244fc268f0c596dc813b173f8ec2` in the isolated worktree `../Lifted-Two-Phase-support/2026-11-08-cleanup/repair-worktree`.
- R12-N5 and R28-N5 inputs: `data/R12_N5_data` and `data/R28_N5_data` from `origin/codex/final-experiments-2026-11-05@3abd7c10edbf6b912564378b62ff55528ef7ec95`. All 24 local R12 input files match that Git tree after normalizing CRLF/LF line endings; R28 files were copied directly from that tree. The original repair branch's R12 input uses a different scenario set.
- The worktree adds post-solve export code (`solution_export.cpp/.hpp` and calls from Full/EF). It does not alter the optimization model. COPT 8.0.7 solved both cases with the same five equally likely scenarios, 50 candidate DWC links, four chargers per existing plug-in location, 50-kWh battery increments, and cost scales of 1.
- The DWC-allowed local objective and components reproduce the historical target-machine L2P incumbent: 8197.972066 total, including 378.00 facility, 2526.36 battery, 3891.601024 daytime energy, and 1402.011042 overnight energy.

## Commands

From the writing repository root, locate the preserved worktree:

```powershell
$paperSupport = Join-Path (Split-Path (Get-Location).Path -Parent) 'Lifted-Two-Phase-support/2026-11-08-cleanup'
$paperRepair = Join-Path $paperSupport 'repair-worktree'
Set-Location $paperRepair
```

Run the commands below from that worktree after rebuilding `out/wireless.exe`. The cleanup removed the rebuildable `out/` directory and preserved all source edits, input data and solution exports:


```text
out/wireless.exe --alg data-check --data R12_N5 --scenario-count 5 --cap 4 --dwc-top-n 50 --battery-step 50
out/wireless.exe --alg no-dwc-feasibility --data R12_N5 --scenario-count 5 --cap 4 --dwc-top-n 50 --battery-step 50 --facility-cost-scale 1 --energy-scale 1
out/wireless.exe --alg full --data R12_N5 --scenario-count 5 --cap 4 --dwc-top-n 50 --battery-step 50 --facility-cost-scale 1 --energy-scale 1 --rsp-workers 3 --rsp-threads 4 --wireless-option 0 --export-solution --export-dir results/chapter5_local/dwc_off
out/wireless.exe --alg full --data R12_N5 --scenario-count 5 --cap 4 --dwc-top-n 50 --battery-step 50 --facility-cost-scale 1 --energy-scale 1 --rsp-workers 3 --rsp-threads 4 --wireless-option 1 --export-solution --export-dir results/chapter5_local/dwc_on
```

`solverCfg.txt` specifies a two-hour limit, 12 threads, and 0.0001 native MIP gap. The no-DWC feasibility check found all five scenarios feasible with maximum batteries. Both Full runs then returned finite incumbents; their exported cost sums equal the reported objectives to nine decimal places. The no-DWC run has zero reported outer gap, and the DWC-allowed run has a 0.000589% outer gap, below Full's 1% stopping criterion.

## Files and definitions

- `dwc_off/` and `dwc_on/` each contain `summary.csv`, `scenarios.csv`, `batteries.csv`, and `facilities.csv`.
- Charging kWh are grid-side model variables, before the charging efficiencies enter battery-state equations. Scenario probabilities weight the expected values in `summary.csv`.
- `mean_route_capacity_kwh` is the probability-weighted average over 12 routes per scenario. `mean_bus_capacity_kwh` weights each route's selected battery size by its number of buses in that scenario.
- The exported charging allocation is one optimized solution and need not be unique. Facility and battery selections are likewise reported for this solution pair.
- `dwc_off_solver.log` and `dwc_on_solver.log` document solver status and incumbent costs. Their timings are retained for provenance only.

## R28-N5-F50-C4 extension

The same commands were run with `--data R28_N5 --cap 4`, exporting to `r28_c4_dwc_off/` and `r28_c4_dwc_on/`. The R28 input has five equally likely scenarios and 28 routes. A separate no-DWC feasibility check found all five scenarios feasible. Both exported cost sums equal their reported incumbents to nine decimal places.

| Case | Lower bound | Incumbent | Outer gap | Battery cost | Day cost | Night cost | Facility cost |
|---|---:|---:|---:|---:|---:|---:|---:|
| No DWC | 18011.807813 | 18190.972177 | 0.984908% | 7562.80 | 6468.912200 | 4159.259978 | 0 |
| DWC allowed | 17425.712220 | 17478.568951 | 0.302409% | 5857.84 | 8031.245591 | 3224.983359 | 364.50 |

The incumbent cost difference is 712.403227 (3.9162% of the no-DWC incumbent). Bound arithmetic certifies a positive absolute cost reduction between 533.238862 and 765.259957, or a relative reduction between 2.9605% and 4.2068%. The DWC-allowed incumbent reproduces the historical R28-N5-F50-C4 L2P objective to the reported precision. Its exported solution installs 26 DWC links. The bus-weighted mean route battery capacity is 182.63 kWh without DWC and 141.46 kWh with DWC; 82 of 140 route–scenario selections are smaller and 58 are unchanged in the exported pair. The daytime share of expected grid-side charging rises from 49.94% to 61.50%.

The R28-C4 `route_energy.csv` files add route-level plug-in, DWC, and overnight grid-side kWh for each scenario. The export records the route attached to each charging variable without changing the model. The first incremental rebuild missed translation units that use the expanded `ScenarioVariables` structure and crashed during initialization; a clean rebuild followed by an R12 smoke solve and weighted-energy reconciliation corrected this. Both R28-C4 runs were repeated with that clean build and reproduced the exact incumbents and cost decompositions above. Run `python verify_r28.py` to check the 140 matched route–scenario rows, weighted energy totals, and value interval.

Of the 140 matched route–scenario choices, 66 use DWC and select a smaller battery, 16 use DWC with unchanged battery, 16 select a smaller battery without direct DWC use, and 42 do neither. The 16 cells without direct DWC use belong to routes 282, 284, 285, and 941; in the exported pair, each shifts some charging from overnight to daytime plug-in. This is consistent with reallocation of shared plug-in opportunities, though the exported charging allocation need not be unique. Route 41 averages 543.02 grid-side DWC kWh and 130 kWh less selected battery capacity across the five scenarios; route 66 averages 751.04 DWC kWh and 100 kWh less battery capacity.

The C2 and C3 no-DWC feasibility checks also found all five scenarios feasible. Section 5.3 uses the C3 no-DWC EF result below with the final target-machine DWC-allowed Full result. The C2 weak DWC incumbent is not interpreted as a route-level plan. A cross-capacity substitution claim requires separated certified value intervals.

## R28-N5-F50-C3 direct EF check

The no-DWC case was also solved as one five-scenario extensive-form MIP with `--alg ef --wireless-option 0`, native MIP gap 1%, 12 threads, and the same input and export build noted above. This EF run has no decomposition subproblems. The model has 1,318,549 variables and 2,455,200 constraints. It returned LB 18435.763070 and incumbent 18558.249517 (0.660011% gap); the exported cost components sum to the incumbent at nine decimal places. The solution is in `r28_c3_ef_dwc_off/`, and the full solver summary is `r28_c3_ef_dwc_off_full.log`. The original stdout redirect is `r28_c3_ef_dwc_off_solver.log` and contains the generated log path. The DWC-allowed side can use valid Full bounds from the same planning model; a second EF run is not required for a matched cost comparison. Local timings are provenance only and do not enter Sections 5.1–5.2.

## Reconciliation with the final 2026-11-07 package

The current algorithm results are published at `origin/codex/final-experiments-2026-11-05@65c331a:experiments/final_2026-11-07/`. All 24 R28 input files in the local planning worktree match the final package's case hashes byte for byte, including the selected five-scenario configuration. Ignoring line endings, comparison of `d030988` and `c29cdc5` shows no change to the original model's objective or constraints; the extensive-form change configures its stopping gap from the common global target.

The final C3 Full run has bounds `[17716.315484, 17886.326205]` and facility/battery/day/night costs `464.40 / 6269.28 / 7699.628571 / 3453.017634`, reproducing the values already used for the planning comparison. Its pairing with the no-DWC EF gives a 671.923312 USD/day incumbent saving and a certified absolute saving interval of 549.436865–841.934033 USD/day (2.9803%–4.5367%). The C3/C4 intervals overlap. The C4 exported incumbent also reproduces the final target-machine result. No local timing is used as performance evidence.
