# Manuscript Section Logic

This file is the current paper map.
It should stay concise: section roles, subsection responsibilities, and open work only.
Paragraph-level drafts belong in the corresponding `.tex` files.

## Target Orientation

- Primary target style: IJOC / Transportation Science algorithm paper.
- Core contribution: reformulation-enabled partial integer L-shaped method for two-stage stochastic mixed-integer planning with shared resources and scenario-dependent discrete recourse.
- Application role: electric-bus wireless charging provides the motivating structure and computational test bed, not a standalone transportation case study.

## Paper Spine

1. `Introduction`
   - Motivate wireless charging deployment under service-plan scenario uncertainty.
   - Explain why scenario-adaptive battery sizing and shared charging resources create a difficult stochastic MIP.
   - State the algorithmic contributions and paper organization.

2. `Literature Review`
   - Stream 1: electric-bus charging and wireless infrastructure planning, kept concise as the application-structure source.
   - Stream 2: stochastic facility-location models and Benders / integer L-shaped decomposition, used as the main methodological positioning.
   - Organize Stream 2 around two performance dimensions: decomposition / reformulation choice and cut / separation strength.
   - End by positioning the paper as a tailored exact decomposition method for shared-resource mixed-integer recourse.

3. `Problem Formulation and Baseline Decomposition`
   - Present the compact two-stage stochastic formulation, emphasizing information structure and recourse coupling rather than model novelty.
   - Present the baseline integer L-shaped decomposition induced by the information structure.

4. `Reformulation-Enabled Integer L-Shaped Method`
   - Develop the lifted reformulation.
   - Develop the two-phase relaxation-verification structure.
   - Develop monotonicity-strengthened feasibility and optimality cuts.
   - Present the complete algorithm and convergence result.

5. `Computational Results`
   - Define the Tower-derived instance families and solver protocol.
   - Show one compact planning-solution interpretation.
   - Validate algorithmic performance through network-scale ablation, scenario-count scalability, and capacity-coupling stress tests.

6. `Conclusion`
   - Summarize model, method, computational evidence, limitations, and extensions.

## Cross-Section Alignment

- The core narrative is:
  SWC/DWC deployment changes in-route charging opportunities -> this changes route battery needs and residual plug-in charging demand -> residual plug-in charging demand creates shared capacity coupling -> the resulting two-stage stochastic MIP motivates lifted decomposition, relaxation-verification, and strengthened cuts.
- Section 3 introduces the baseline integer L-shaped decomposition only as a benchmark; Section 4 should focus on the paper's contributions.
- Section 5 should support the algorithm story rather than become a transportation case-study chapter.
- Use "service-plan scenario" consistently.
- Describe recourse as scenario-adaptive battery configuration plus scenario-level charging and energy feasibility, not as daily operations.
- Treat fleet-size changes, existing fleet status, and battery-depreciation status as fixed inputs after a scenario is realized.
- Defer cross-section wording cleanup to the final writing pass; during section drafting, record terminology issues here instead of synchronizing unrelated `.tex` files immediately.

## Section 3 Plan

`3.1 Two-stage stochastic formulation`

- Define scenario meaning and decision timing directly before the formulation.
- Define station, directed-link, and existing plug-in charging station sets.
- Define SWC deployment on stations outside existing hubs and DWC deployment on directed links.
- Define the main recourse variables and the shared plug-in capacity coupling.
- Use route-indexed trip chains, station visits, directed link traversals, and visit-time plug-in occupancy variables to absorb bus, location, and time indices in operational constraints.
- Group recourse constraints visibly into route battery configuration, plug-in charging use / shared capacity, and scenario-level charging / energy-feasibility constraints.
- In the plug-in block, distinguish route-restrictable charging-use constraints from the shared station-time capacity constraint that Phase 1 relaxes.
- Write the energy-feasibility block as route-local constraints imposed for each route, so Section 4 can reference the block directly as the route-level subproblem core.
- Do not introduce separate hub/interchange event sets in the main model; include trip-chain boundary energy and overnight charging amounts explicitly.
- Split energy balance into station arrival/departure transitions and directed link traversal transitions.
- Avoid a numbered black-box operational feasibility constraint in the core model; keep remaining timing conventions in the construction of visits, traversals, and charging windows.
- Present the compact two-stage objective and scenario recourse constraints.
- State the model boundary while explaining the recourse formulation.
- Identify the extensive-form stochastic MILP as the direct-solution benchmark.
- Keep the consolidated notation table in the appendix.

Confirmed modeling details for `3.1`:

- The operated route set is common across service-plan scenarios; scenarios change frequencies, fleet requirements, and associated trip-chain data, but do not add or remove routes.
- Each trip chain corresponds to one bus, so `n_{\omega,r}=|\mathcal{M}_{\omega,r}|`.
- All stations in `\mathcal{S}\setminus\mathcal{C}` and all directed links in `\mathcal{L}` are candidate SWC/DWC locations.
- Plug-in charging stations in `\mathcal{C}` are pre-existing resources; they are excluded from SWC candidates by the candidate-screening rule.
- `\kappa` is treated as the maximum grid-side plug-in energy input per occupied time slot, so no separate `\Delta t` is introduced.
- Charging quantities are grid-side purchased energy; efficiencies convert purchased energy into battery-side energy in the balance equations.
- Overnight charging occurs at depots, and overnight capacity is assumed sufficient to restore each bus to the upper admissible SoC before the next representative service day.
- SWC station congestion is not modeled separately; visit-level SWC opportunity limits absorb the available charging opportunity.
- Trip-level energy consumption is exogenous and does not vary with selected battery capacity.
- Shared plug-in station-time capacity is the only cross-route coupling among second-stage route-level constraints.

Section 3 TODO before final submission:

- Synchronize Appendix notation after Section 3 symbols stabilize.
- Decide whether to add a new decision-timeline figure for Stage 1 facilities, scenario realization, route-level battery choices, and charging-allocation recourse.
- Keep SWC/DWC technical illustrations out of `3.1`; consider moving them to the Introduction or appendix if useful.
- Ensure the final notation table states `\mathcal R`, `\mathcal K`, `\beta_k`, `n_{\omega,r}=|\mathcal M_{\omega,r}|`, depot boundary energy, grid-side charging quantities, and the candidate-screening rule for plug-in stations.
- Ensure the final model-boundary text records omitted details now kept out of `3.1`: cost time-scale conversion, omitted battery degradation, sufficient overnight depot charging, no separate SWC station-time congestion, and exogenous energy consumption.
- Check whether objective values reported in Section 5 are decision-relevant optimized costs or total system costs with omitted scenario constants added back.

`3.2 Baseline integer L-shaped decomposition`

- Define the baseline master with only `w` and scenario estimators `theta`.
- Define scenario subproblems that jointly optimize battery choices, charging amounts, plug-in occupancy, and energy states.
- State the limitation: battery-selection integrality, operational integrality, and shared plug-in capacity remain inside the scenario MIP.
- Transition to Section 4.

Open work:

- Check appendix notation table width after compilation.

## Section 4 Plan

`4.1 Lifted master reformulation`

- Lift scenario-dependent battery variables into the master without imposing nonanticipativity.
- Define conditional operational value `Q_omega(w,z_omega)` and the lifted master.
- State exact equivalence to the extensive form when operational value functions are represented exactly.

`4.2 Two-phase relaxation-verification framework`

- Phase 1 relaxes shared plug-in charger capacity coupling and operational integrality.
- The relaxed scenario recourse decomposes into route-level LP subproblems.
- Route-level LP dual information provides valid lower-bounding cuts for exact recourse because the relaxation value is a lower bound.
- Phase 2 restores exact scenario-level mixed-integer verification.

`4.3 Monotonicity-based strengthened cuts`

- State the monotonicity condition.
- Define the direction-selective indicator for opening facilities and upgrading batteries.
- State and prove strengthened feasibility and optimality cuts.
- Position these cuts as the main distinction between lifted two-phase and the full method.

`4.4 Complete solution procedure`

- Present pseudocode.
- State stopping rules.
- State finite convergence under exact master and exact Phase 2 separation.

Open work:

- Keep proofs in the main text for now; move only if Section 4 becomes too long.
- Keep RSP as an implementation-level Phase 2 enhancement, not one of the three conceptual contributions.
- Keep exact convergence theory separate from computational tolerance.

## Section 5 Plan

`5.1 Experimental design and instance families`

- Introduce the Tower-derived data as a realistic computational test bed, not a public benchmark.
- Define all main instance families in one place.
- Use `S`, `M`, `B`, and `L` for network scale.
- Use `B-k` for scenario-count tests.
- Use `B-10-C1` through `B-10-C5` for capacity-coupling tests.
- Define compared algorithms and common solver protocol.

`5.2 Solution characteristics on the base instance`

- Use `B-3`.
- Report a compact solution-summary table.
- Show selected shared stations or links and route-sharing counts.
- Keep interpretation short and tied to model validity.

`5.3 Network-scale algorithmic ablation`

- Use `S-3`, `M-3`, `B-3`, and `L-3`.
- Compare direct MILP, baseline integer L-shaped, lifted integer L-shaped, lifted two-phase, and full method where meaningful.
- Show which components expand the solvable network-scale boundary.

`5.4 Scenario-count scalability`

- Use `B-3`, `B-5`, `B-10`, `B-20`, and `B-40`.
- Compare `solver`, `lifted-two-phase`, and `full`.
- Emphasize scenario-count growth after fixing the base network.

`5.5 Capacity-coupling and relaxation-gap stress test`

- Use `B-10-C1` through `B-10-C5`.
- Vary existing plug-in charger capacity to change how tight the relaxed coupling constraints are.
- Compare lifted two-phase and full method unless final results justify full-only reporting.
- Interpret as a boundary / stress test, not a universal robustness claim.

Open work:

- Rerun final experiment tables under final instance names and algorithm settings.
- Extract the `B-3` planning solution table.
- Identify representative selected stations or links for route-sharing examples.
- Fill reproducibility details: solver version, hardware, time limit, MIP gap, threads.
- Add appendix diagnostics only after main Section 5 results are stable.

## Section 6 Plan

- Summarize the two-stage planning model.
- Summarize the algorithmic chain: lifting, Phase 1 lower bounds, Phase 2 verification, monotonicity-strengthened cuts.
- Summarize computational evidence at a high level without overclaiming beyond rerun results.
- State limitations: constructed scenarios, exogenous plug-in infrastructure, omitted battery-weight energy feedback, and case-specific parameterization.
- State future work: richer scenarios, broader candidate sets, operational uncertainty, and broader shared-resource stochastic MIPs.

## Active Checklist

- [ ] Finish Section 3 formulation details.
- [ ] Review Section 4 after Section 3 symbols stabilize.
- [ ] Rewrite Section 5 after final experiments are rerun.
- [ ] Decide appendix contents after main body stabilizes.
- [ ] Compile after each substantial `.tex` update.
