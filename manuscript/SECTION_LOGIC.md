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

3. `Problem Formulation and Recourse Structure`
   - Present the compact two-stage stochastic formulation and complete scenario recourse model.
   - Extract the recourse structures that motivate the algorithm: scenario separability, conditional route locality, and facility--battery resource monotonicity.

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

- Define service-plan scenarios, probabilities, and decision timing.
- Make clear that scenarios fix route frequencies, timetables, trip chains, active-fleet requirements, and trip-level energy requirements; the model does not choose fleet size.
- Define first-stage wireless facility decisions on candidate SWC stations and DWC links.
- Present the compact two-stage stochastic objective and the scenario recourse value `Phi_omega(w)`.
- Do not expand the recourse feasible set in this subsection.

`3.2 Scenario recourse formulation`

- Expand the complete scenario recourse model using route-indexed trip-chain data.
- Organize the recourse model into four paragraph blocks: battery configuration and cost; charging cost; plug-in charging and shared capacity; wireless charging and route energy dynamics.
- In the battery block, define route battery choices and induced battery capacity.
- In the charging-cost block, include daytime plug-in, SWC, DWC, and overnight depot charging.
- In the plug-in block, define plug-in charging amounts, occupancy variables, time slots, and station-time capacity.
- In the energy block, define SoC bounds, SWC/DWC availability, station and link energy balances, depot boundary conditions, and overnight charging balance.
- Keep model explanations short but sufficient for reproducibility.

`3.3 Recourse structure`

- State scenario separability and define the resulting direct baseline decomposition as Natural ILS, without formal baseline master or cut formulas.
- State conditional route locality: fixed `(w,z_omega)` leaves the energy-dynamics block route-indexed, while shared plug-in charger capacity links plug-in occupancy across routes at each station-time pair.
- Separate structure from algorithm operation: route locality motivates the method but should not pre-state the relaxation design in Section 3.
- State facility--battery resource monotonicity under the model specification that trip-level energy consumption is independent of battery capacity; do not use implementation parameter names in the main text.
- State only the monotone operational resource order in Section 3; do not use an unused-resource explanation for battery monotonicity.
- Defer the formal resource-monotonicity proof to Section 4.3, where battery upgrades are handled by energy-state shifting.
- Close by identifying the layered recourse structure without naming the specific algorithmic components; leave lifting, two-phase separation, and strengthened cuts to Section 4.

Confirmed modeling details for `3.1`:

- The operated route set is common across service-plan scenarios; scenarios change frequencies, active-fleet requirements, and associated trip-chain data, but do not add or remove routes.
- Each trip chain corresponds to one bus, so `n_{\omega,r}=|\mathcal{M}_{\omega,r}|`.
- All stations in `\mathcal{S}\setminus\mathcal{C}` and all directed links in `\mathcal{L}` are candidate SWC/DWC locations.
- Plug-in charging stations in `\mathcal{C}` are pre-existing resources; they are excluded from SWC candidates by the candidate-screening rule.
- `\kappa` is treated as the maximum grid-side plug-in energy input per occupied time slot, so no separate `\Delta t` is introduced.
- Charging quantities are grid-side purchased energy; efficiencies convert purchased energy into battery-side energy in the balance equations.
- Overnight charging occurs at depots, and overnight capacity is assumed sufficient to restore each bus to the upper admissible SoC before the next representative service day.
- SWC station congestion is not modeled separately; visit-level SWC opportunity limits absorb the available charging opportunity.
- Trip-level energy consumption is exogenous and does not vary with selected battery capacity.
- Shared plug-in station-time capacity is the main explicit cross-route coupling among second-stage route-level constraints.

Section 3 TODO before final submission:

- Revisit the label "Natural ILS"; use a more conventional name if the target literature suggests one.
- Synchronize Appendix notation after Section 3 symbols stabilize.
- Decide whether to add a new decision-timeline figure for Stage 1 facilities, scenario realization, route-level battery choices, and charging-allocation recourse.
- Keep SWC/DWC technical illustrations out of `3.1`; consider moving them to the Introduction or appendix if useful.
- Ensure the final notation table states `\mathcal R`, `\mathcal K`, `\beta_k`, `n_{\omega,r}=|\mathcal M_{\omega,r}|`, depot boundary energy, grid-side charging quantities, and the candidate-screening rule for plug-in stations.
- Ensure the final model-boundary text records omitted details now kept out of `3.1`: representative-day conversion of investment costs, omitted battery degradation, sufficient overnight depot charging, no separate SWC station-time congestion, and exogenous energy consumption.
- Check whether objective values reported in Section 5 are decision-relevant optimized costs or total system costs with omitted scenario constants added back.

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
- Prove model-induced resource monotonicity in the method section; battery upgrades require an energy-state shifting argument because absolute SoC lower bounds also increase.
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
