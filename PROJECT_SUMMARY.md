# Project Summary

This file records stable project facts for the manuscript.
For active section plans, use `manuscript/SECTION_LOGIC.md`.
For writing workflow rules, use `manuscript/WRITING_RULES.md`.
For implementation and CLI details, use `handoff.md` when needed.

## Paper Positioning

- Target orientation: IJOC / Transportation Science style algorithm paper.
- Application: wireless charging infrastructure planning for electric bus systems.
- Method contribution: reformulation-enabled partial integer L-shaped method for a two-stage stochastic mixed-integer planning problem.
- Application role: supplies a realistic shared-resource stochastic planning structure and computational test bed; it should not dominate the paper as a transportation case study.

## Problem Interpretation

Wireless charging facilities are installed before the future fleet-service configuration is known.
The installed facilities must support multiple possible future operating plans.
Each scenario represents one plausible future fleet-service configuration and is operationalized as a representative operating day with route frequencies, timetables, trip chains, fleet requirements, and trip-level energy requirements.

Given a scenario, fleet-size changes, existing fleet status, and existing battery-depreciation status are fixed inputs.
They are not optimization decisions.
The model optimizes the cross-scenario wireless infrastructure layout and the scenario-adaptive battery configuration and charging response.

Existing plug-in / fast-charging stations are exogenous operational resources.
They enter the recourse problem through shared capacity constraints and are not candidate facility-location decisions.
The strategic facility-location decisions concern only stationary wireless charging (SWC) locations and dynamic wireless charging (DWC) links.

## Decision Hierarchy

| Layer | Decision | Scenario relation | Algorithmic role |
|---|---|---|---|
| Strategic | Wireless facility layout `w` | Shared across scenarios; here-and-now | Master |
| Tactical | Route-level battery configuration `z_omega` | Scenario-adaptive recourse | Lifted master |
| Operational | Charging and energy-state decisions `y_omega` | Conditional on `w` and `z_omega` | Subproblem |

Battery configuration and charging operation are physically distinct, but no additional uncertainty is revealed between them.
Both belong to the second-stage recourse.
Battery capacities are not daily dispatch decisions; they adapt to the realized future fleet-service configuration.

## Scenario Wording

Use:

- `future fleet-service configuration scenario`
- `scenario-adaptive battery configuration`
- `representative operating day`
- `existing fleet status and battery-depreciation status are fixed scenario inputs`

Avoid:

- saying the scenario is a battery plan;
- saying battery sizing is first-stage;
- saying the master is a pure first-stage model after lifting;
- implying that exogenous fleet changes or depreciation are optimized in the model.

## Mathematical Spine

The extensive-form model minimizes:

```text
wireless facility cost
+ expected scenario battery-configuration cost
+ expected scenario charging-operation cost
```

Core constraints:

- each route selects one battery option in each scenario;
- wireless charging is available only if the corresponding SWC/DWC facility is installed;
- bus energy states satisfy route-level energy balance and state-of-charge bounds;
- existing plug-in fast chargers have shared station-time capacity limits;
- operational variables define charging amounts, charger occupancy, and energy trajectories.

The shared plug-in fast-charger capacity constraints are the main route-coupling structure in the second-stage operational problem.

## Method Contributions

The paper's method is a reformulation-enabled partial integer L-shaped method.

1. Lifted reformulation:
   scenario-dependent battery variables are lifted into the master without imposing cross-scenario nonanticipativity.
   This exposes the facility-battery structure and leaves conditional operational recourse in the subproblem.

2. Two-phase relaxation-verification:
   Phase 1 relaxes shared plug-in fast-charger capacity coupling and operational integrality, yielding route-level LP subproblems and valid lower-bounding cuts.
   Phase 2 restores exact scenario-level mixed-integer operational verification.

3. Monotonicity-strengthened cuts:
   under the no battery-weight feedback setting, opening more wireless facilities or selecting larger batteries cannot reduce feasibility.
   This supports direction-selective feasibility and optimality cuts that exclude or bound dominated configurations more aggressively than generic no-good cuts.

Finite convergence is stated under exact master solution, exact Phase 2 separation, and valid cuts.
Implementation tolerance language must remain separate from exact convergence theory.

## Algorithm Ladder

| CLI problem | CLI algorithm | Paper role |
|---|---|---|
| `wireless` | `solver` | direct extensive-form MILP benchmark |
| `wireless` | `natural-ils` | classical natural integer L-shaped baseline |
| `wireless_reform` | `lifted-ils` | lifted reformulation baseline |
| `wireless_reform` | `lifted-monotone` | lifted plus monotone-cut cross-ablation |
| `wireless_reform` | `lifted-two-phase` | lifted two-phase method |
| `wireless_reform` | `full` | complete proposed method |

Appendix candidates:

- `lifted-two-phase-no-rsp` for RSP diagnostics;
- `full-no-p1-custom` if a Phase 1 custom-cut ablation is needed.

## Experiment Logic

Main Section 5 logic:

- 5.2: show that the planning solution on the base instance is interpretable.
- 5.3: test how algorithmic components expand the solvable network-scale boundary.
- 5.4: test scalability with increasing scenario count on a fixed base network.
- 5.5: stress the complete method under different capacity-coupling / relaxation-gap conditions.

Current intended instance families:

- Network scale: `S-3`, `M-3`, `B-3`, `L-3`.
- Scenario count: `B-3`, `B-5`, `B-10`, `B-20`, `B-40`.
- Capacity coupling: `B-10-C1` through `B-10-C5`.

Numerical values should be filled only after experiments are rerun under the final naming and algorithm settings.
