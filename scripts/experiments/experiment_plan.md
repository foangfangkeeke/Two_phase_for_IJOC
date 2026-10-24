# Experiment Plan

## Fixed settings

- Solver: COPT
- Time limit: 3600 seconds per run
- Battery step: 50 kWh
- Energy scale: 1.0
- Charging window: 10 minutes from the source data
- Scenario start: 0
- RSP allocation for Lifted Two-Phase and Full: 3 workers x 4 threads

The instance identifier is `R{routes}-N{scenarios}-F{candidate facilities}-C{capacity}`.
Only these four scale dimensions appear in result directory names.

## Pending instance groups

| Group | Instances | Algorithms | Runs |
| --- | ---: | --- | ---: |
| Main | 9 | EF, Natural ILS without RSP, Natural ILS, Lifted ILS, Lifted Two-Phase, Full | 54 |
| Scenario | 4 | Lifted Two-Phase, Full | 8 |
| Facility | 2 | Lifted Two-Phase, Full | 4 |
| Capacity | 3 | Lifted Two-Phase, Full | 6 |
| Total | 18 | | 72 |

`R28-N5-F40-C3 / Full` is already complete and is stored under
`results/experiments/R28-N5-F40-C3`. It is not included in the pending count.

## Algorithm mapping

| Plan name | Command settings |
| --- | --- |
| `ef` | `--alg ef --rsp 0` |
| `natural-ils-no-rsp` | `--alg natural-ils --rsp 0` |
| `natural-ils` | `--alg natural-ils --rsp 1` |
| `lifted-ils` | `--alg lifted-ils --rsp 1` |
| `lifted-two-phase` | `--alg lifted-two-phase --rsp 1` |
| `full` | `--alg full --rsp 1` |

## Execution order

1. Run the Main group one algorithm at a time, in the manifest order.
2. For each algorithm, complete the R3 cases before R12, and R12 before R28.
3. If an algorithm cannot produce a usable result on a smaller route network,
   stop that algorithm before attempting the larger route network.
4. Run Scenario, Facility, and Capacity groups only after the Main Full and
   Lifted Two-Phase runs are complete.
5. Run one group and one algorithm per command. Do not launch the whole plan at once.

Examples:

```powershell
.\scripts\experiments\run_plan_group.ps1 -Group main -Algorithm full
.\scripts\experiments\run_plan_group.ps1 -Group scenario -Algorithm lifted-two-phase
.\scripts\experiments\run_plan_group.ps1 -Group capacity -Algorithm full
```

Use `-InstanceId R12-N5-F50-C4` to run one row from a group.
