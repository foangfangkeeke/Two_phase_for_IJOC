# R28-S5-F50-C4 extracted results

Run `C:/ProgramData/anaconda3/python.exe scripts/analysis/export_section53.py` from the repository root to regenerate these files. The script reads the immutable optimization logs, fixed-plan files and operating logs listed in `../manifest.json`. It launches no solver. `export_manifest.json` records the source manifest hash, script hash, row counts and SHA256 of each CSV.

## Bound and cost reconciliation

The 14 C4 runs consist of five DWC Full and five no-DWC EF battery-price levels, plus four additional DWC Full construction-cost levels. The 100% level is the shared base pair. Each fixed plan was matched against the **costed design that produced the original final UB**, including Full repair incumbents; the last visited master was never substituted. The original optimization status and LB remain unchanged.

Each plan-audit log fixes that design and solves the five operating scenarios using the same scenario model. All five scenario solves report `OPTIMAL`. The printed scenario objectives plus construction cost reconcile with the audit total. When that feasible total is lower than the original UB by more than 0.001 model cost units, `ub_final` uses the lower audit total and its four-part cost breakdown. `ub_original` and the raw logs are always retained. For Full runs that keep the original UB, the four cost components come from the original costed incumbent. For EF runs whose audit total equals the original UB within 0.001, the breakdown comes from the matching audit.

One audit is higher: `C4_battery0p5_DWC` has audit total **14450.083793**, compared with original UB **14449.937509**. The audit fixed-design lower bound is **14449.691243**, so the original UB falls inside its solve interval. The original Full UB and its cost breakdown are retained. A higher separately solved feasible objective does not alone show a wrong design extraction. `ub_reconciliation.csv` records every difference and source decision.

`manifest.json` preserves the original UB for every raw optimization record; `run_summary.csv` and both sensitivity CSVs give the reconciled `ub_final`. The certified saving interval is `[LB_noDWC − UB_final_DWC, UB_final_noDWC − LB_DWC]`. `incumbent_cost_difference` is `UB_final_noDWC − UB_final_DWC`; it is a feasible-solution difference, not the certified saving lower bound. All cost amounts use the model's cost units. The original `OPTIMAL` status means the configured 1% global gap was met, not exact global optimality.

## CSV files

| File | Rows | Contents |
| --- | ---: | --- |
| `run_summary.csv` | 14 | Original LB/UB/gap and status, reconciled UB/gap, UB source, four cost components, selected links and vehicle-weighted battery capacity. |
| `ub_reconciliation.csv` | 14 | Original UB, audit total and fixed-design lower bound, difference, retained or adopted UB. |
| `final_design_facilities.csv` | 244 | Selected DWC link names and endpoints for the C4 runs; no-DWC runs have zero selected links in `run_summary.csv`. |
| `final_design_batteries.csv` | 1960 | Five scenarios × 28 routes × 14 runs: route battery, bus count and scenario probability. |
| `baseline_routes.csv` | 280 | C4 DWC Full and no-DWC EF fixed-plan route charging opportunities and Plug-in, DWC and overnight **grid-input kWh**. |
| `baseline_stations.csv` | 50 | Five Plug-in stations in each of five scenarios for both baseline arms: grid-input kWh, delivered battery kWh and equivalent charging minutes. |
| `baseline_scenarios.csv` | 10 | The three charging totals by scenario and each fixed-design recourse objective/bound. |
| `baseline_route_changes.csv` | 140 | Matched route battery changes and actual DWC use across the baseline pair. |
| `baseline_station_changes.csv` | 5 | Scenario-probability-weighted Plug-in kWh change at each existing station. |
| `baseline_summary.csv` | 1 | Matched battery and station counts, expected Plug-in change rate, weighted battery means and certified cost-saving interval. |
| `battery_cost_sensitivity.csv` | 5 | Both arms' bounds, certified interval, incumbent cost difference, selected links, battery means and four cost components. |
| `dwc_cost_sensitivity.csv` | 5 | The same fields, reusing the base no-DWC EF result at every DWC construction-cost level. |

## Calculation definitions

For a scenario, vehicle-weighted mean battery capacity is `sum_route(buses × battery_kwh) / sum_route(buses)`. The reported mean across scenarios weights those scenario means by their logged probabilities. A route–scenario cell is counted as directly using DWC when its DWC grid-input charge exceeds **0.001 kWh**. Battery capacity changes are exact multiples of 50 kWh. A station is classified as unchanged when the absolute difference in expected Plug-in grid-input energy is at most **0.001 kWh**.

Expected energy is `sum_s(probability_s × grid_input_kwh_s)`. The Plug-in change rate is `(expected Plug-in kWh with DWC − expected Plug-in kWh without DWC) / expected Plug-in kWh without DWC`. The baseline route and station CSVs show the underlying rows. The equivalent Plug-in minutes in `baseline_stations.csv` are energy divided by charging rate; they are **not** observed stall occupancy or peak concurrent use. The raw logs contain no certified peak-concurrency series.

The baseline extraction gives 67 route–scenario battery decreases, 73 unchanged and none increased; 64 of the decreases also directly use DWC. Expected Plug-in grid-input energy rises by 10.206962% in the fixed-plan comparison. Four stations increase and one decreases. These are outcomes of the logged fixed-design operating solves; a different optimal operating assignment can redistribute charging while retaining essentially the same cost.
