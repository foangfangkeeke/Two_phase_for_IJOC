# Section 5.3 evidence and logging procedure

This procedure creates a **separate plan-audit log** for each costed incumbent. It fixes that incumbent's DWC links and scenario-specific route batteries, then solves the five operating subproblems again. The optimization log remains the source of LB, UB, gap and run time. A plan-audit log supplies the charging quantities and opportunities needed for Section 5.3; its operating assignment can differ from the original run when several assignments have the same objective.

## Evidence checklist

| Section | Optimization records | Plan-audit records | Derived quantities |
| --- | --- | --- | --- |
| 5.3.1 C4 matched plans | DWC Full and **cost-optimizing** no-DWC EF, same `R28_N5`, N5, F50, C4, battery step 50 and model inputs; retain both LB/UB and incumbent cost distribution | For each plan: selected links, route battery and bus count in each scenario, route DWC/Plug-in available minutes, route DWC/Plug-in/night grid kWh, five stations' Plug-in kWh and equivalent plug minutes | Cost saving interval; route battery changes across 140 route-scenario cells; direct DWC use versus changes through shared Plug-in resources; weighted station load |
| 5.3.2 capacity | Matched DWC Full / no-DWC EF at C3 and C4; C5 only if needed | Same plan-audit records for both capacities and both arms | Certified saving interval `[LB_noDWC - UB_DWC, UB_noDWC - LB_DWC]`, station load, battery changes. Do not claim monotonic saving when intervals overlap. |
| 5.3.3 cost | At each parameter level, DWC Full and no-DWC EF LB/UB and costed incumbents. Change only one input cost at a time. | Same plan-audit records for each incumbent | Saving interval, selected link count, scenario-probability-weighted bus-count-weighted battery capacity. |

The historical `no-dwc-feasibility` logs fix all batteries to their maximum and use a zero objective. They cannot serve as the cost baseline. Cost-optimizing no-DWC EF records for C3 and C4 are retained under `experiments/section53_2026-11-09/ef_runs/no_dwc_ef_C3_2026-11-09_08-01-32_UTC` and `experiments/section53_2026-11-09/ef_runs/no_dwc_ef_C4_2026-11-09_07-56-34_UTC`; the package manifest gives each raw solver-log path. `OPTIMAL` in these logs means meeting the configured 1% global gap, not proof of exact optimality; retain both bounds.

## Inputs and run order

1. Verify the published C3/C4 input bytes before reusing their optimization logs:

   ```powershell
   C:/ProgramData/anaconda3/python.exe scripts/analysis/verify_published_case_inputs.py R28-N5-F50-C3
   C:/ProgramData/anaconda3/python.exe scripts/analysis/verify_published_case_inputs.py R28-N5-F50-C4
   ```

2. Retain the existing DWC Full logs at `experiments/final_2026-11-07/logs/R28-N5-F50-C3_full.log` and `...C4_full.log`, together with the completed no-DWC **cost baselines solved with `--alg ef --wireless-option 0`**. Algorithm identity need not match to compare valid bounds for the same mathematical model. The EF executable logs its final facility/battery design only when an incumbent exists; the added logging does not change model constraints or objective. If a new capacity case is added, use the same case data, costs, limits and plug capacities. Example C4 options:

   ```powershell
   C:/ProgramData/anaconda3/python.exe scripts/analysis/run_guarded_no_dwc_ef.py 4
   ```

   Use `3` for C3. The wrapper runs `--alg ef --wireless-option 0` with the matching case options and the established external memory guard. Preserve its raw solver log and run directory (`launch.json`, `resources.jsonl`, `exit.json`, any external memory event), including the exact binary/source and input hashes, wall time, terminal status, LB, UB and gap. A C4 no-DWC Full run made before switching to EF is available locally as a separate, same-archived-executable cross-check; it is not an EF result. A positive saving is certified only when `LB_noDWC > UB_DWC`. A paired plan comparison also needs a feasible no-DWC incumbent.

3. For each costed Full log, write a plan file and execute the fixed-plan audit with **exactly the same case options and input bytes**. The audit's new executable is deliberately versioned separately from the optimization executable. Example C4 with DWC:

   ```powershell
   C:/ProgramData/anaconda3/python.exe scripts/analysis/prepare_fixed_plan.py experiments/final_2026-11-07/logs/R28-N5-F50-C4_full.log results/section53/C4_DWC.plan
   .\out\wireless.exe --alg plan-audit --data R28_N5 --scenario-count 5 --scenario-start 0 --cap 4 --battery-step 50 --dwc-top-n 50 --energy-scale 1.0 --facility-cost-scale 1.0 --wireless-option 1 --analysis-plan results/section53/C4_DWC.plan
   ```

   For no DWC, use its cost-optimization EF log as input to `prepare_fixed_plan.py` and pass `--wireless-option 0` to `plan-audit`. The extractor selects the lowest costed incumbent attached to a `Search master` or `Search incumbent` design; it fails if that association is absent. `plan-audit` validates the plan's data name, N, F, C, battery step, wireless flag, facilities and all route batteries. Its own output is a separate diagnostic result; it does not replace the original optimization UB or execution time. Compare `Plan audit total.difference` against the source UB before interpreting the charging plan. Preserve any material difference and the scenario statuses instead of silently reconciling them.

## Log fields and extraction

Each `Plan audit ...: { ... }` line contains a JSON object. Read only lines with these prefixes and parse the text after the first `: ` as JSON. Require `Plan audit status: COMPLETE_OPTIMAL_RECOURSE` for the primary plan charts.

```python
import json
from pathlib import Path

labels = {"Plan audit source", "Plan audit facility", "Plan audit scenario",
          "Plan audit route", "Plan audit station", "Plan audit route_station",
          "Plan audit total"}
records = {label: [] for label in labels}
for line in Path("path/to/plan-audit.log").read_text(encoding="utf-8").splitlines():
    label, separator, payload = line.partition(": ")
    if separator and label in records:
        records[label].append(json.loads(payload))
```

| Prefix | Fields used |
| --- | --- |
| `Plan audit source` / `facility` | Optimization log SHA256 and UB, number and names of selected DWC links, link endpoints, battery and DWC construction prices, Plug-in rate and efficiency. |
| `Plan audit scenario` | Scenario index, folder, probability, fixed-plan operating status, objective and bound. |
| `Plan audit route` | Scenario, route name/index, number of buses, battery kWh, DWC and Plug-in **available minutes**, DWC/Plug-in/night **grid-input kWh**. |
| `Plan audit station` | Scenario, station, installed plugs, summed available bus minutes, actual Plug-in grid-input kWh, equivalent plug minutes (`grid kWh / charging rate`) and energy delivered to batteries (`grid kWh × efficiency`). |
| `Plan audit route_station` | Scenario, route and station: available minutes, Plug-in grid-input kWh and equivalent minutes. This is the compact route-to-station allocation record; no per-minute occupancy is exported. |
| `Plan audit total` | Facility, battery, daytime and nighttime costs; recomputed total, source UB and difference. |

For 5.3.1, join DWC and no-DWC `route` rows on `(scenario, route_index)` after checking route names. Count capacity reductions across all 140 cells. Mark a route as directly using DWC when `dwc_grid_kwh >` a stated numerical tolerance; `dwc_available_minutes > 0` means an opportunity, not use. Aggregate station kWh with scenario probabilities for expected daily values. Keep DWC, Plug-in and night kWh as distinct quantities. The model's day-energy cost combines DWC and Plug-in, so it cannot by itself recover either charging amount.

For 5.3.2 and 5.3.3, compute the saving interval from the **optimization** bounds, not from plan-audit totals. Bus-count-weighted battery capacity for each scenario is `sum(route.buses × route.battery_kwh) / sum(route.buses)`; combine scenarios using their logged probabilities. Report the selected DWC link count from `Plan audit source`.

`plug_available_minutes` and `dwc_available_minutes` are sums over bus trips from the timetable. They are physical opportunity windows, **not** simultaneously feasible charging time under shared plug capacity and not actual energy. `equivalent_plug_minutes` is energy divided by charging rate, not the raw binary occupancy sum. The audit intentionally does not export occupancy or a peak-concurrency curve: those binaries have no objective cost and may contain unused occupancy. A peak-concurrency figure would require a separately specified secondary scheduling rule with the primary cost fixed.

## Cost sensitivity inputs

Do not use `--facility-cost-scale` for DWC-only sensitivity: it scales all three charger unit costs and the inverter. Make a separately named, hash-recorded input copy for each parameter level. Keep the dataset name beginning with `R28_` so Full writes the `Search master` design lines needed by `prepare_fixed_plan.py`. For DWC construction cost, change only the dynamic-charger unit cost and inverter-cost cells in `globalCfg.txt` by the same stated factor; that factor is the single varied parameter, and the no-DWC economic baseline can be reused when every other input and option matches. For battery unit cost, change only the battery price cell in `globalCfg.txt`, and re-optimize both arms. Save a one-parameter-change manifest for every copy, the exact source/executable hashes and the raw logs. C5 and peak occupancy are optional follow-ups; C2's current with-DWC gap is too large for detailed plan interpretation.

Generate a variant without touching the base dataset:

```powershell
C:/ProgramData/anaconda3/python.exe scripts/analysis/make_cost_case.py dwc 0.75
C:/ProgramData/anaconda3/python.exe scripts/analysis/make_cost_case.py dwc 1.25
C:/ProgramData/anaconda3/python.exe scripts/analysis/make_cost_case.py dwc 0.5
C:/ProgramData/anaconda3/python.exe scripts/analysis/make_cost_case.py dwc 1.5
```

The four resulting names are `R28_N5_dwc0p5`, `R28_N5_dwc0p75`, `R28_N5_dwc1p25` and `R28_N5_dwc1p5`; each directory contains `cost_case_manifest.json` proving that only `globalCfg.txt` changed and that its single DWC construction-cost factor affects the dynamic unit cost and inverter cells together. Run DWC Full on each name using the archived optimization executable, original N5/F50/C4 settings and `--wireless-option 1`. The C4 no-DWC EF bounds are reusable for these variants because the no-DWC model contains no DWC facility cost term. Create a fixed-plan audit from each costed Full log using that variant's exact dataset name. Battery-cost variants `R28_N5_battery0p5`, `R28_N5_battery0p75`, `R28_N5_battery1p25` and `R28_N5_battery1p5` change only the battery price cell and require a new no-DWC EF run at each level. The four factors are 50%, 75%, 125% and 150% of the C4 baseline.

## Completed local evidence and provenance

The selected files are collected under `experiments/section53_2026-11-09`; its `manifest.json` pairs every fixed plan with its optimization and plan-audit logs by SHA256. The local `results/section53` directory contains 16 fixed-plan files: the C3 and C4 DWC/no-DWC pairs, four DWC construction-cost variants, and four battery-cost DWC/no-DWC pairs. The six no-DWC EF run directories contain `launch.json`, `exit.json`, `resources.jsonl`, and a `stdout.log` pointer to each raw solver log. All six EF runs exited with code 0 without triggering the external memory guard. The 16 selected `plan-audit` logs each have `COMPLETE_OPTIMAL_RECOURSE`, five optimal scenario records, 140 route records, and 25 station records. Preserve each audit's `Plan audit total.difference` rather than replacing the optimization UB with its recomputed total.

To pair files without relying on timestamps, read `source_log_sha256` in each `experiments/section53_2026-11-09/plans/*.plan`. Match it to the SHA256 of the optimization raw log and to `Plan audit source.sha256` in the audit log. The C3/C4 base DWC optimization logs remain under `experiments/final_2026-11-07/logs`; all other selected logs are in the Section 5.3 package. The DWC Full runs use `experiments/final_2026-11-07/versions/c29cdc5/wireless.exe` (SHA256 `a14be609b82aceb66763501b2eacad779592db567dc6382e7656bbbfc0b05fc1`). The no-DWC EF and plan audits use `experiments/section53_2026-11-09/versions/ef_audit/wireless.exe` (SHA256 `9c6d95ad9f98935c09d8e4f949881f96d3ac70731d2d00d4cf2428fc81df1561`). Keep these executable identities with the logs; the plan audits are separate fixed-design calculations.

The EF/audit source, analysis scripts and build configuration are saved in `experiments/section53_2026-11-09/versions/ef_audit/source_snapshot.zip` (SHA256 `ec557f5d781b85cb406568829c351204fc189f3b9fa3f5afc80a8a441dd45211`). All 35 archive members were checked byte-for-byte against the working files. The snapshot records the exact source state used for the EF/audit executable; do not substitute an older repository HEAD for it.
