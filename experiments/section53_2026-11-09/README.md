# Section 5.3 planning evidence

This package contains the selected costed optimization and fixed-plan operating logs for the R28-N5-F50-C4 analysis, plus the C3 capacity comparison. It includes the 50%, 75%, 125% and 150% DWC construction-cost and battery-price variants. The 100% case is the unchanged C4 base case. `manifest.json` gives the exact path, SHA256, LB, UB, gap and algorithm for every selected record. The two original C3/C4 DWC Full logs remain in `../final_2026-11-07/logs` and are referenced by the manifest without duplicating their bytes.

## Result status

All 16 selected optimization records reached the configured 1% global gap with an incumbent. All 16 fixed-plan audits have `COMPLETE_OPTIMAL_RECOURSE`, five optimal scenario records, 140 route records and 25 station records. The six no-DWC EF runs exited with code 0 and did not trigger the external memory guard. `OPTIMAL` here means the configured gap was met; use the bounds rather than treating any reported objective as the exact optimum.

For each row below, the certified interval for the optimum-cost saving from DWC is `[LB_noDWC - UB_DWC, UB_noDWC - LB_DWC]`. Cost units follow the model. The route-battery column is the scenario-probability-weighted mean of the bus-count-weighted route capacities in the **DWC incumbent**. These capacities and selected-link counts describe feasible plans, not certified optimal design choices.

| Varied cost | Factor | Certified DWC saving interval | Selected DWC links | DWC plan battery (kWh) |
| --- | ---: | ---: | ---: | ---: |
| DWC construction | 50% | 861.835–1064.830 | 39 | 133.35 |
| DWC construction | 75% | 719.448–922.462 | 34 | 135.02 |
| DWC construction | 100% | 600.468–805.152 | 26 | 141.32 |
| DWC construction | 125% | 418.316–715.687 | 25 | 145.34 |
| DWC construction | 150% | 367.000–627.659 | 25 | 144.06 |
| Battery price | 50% | −72.222–155.771 | 5 | 178.89 |
| Battery price | 75% | 159.056–356.515 | 17 | 150.89 |
| Battery price | 100% | 600.468–805.152 | 26 | 141.32 |
| Battery price | 125% | 1035.954–1173.030 | 34 | 135.02 |
| Battery price | 150% | 1491.187–1651.616 | 39 | 133.35 |

At 50% battery price the interval crosses zero, so these bounds do **not** certify a positive saving there. Adjacent DWC construction-cost intervals overlap; they do not certify a strict change at every step. The 100% row is repeated in the table because it is the shared baseline.

## Files and provenance

- `plans/*.plan` fixes the selected DWC links and 140 scenario-route battery choices. Its first line is the SHA256 of the costed optimization log.
- `logs/optimization/*.log` contains the 14 new costed logs. The two original C3/C4 DWC Full logs are linked from `manifest.json` in the existing final-experiments package.
- `logs/plan_audit/*.log` contains the 16 fixed-design operating logs. Each `Plan audit source.sha256` matches the corresponding plan's source-log hash. Audit totals can differ slightly from the original optimization UB because the operating assignments are solved again; `manifest.json` retains each difference. Use the original optimization LB/UB for saving intervals.
- `ef_runs/*` contains six EF `launch.json`, `exit.json`, `resources.jsonl`, stdout and stderr records. Its original stdout pointer names the local raw log; use the corresponding `manifest.json` path in this package when reading remotely.
- The DWC Full executable is `../final_2026-11-07/versions/c29cdc5/wireless.exe`, SHA256 `a14be609b82aceb66763501b2eacad779592db567dc6382e7656bbbfc0b05fc1`, with its source archive beside it. EF and plan-audit use `versions/ef_audit/wireless.exe`, SHA256 `9c6d95ad9f98935c09d8e4f949881f96d3ac70731d2d00d4cf2428fc81df1561`, and `versions/ef_audit/source_snapshot.zip`, SHA256 `ec557f5d781b85cb406568829c351204fc189f3b9fa3f5afc80a8a441dd45211`. The current source changes add EF incumbent and plan-audit logging; the optimization model constraints and objective were not changed. Keep the executable identities with their records.
- The eight variant datasets are under `data/R28_N5_{dwc,battery}{0p5,0p75,1p25,1p5}_data`. Each `cost_case_manifest.json` records the factor and proves that only `globalCfg.txt` changed. The DWC factor changes the dynamic charger and inverter construction-cost entries together. The battery factor changes only the battery unit-price entry. The C4 no-DWC EF baseline is reused for all DWC construction-cost variants because those DWC cost entries are absent from the no-DWC objective. Battery-price variants each have a separate no-DWC EF run.

## Extracting figures and tables

Use `manifest.json` to select cases by `id`, verify the SHA256 of each source and audit log, and read `lb`/`ub` from the optimization record. Pair DWC and no-DWC on the same dataset and capacity; for DWC construction-cost variants pair each DWC record with `C4_noDWC_EF`. The JSON objects after `Plan audit route:`, `Plan audit station:` and `Plan audit route_station:` contain charging opportunities, selected energy and station allocation. Join route records on `(scenario, route_index)` after checking the route names. The route battery average is `sum_s probability_s × [sum_r buses_sr × battery_kwh_sr / sum_r buses_sr]`. The five-station expected Plug-in energy is `sum_s probability_s × station_plug_grid_kwh_s` for each station.

`dwc_available_minutes` and `plug_available_minutes` are opportunity windows; `dwc_grid_kwh` and `plug_grid_kwh` are choices made by the fixed-plan operating solve. `equivalent_plug_minutes` is energy divided by charging rate. No peak simultaneous occupancy is certified by these logs. Full field definitions and a JSON parsing example are in [`docs/section53_evidence_plan.md`](../../docs/section53_evidence_plan.md). The fixed-plan audit is a separate calculation and never replaces the original optimization bounds or runtime.
