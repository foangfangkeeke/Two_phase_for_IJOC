# Chapter 5 Data

Last updated: 2026-09-03

This ledger supersedes the August 30 results. The September logs under
`logs/` are the authoritative sources for computational performance, and
`results/chapter5_2/` is the authoritative source for the representative
planning solution. Values from the earlier three-scenario and four-charger
experiments must not be reused.

## Experiment Design

### Case Definitions

- Table A fixes five service-plan scenarios and five plug-in chargers per
  location while increasing network size: S-5, M-5, B-5, and L-5.
- Table B fixes the base network and five plug-in chargers per location while
  increasing the scenario count: B-5, B-10, B-20, and B-40.
- Table C fixes the base network and ten scenarios while varying the number of
  plug-in chargers per location from two to six.
- The representative planning solution uses B-5 with five plug-in chargers
  per location and a facility-cost scale of 3.

In manuscript case names, the letter denotes network size and the number
denotes scenario count. Capacity is stated separately rather than embedded in
the case name.

### Reporting Conventions

- A performance cell reports `total time in seconds / final gap in percent
  [status]`.
- `Opt.` denotes a Gurobi solve with status `OPTIMAL` under the configured MIP
  tolerance. `Conv.` denotes decomposition convergence within the outer
  tolerance.
- `TL`, `OOM`, `NI`, and `NR` denote time limit, out of memory, no finite
  incumbent, and not run, respectively.
- A gap is reported only when a finite incumbent exists. An infinite or
  sentinel upper bound is never treated as an incumbent.
- Reported elapsed time may exceed 7,200 seconds because a master iteration
  can finish after the time-limit check.
- When duplicate logs exist, the latest completed rerun is used unless noted
  otherwise.

### Algorithm Availability and Components

All six manuscript methods are implemented and have at least one September
log. All decomposition variants use RSP screening; RSP is held fixed and is
not an ablation component.

| Method | CLI algorithm | Integer L-shaped decomposition | Lifted reformulation | Monotonicity-strengthened cuts | Two-phase separation |
|---|---|:---:|:---:|:---:|:---:|
| EF | `solver` | $\times$ | $\times$ | $\times$ | $\times$ |
| ILS | `natural-ils` | $\checkmark$ | $\times$ | $\times$ | $\times$ |
| L-ILS | `lifted-ils` | $\checkmark$ | $\checkmark$ | $\times$ | $\times$ |
| L-ILS-M | `lifted-monotone` | $\checkmark$ | $\checkmark$ | $\checkmark$ | $\times$ |
| L2P-ILS | `lifted-two-phase` | $\checkmark$ | $\checkmark$ | $\times$ | $\checkmark$ |
| Full | `full` | $\checkmark$ | $\checkmark$ | $\checkmark$ | $\checkmark$ |

### Run-Chain Audit

| Method | Network-scale logs present | Observed stopping point |
|---|---|---|
| EF | S-5, M-5, B-5, L-5 | L-5 ends OOM without an incumbent. |
| ILS | S-5, M-5, B-5, L-5 | L-5 reaches the time limit without an incumbent. |
| L-ILS | S-5, M-5 | M-5 reaches the time limit without an incumbent; B-5 and L-5 are not run. |
| L-ILS-M | S-5, M-5 | M-5 reaches the time limit without an incumbent; B-5 and L-5 are not run. |
| L2P-ILS | S-5, M-5, B-5, L-5 | L-5 reaches the time limit with a finite incumbent. |
| Full | S-5, M-5, B-5, L-5 | L-5 reaches the time limit with a finite incumbent. |

The B-series contains EF, L2P-ILS, and Full logs for all four scenario counts,
and the capacity series contains L2P-ILS and Full logs for all five capacities.
The tracked experiment scripts predate the September reruns and do not record
the continuation-gap threshold used for this run set. The ledger therefore
records the observed run chain and terminal states without assigning an
unverified threshold.

The surviving `large_5_cap5` ILS log is incomplete because the terminal log
was deleted accidentally. Following the run record supplied by the author,
L-5 ILS is classified as `TL-NI`; its UB, LB, final gap, and exact elapsed time
remain unavailable until the terminal log is restored.

## Section 5.1: Experimental Setup and Base Instance

### Base Instance

The representative base instance is B-5, not the earlier B-3 instance. It
uses the 28-route Tower Transit network, five equally likely service-plan
scenarios, and five plug-in chargers at each existing plug-in location.

| Item | Value |
|---|---:|
| Routes | 28 |
| Physical stations | 493 |
| Directed links | 625 |
| Candidate SWC stations | 488 |
| Candidate DWC links | 625 |
| Plug-in charging locations | 5 |
| Chargers per plug-in location | 5 |
| Candidate wireless facilities | 1,113 |
| Battery capacity options | 31 (20, 30, ..., 320 kWh) |
| Service scenarios | 5 |
| Buses / trip chains per scenario | 264--322 |
| Scheduled trips per scenario | 2,568--3,138 |

The manuscript reports scenario-dependent bus and trip counts as
minimum--maximum ranges. The underlying bus counts are
`282 / 264 / 322 / 286 / 281`, and the trip counts are
`2,850 / 2,568 / 3,138 / 2,890 / 2,824` in the scenario order below. Trip
counts are obtained from the corresponding `tripStations.txt` generation
records.

### Five-Scenario Construction

| Scenario | Probability | Service-plan change |
|---:|---:|---|
| 0 | 0.2 | Base plan |
| 1 | 0.2 | Systemwide frequency -10% |
| 2 | 0.2 | Systemwide frequency +10% |
| 3 | 0.2 | Route 984 frequency +25% |
| 4 | 0.2 | Route 941 frequency -25% |

The physical network is fixed across scenarios; bus requirements and trip
chains are reconstructed for each service plan.

### Parameter Settings Relevant to the New Runs

| Parameter | Value | Source or role |
|---|---|---|
| Battery capacity cost | 0.148 USD/kWh/day | BloombergNEF; Wang et al. |
| State-of-charge bounds | 0.2 / 0.8 | Li et al. |
| Plug-in energy rate | 2.5 kWh per one-minute slot | Najafi et al. |
| SWC opportunity limit | 1.0 kWh/min times eligible dwell time | Wang et al. |
| DWC opportunity limit | 1.0 kWh/min times traversal time | Wang et al. |
| Charging efficiencies (PI/SWC/DWC) | 0.90 / 0.85 / 0.80 | Li et al.; Sun et al. |
| Base SWC facility cost | 2.16 USD/day | Wang et al. |
| Base DWC facility cost | 9 USD/km/day times link length, plus 0.90 USD/day | Wang et al. |
| Facility-cost scale | 3 | September experiment setting |
| Energy scale | 1 | September experiment setting |
| Electricity prices (day/night) | 0.1922 / 0.1233 USD/kWh | SingStat |

The effective facility costs in the September runs are three times the base
facility-cost values. Other physical and economic parameters remain those in
Appendix Table `tab:parameter-settings` unless subsequently revised.

### Computational Protocol

| Item | Setting |
|---|---|
| Implementation | C++ |
| Solver | Gurobi 12.0.3 |
| Processor | Intel Core i5-14600K |
| Memory | 32 GB |
| Thread budget | 12 |
| Time limit | 7,200 seconds per run |
| Solver/outer gap setting | `1e-4` (0.01%) |

## Section 5.2: Representative Planning Solution

### Source and Consistency Check

The controlled comparison uses:

- `results/chapter5_2/full_with_wpt/` for the Full-method WPT solution;
- `results/chapter5_2/solver_no_wpt/` for the no-WPT benchmark.

Both exports report `data_folder=tower_base_data`, `scenario_count=5`,
`cap=5`, `facility_cost_scale=3`, and `energy_scale=1`. The no-WPT benchmark
disables wireless facilities and reoptimizes battery and charging decisions.

### Objective Components

| Component (USD/day) | Without WPT | With WPT | With WPT minus without WPT |
|---|---:|---:|---:|
| Wireless facilities | 0.000000 | 405.000000 | 405.000000 |
| Scenario-dependent batteries | 4,187.216000 | 986.272000 | -3,200.944000 |
| Charging operations | 5,906.568956 | 6,783.866930 | 877.297974 |
| Total | 10,093.784956 | 8,175.138930 | -1,918.646026 |

The WPT solution reduces the reported objective by 19.008192%. Its facility
cost comprises 388.800000 USD/day for SWC and 16.200000 USD/day for DWC.
The reported components exclude scenario-dependent constants omitted from the
optimization formulation.

### WPT Solution Bounds and Facilities

| Metric | Value |
|---|---:|
| Best upper bound | 8,175.138930 |
| Best lower bound | 8,174.972777 |
| Final gap | 0.002032% |
| Selected SWC stations | 60 |
| Selected DWC links | 1 |

The raw `solution_summary.csv` stores `final_gap=0.000020` as a fraction;
the manuscript-facing percentage is calculated from the reported UB and LB.

### Battery Choices

The manuscript-facing means are computed over all 140 route--scenario battery
selections in each solution. Because the five scenarios are equally likely,
these values also equal the probability-weighted means across scenarios.

| Solution | Route--scenario selections | Mean capacity (kWh) | Maximum capacity (kWh) |
|---|---:|---:|---:|
| With WPT | 140 | 22.500000 | 40 |
| Without WPT | 140 | 84.714286 | 310 |

The scenario-level values below are retained as an audit of the aggregate
statistics.

| Solution | Scenario | Routes | Routes at 20 kWh | Mean capacity (kWh) | Maximum capacity (kWh) |
|---|---:|---:|---:|---:|---:|
| With WPT | 0 | 28 | 22 | 22.500000 | 40 |
| With WPT | 1 | 28 | 22 | 22.500000 | 40 |
| With WPT | 2 | 28 | 22 | 22.500000 | 40 |
| With WPT | 3 | 28 | 22 | 22.500000 | 40 |
| With WPT | 4 | 28 | 22 | 22.500000 | 40 |
| Without WPT | 0 | 28 | 8 | 85.000000 | 310 |
| Without WPT | 1 | 28 | 8 | 82.857143 | 310 |
| Without WPT | 2 | 28 | 8 | 85.714286 | 310 |
| Without WPT | 3 | 28 | 8 | 85.000000 | 310 |
| Without WPT | 4 | 28 | 8 | 85.000000 | 310 |

Sources are the two `route_battery_summary.csv` files. The WPT solution no
longer assigns 20 kWh to every route: six routes use larger batteries in each
scenario, with a maximum of 40 kWh.

### Export Limitation

`full_with_wpt/scenario_summary.csv` reports positive operational objective
values but zero fast-charging, SWC, DWC, night-charging, and total-charging
energy in every scenario. These fields are internally inconsistent and must
not be used for charging-energy shares, exposure counts, or charging-event
claims until the export is corrected or independently verified. The current
September artifacts support the objective, battery, and selected-facility
statistics above, but not the earlier facility-exposure table.

## Section 5.3: Algorithmic Performance and Scalability

### Table A: Network-Scale Instance Dimensions

All Table A cases use five scenarios and five plug-in chargers per location.
Trip counts are the numbers of rows in the corresponding `tripDepTimes.txt`
files. Trip and bus counts follow the case-specific scenario order printed in
the logs.

| Instance | Routes | Stations / links / PI locations | SWC / DWC candidates | Candidate total | Scheduled trips | Bus trip chains |
|---|---:|---:|---:|---:|---:|---:|
| S-5 | 3 | 49 / 53 / 2 | 47 / 53 | 100 | 230 / 206 / 252 / 244 / 224 | 28 / 26 / 29 / 28 / 25 |
| M-5 | 11 | TODO | TODO | 638 | 432 / 391 / 479 / 439 / 424 | 41 / 40 / 46 / 42 / 40 |
| B-5 | 28 | 493 / 625 / 5 | 488 / 625 | 1,113 | 2,850 / 2,568 / 3,138 / 2,872 / 2,824 | 282 / 264 / 322 / 286 / 281 |
| L-5 | 59 | 1,459 / 1,822 / 8 | 1,451 / 1,822 | 3,273 | 6,291 / 5,669 / 6,923 / 6,316 / 6,267 | 762 / 705 / 909 / 764 / 761 |

The M-5 candidate total is verified from the lifted master size:
`2,343 - 5 x 11 x 31 = 638`. The current logs do not split this total into
stations and links or report the corresponding physical-network counts.
Those values must be recovered from the regenerated medium-instance record.

### Table A: Network-Scale Algorithm Comparison

Each algorithm entry is `total time (s) / final gap (%) [status]`.

| Instance | EF | ILS | L-ILS | L-ILS-M | L2P-ILS | Full |
|---|---:|---:|---:|---:|---:|---:|
| S-5 | 2.707 / 0.0097 [Opt.] | 1.555 / 0.000000 [Conv.] | 0.280 / 0.000000 [Conv.] | 0.305 / 0.000000 [Conv.] | 0.526 / 0.000000 [Conv.] | 0.563 / 0.000000 [Conv.] |
| M-5 | 7,200.758 / 0.0278 [TL] | 7,200.029 / 0.662437 [TL] | 7,207.227 / -- [TL-NI] | 7,234.061 / -- [TL-NI] | 5.613 / 0.003085 [Conv.] | 5.288 / 0.003085 [Conv.] |
| B-5 | 5,469.130 / 1.1639 [OOM] | 8,359.261 / 0.858418 [TL] | NR | NR | 135.543 / 0.002032 [Conv.] | 133.674 / 0.002032 [Conv.] |
| L-5 | 656.130 / -- [OOM-NI] | >=7,200 / -- [TL-NI] | NR | NR | 7,230.550 / 0.152628 [TL] | 7,510.302 / 0.015066 [TL] |

For S-5 EF, the latest rerun is used. Gurobi reports status `OPTIMAL` with a
0.0097% terminal gap under the configured 0.01% MIP tolerance. B-5 EF ends
with error code 10001 after retaining a finite incumbent; L-5 EF ends with the
same error code but no incumbent. The latter is reported as `OOM-NI`.

Important terminal values retained for interpretation:

- M-5 ILS: UB 1,288.788312, LB 1,280.250903, gap 0.662437%.
- M-5 L-ILS and L-ILS-M: no finite incumbent; final recorded LB 133.168000.
- B-5 EF: UB 8,221.379570, LB 8,125.691828, gap 1.1639%.
- B-5 ILS: UB 8,202.466852, LB 8,132.055374, gap 0.858418%.
- L-5 L2P-ILS: UB 28,896.500012, LB 28,852.395835, gap 0.152628%.
- L-5 Full: UB 28,859.077042, LB 28,854.729135, gap 0.015066%.

### Table A: Phase-Time Support

| Instance | Method | Phase 1 (s) | Phase 2 (s) | Total (s) |
|---|---|---:|---:|---:|
| S-5 | L2P-ILS | 0.297 | 0.229 | 0.526 |
| S-5 | Full | 0.327 | 0.236 | 0.563 |
| M-5 | L2P-ILS | 4.433 | 1.180 | 5.613 |
| M-5 | Full | 4.149 | 1.139 | 5.288 |
| B-5 | L2P-ILS | 83.716 | 51.827 | 135.543 |
| B-5 | Full | 85.312 | 48.362 | 133.674 |
| L-5 | L2P-ILS | 2,431.407 | 4,799.143 | 7,230.550 |
| L-5 | Full | 2,338.616 | 5,171.686 | 7,510.302 |

### Table B: Scenario-Count Algorithm Comparison

The physical network and five-charger capacity are fixed. The scenario sets
are nested, and each scenario has probability `1/k` in a k-scenario case.

| Instance | Scenarios | EF | L2P-ILS | Full |
|---|---:|---:|---:|---:|
| B-5 | 5 | 5,469.130 / 1.1639 [OOM] | 135.543 / 0.002032 [Conv.] | 133.674 / 0.002032 [Conv.] |
| B-10 | 10 | 7,202.652 / 2.6843 [TL] | 574.103 / 0.002044 [Conv.] | 232.253 / 0.002044 [Conv.] |
| B-20 | 20 | 985.030 / -- [OOM-NI] | 469.096 / 0.009122 [Conv.] | 339.781 / 0.005732 [Conv.] |
| B-40 | 40 | 1,378.010 / -- [OOM-NI] | 10,107.386 / 0.305541 [TL] | 4,275.907 / 0.008980 [Conv.] |

### Table B: Phase-Time Support

| Instance | Method | Phase 1 (s) | Phase 2 (s) | Total (s) |
|---|---|---:|---:|---:|
| B-5 | L2P-ILS | 83.716 | 51.827 | 135.543 |
| B-5 | Full | 85.312 | 48.362 | 133.674 |
| B-10 | L2P-ILS | 108.002 | 466.101 | 574.103 |
| B-10 | Full | 112.536 | 119.717 | 232.253 |
| B-20 | L2P-ILS | 224.357 | 244.739 | 469.096 |
| B-20 | Full | 222.099 | 117.682 | 339.781 |
| B-40 | L2P-ILS | 711.627 | 9,395.759 | 10,107.386 |
| B-40 | Full | 730.102 | 3,545.805 | 4,275.907 |

## Section 5.4: Capacity Sensitivity

### Table C: Capacity Comparison

The experiment fixes B-10 and compares L2P-ILS with Full as the number of
plug-in chargers per location varies.

| Chargers | L2P-ILS | Full |
|---:|---:|---:|
| 2 | 7,244.384 / -- [TL-NI] | 12,166.001 / -- [TL-NI] |
| 3 | 1,527.893 / 0.004080 [Conv.] | 321.357 / 0.002958 [Conv.] |
| 4 | 303.083 / 0.008554 [Conv.] | 222.819 / 0.009541 [Conv.] |
| 5 | 574.103 / 0.002044 [Conv.] | 232.253 / 0.002044 [Conv.] |
| 6 | 782.882 / 0.001056 [Conv.] | 210.261 / 0.007782 [Conv.] |

Both capacity-2 runs reach the time limit without a finite exact-feasible
incumbent. The long reported Full time results from completing a master
iteration after the nominal 7,200-second check.

### Table C: Phase 1 to Phase 2 Transition

The Phase 1 relaxation is unchanged across capacities. Let
`g0 = (UB0 - LB^R) / max(1, abs(UB0))`, where `UB0` is the first finite
exact-feasible Phase 2 objective. The transition bound and first incumbent are
identical across L2P-ILS and Full for capacities 3--6.

| Chargers | Phase 1 LB | First Phase 2 UB | Initial gap (%) |
|---:|---:|---:|---:|
| 2 | 8,158.993391 | -- | -- |
| 3 | 8,158.993391 | 8,199.668562 | 0.496059 |
| 4 | 8,158.993391 | 8,190.618101 | 0.386109 |
| 5 | 8,158.993391 | 8,188.301086 | 0.357922 |
| 6 | 8,158.993391 | 8,187.898441 | 0.353022 |

### Table C: Phase-Time Support

| Chargers | Method | Phase 1 (s) | Phase 2 (s) | Total (s) |
|---:|---|---:|---:|---:|
| 2 | L2P-ILS | 109.980 | 7,134.404 | 7,244.384 |
| 2 | Full | 108.904 | 12,057.097 | 12,166.001 |
| 3 | L2P-ILS | 125.255 | 1,402.638 | 1,527.893 |
| 3 | Full | 112.994 | 208.363 | 321.357 |
| 4 | L2P-ILS | 126.459 | 176.624 | 303.083 |
| 4 | Full | 110.014 | 112.805 | 222.819 |
| 5 | L2P-ILS | 108.002 | 466.101 | 574.103 |
| 5 | Full | 112.536 | 119.717 | 232.253 |
| 6 | L2P-ILS | 130.034 | 652.848 | 782.882 |
| 6 | Full | 114.292 | 95.969 | 210.261 |

### Table C: Phase 2 Resource-Monotonicity Cut Comparison

L2P-ILS and Full use the same Phase 1 architecture; Full adds the
resource-monotonicity-strengthened scenario cuts in Phase 2. The comparison
therefore isolates their incremental effect within Phase 2. Iteration counts
include the iteration indexed zero.

| Chargers | L2P-ILS iterations | Full iterations | L2P-ILS Phase 2 (s) | Full Phase 2 (s) | Time ratio |
|---:|---:|---:|---:|---:|---:|
| 2 | 122 | 104 | 7,134.404 | 12,057.097 | -- |
| 3 | 14 | 5 | 1,402.638 | 208.363 | 6.73 |
| 4 | 3 | 3 | 176.624 | 112.805 | 1.57 |
| 5 | 7 | 3 | 466.101 | 119.717 | 3.89 |
| 6 | 10 | 2 | 652.848 | 95.969 | 6.80 |

The time ratio is L2P-ILS Phase 2 time divided by Full Phase 2 time. It is
omitted at capacity 2 because neither method obtains a finite exact-feasible
incumbent. For these two incomplete runs, Phase 2 time is total time minus
Phase 1 time.

## Source Selection

- Table A sources: latest relevant logs in `logs/small_5_cap5/`,
  `logs/medium_5_cap5/`, `logs/tower_base_5_cap5/`, and
  `logs/large_5_cap5/`.
- Table B sources: logs in `logs/tower_base_5_cap5/`,
  `logs/tower_base_10_cap5/`, `logs/tower_base_20_cap5/`, and
  `logs/tower_base_40_cap5/`.
- Table C sources: L2P-ILS and Full logs in
  `logs/tower_base_10_cap2/` through `logs/tower_base_10_cap6/`.
- For duplicate S-5 runs, use
  `small_5_cap5_solver_wireless_2026-09-03_11-11-38.log` and
  `small_5_cap5_full_wireless_reform_2026-09-03_10-57-00.log`.
- For duplicate B-5 EF runs, use
  `tower_base_5_cap5_solver_wireless_2026-09-02_03-35-33.log`.

## Remaining Verification Items

- Recover the deleted terminal log for L-5 ILS and replace the provisional
  `>=7,200 / -- [TL-NI]` entry with its exact elapsed time and final LB.
- Recover the regenerated M-5 physical-station, directed-link, SWC-candidate,
  DWC-candidate, and plug-in-location counts.
- Correct or independently verify the zero charging-energy fields in
  `results/chapter5_2/full_with_wpt/scenario_summary.csv` before reporting
  charging-energy composition, facility exposure, or charging-event counts.
- Record the actual September continuation-gap threshold or archive the runner
  used for these experiments if the continuation rule is discussed in the
  manuscript.
