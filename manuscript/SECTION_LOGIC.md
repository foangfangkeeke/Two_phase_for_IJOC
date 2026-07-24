# Manuscript Section Logic and Open Work

This file records the current paper map, section responsibilities, structural issues, and open work.
It is a planning file, not final manuscript text.

## Target Orientation

- Primary orientation: IJOC/Transportation Science style algorithm paper.
- Core contribution: an exact reformulation-enabled partial integer L-shaped method for two-stage stochastic mixed-integer planning problems with shared binary resources and scenario-dependent discrete recourse.
- Application role: wireless charging deployment for electric buses supplies the motivating structure and computational test bed; it should not dominate the paper as a standalone transportation case study.
- Main evidence source: `experiment_results_2026-07-19.md`.

## Current Paper Spine

- Section 1, `Introduction`: motivate shared wireless charging deployment under service-plan uncertainty, state the optimization difficulty, and list algorithmic contributions.
- Section 2, `Literature Review`: position the work through two streams: electric-bus charging infrastructure planning and facility location under uncertainty with decomposition methods.
- Section 3, `Problem Formulation and Natural Decomposition`: define the original two-stage stochastic extensive-form MILP, the natural integer L-shaped decomposition, and the main limitation of that natural baseline.
- Section 4, `Reformulation-Enabled Integer L-Shaped Method`: develop the paper's method contributions: lifted reformulation, route-decomposable relaxation-verification, monotonicity-based strengthened cuts, and convergence.
- Section 5, `Computational Results`: define the common Tower-derived test bed, show one compact planning interpretation, and validate algorithmic performance through ablation, scenario scaling, and relaxation-gap stress tests.
- Section 6, `Conclusion`: summarize the model, method, computational evidence, limitations, and extensions.

## Cross-Section Alignment

- The introduction should use wireless bus charging as the motivating application, then move quickly through this chain: SWC/DWC deployment affects in-route charging opportunities, these opportunities affect battery sizing and residual plug-in fast-charging demand, and residual fast-charging demand creates shared plug-in fast-charger capacity coupling in the second-stage recourse.
- The literature review should keep two streams aligned: the application-side charging-infrastructure planning stream, and the method-side stochastic facility-location/decomposition stream.
- Do not use a literature comparison table by default; add one only if advisor feedback or target-journal positioning requires it.
- Section 3 should translate the application into the two-stage extensive-form model and introduce natural integer L-shaped decomposition as the classical decomposition induced by the information structure.
- Section 4 should start from the paper's method contributions rather than from natural ILS: lifted master exposes scenario-adaptive battery choices, Phase 1 relaxes shared plug-in fast-charger capacity coupling and operational integrality to generate route-level LP lower bounds, Phase 2 restores exact mixed-integer verification, and monotonicity-based cuts strengthen both separation phases when valid for exact recourse.
- Section 5 should use the application as a realistic computational test bed, not as a TRB-style standalone case-study narrative.
- The conclusion should restate both sides of the story: the application motivates the shared-resource stochastic MIP, and the method is intended to generalize beyond the wireless bus case.

## Global Issues Found

- The Chinese Markdown files now include UTF-8 BOM where needed; read them with UTF-8 encoding when using shell tools.
- The standalone Discussion section has been removed from `main.tex`, and the conclusion is now `sections/06_conclusion.tex`.
- Section 3 has been reset to a concise two-stage structure plus extensive-form formulation.
- Section 4 is structurally aligned with IJOC-style algorithm papers, but the prose should remain concise and avoid treating standard baselines as major theory.
- Section 4 should avoid claiming exact practical optimality when experiments terminate by tolerance; theorem statements can remain exact under exact-solve assumptions.
- Section 5.2 lacks the representative planning-solution table and selected shared-location examples.
- Section 5.1 needs a compact reproducibility paragraph: solver, version, hardware, time limit, mipGap, threads.
- Section 6 Conclusion now contains a compact framework draft.

## Section 3 Reset Plan

`3 Problem Formulation and Natural Decomposition`

- Opening paragraphs
  - State the wireless charging planning story only to the extent needed for the model.
  - Introduce the two-stage timing: shared facility deployment before scenario realization, and battery/operation recourse after scenario realization.
- `3.1 Planning structure and notation`
  - Define candidate SWC/DWC facilities and first-stage facility vector `w`.
  - Define service-plan scenarios and scenario-dependent representative-day data.
  - Define scenario-dependent battery variables `z_omega` and operational variables `y_omega`.
  - State that existing plug-in/fast chargers are exogenous operating resources.
  - Present the notation table.
- `3.2 Two-stage extensive-form formulation`
  - Present the objective.
  - Present battery-selection and battery-size constraints.
  - Present core operational feasibility constraints.
  - Present variable domains and close by identifying the extensive form as the direct-solver baseline.
  - Identify the extensive form as the direct-solver baseline.
- `3.3 Natural integer L-shaped baseline`
  - Define the natural master with only first-stage facility variables and scenario recourse estimators.
  - Define `Phi_omega(w)` as the natural recourse value that jointly optimizes scenario battery configuration and operations.
  - State the limitation: the scenario subproblem still contains battery-selection integrality, operational integrality, and shared plug-in fast-charger capacity coupling.
  - Transition to Section 4 as a reformulation-enabled improvement over this baseline.

Open work:

- Keep the scenario concept in Section 5.1; move detailed scenario-generation logic to an appendix if the main text becomes crowded.
- Fix the notation table width.
- Decide whether detailed charging-opportunity constraints should be expanded in appendix.

## Section 4 Reset Plan

`4 Reformulation-Enabled Integer L-Shaped Method`

- Contribution 1: exact structure-exposing lifted reformulation.
  - Lift scenario-dependent battery variables into the master without imposing cross-scenario nonanticipativity.
  - The contribution is not generic variable movement; it isolates remaining operational recourse and exposes facility-battery resource structure.
- Contribution 2: nested scenario-route relaxation-verification decomposition.
  - Phase 1 relaxes shared plug-in fast-charger capacity coupling and operational integrality.
  - The relaxed scenario recourse decomposes exactly into route-level LP subproblems.
  - Route-level dual information is aggregated into scenario-level lower-bounding cuts valid for exact recourse because `Q^R_omega(w,z_omega) <= Q_omega(w,z_omega)`.
  - Phase 2 restores the exact scenario-level operational MIP.
- Contribution 3: joint-resource direction-selective monotonicity cuts.
  - Use the partial order induced by adding wireless facilities and upgrading route batteries.
  - Strengthened feasibility and optimality cuts cover dominated configurations.
  - Do not claim monotonicity, logic cuts, or combinatorial cuts are new in general.
- `4.1 Lifted master reformulation`
  - Lift scenario-dependent battery variables into the master without imposing nonanticipativity on them.
  - Define `Q_omega(w,z_omega)` and the lifted master.
  - State exact equivalence to the extensive form when operational value functions are represented exactly.
- `4.2 Two-phase relaxation-verification framework`
  - Narrative chain: exact operational recourse still couples routes through existing plug-in fast-charger capacity -> Phase 1 relaxes this coupling and operational integrality -> route-level LP subproblems provide valid lower bounds and LP-dual cuts -> Phase 2 restores exact mixed-integer operational verification -> a converged Phase 2 loop restores equivalence to the lifted/natural/extensive formulations.
- `4.3 Monotonicity-based strengthened cuts`
  - State monotone feasibility and operating-cost condition.
  - Define the direction-selective indicator.
  - State and prove strengthened feasibility and optimality cuts.
  - Explain that these cuts are the main distinction between lifted two-phase and the full method.
- `4.4 Complete solution procedure`
  - Present pseudocode.
  - State stopping rules.
  - State finite convergence under exact master and exact Phase 2 separation.

Open work:

- Decide whether Phase 1 Farkas feasibility cuts are implemented and should be stated.
- RSP is retained only as an optional experimental enhancement and will be reconsidered after the capacity-coupling experiments reveal whether exact subproblem evaluation becomes a computational bottleneck.
- Move any long proof details to `appendices/appendix_proofs.tex`.
- Keep implementation-tolerance language separate from exact convergence theory.

## Section 5 Reset Plan

`5 Computational Results`

- `5.1 Experimental design and instance families`
  - Introduce the Tower-derived network only as a realistic computational test bed, not as a public benchmark.
  - Define the scenario concept once: scenarios are alternative medium-term service-plan realizations, not same-day random noise.
  - Define all main instance families in one place, following the IJOC pattern in which the experiment setup contains the instance naming convention and experiment map.
  - Use compact instance codes because all experiments are Tower-derived: `S`, `M`, `B`, and `L` denote small, medium, base, and large network scales.
  - Network-scale family: `S-3`, `M-3`, `B-3`, and `L-3`, where network size varies and scenario count is fixed at three.
  - Scenario-count family: `B-k`, with `k` in `{3,5,10,20,40}`, where the base network is fixed and the number of service-plan scenarios varies.
  - Capacity-coupling family: `B-10-C1` through `B-10-C5`, where the base network and 10 scenarios are fixed and the number of existing plug-in fast chargers varies.
  - Explain capacity coupling briefly in Section 5.1 as an instance-family purpose, then explain the residual fast-charging demand and Phase 1/Phase 2 relaxation-gap mechanism in Section 5.5.
  - Summarize travel-time preprocessing and main cost, battery, charging, and electricity parameters.
  - Define the compared methods and common solver protocol.
- `5.2 Solution characteristics on the base Tower instance`
  - Use `B-3` as the representative planning solution.
  - Add a compact solution-summary table.
  - Add selected shared station/link examples and route-sharing counts.
  - Keep the interpretation short: the model produces a network-level facility plan, not a single-day charging schedule.
- `5.3 Network-scale algorithmic ablation`
  - Use `S-3`, `M-3`, `B-3`, and `L-3` to test the ablation chain under fixed scenario count.
  - Compare extensive form, natural ILS, lifted ILS, lifted two-phase, and full method where each method is computationally meaningful.
  - Use the results to show which algorithmic components become necessary as network size increases.
- `5.4 Scenario-count scalability`
  - Use the scenario-count family `B-k`, with `k` in `{3,5,10,20,40}`, defined in Section 5.1.
  - Compare the direct solver, lifted two-phase, and full method where available.
  - Do not include natural ILS or lifted ILS because Section 5.3 already establishes that they fail at small scale.
  - Emphasize how scenario count stresses recourse growth after the network is fixed.
- `5.5 Capacity-coupling and relaxation-gap experiment`
  - Use the capacity family `B-10-C1` through `B-10-C5`, defined in Section 5.1.
  - Within Section 5.5, abbreviate these variants as `C1` through `C5` after stating that all are based on `B-10`.
  - Vary the number of existing plug-in fast chargers to change how binding the relaxed capacity-coupling constraints are.
  - Use this variation as an empirical stress test for the Phase 1--Phase 2 relaxation gap defined in the method section.
  - Compare lifted two-phase and full method because both use the same lifted reformulation, Phase 1 relaxation, and exact Phase 2 verification; the main difference is monotonicity strengthening.
  - Test whether strengthened cuts reduce Phase 2 verification burden as capacity coupling becomes tighter.

Open work:

- Finalize the unified Section 5.1 instance-family table, including data-file names, varied dimensions, and primary experimental use.
- Extract or generate the `B-3` planning-summary table.
- Identify representative shared selected stations or links.
- Resolve or explain why the high-frequency scenario has fewer buses than the base scenario in current generated data.
- Rerun or re-extract all Section 5 computational tables under the final instance naming scheme and final algorithm settings.
- Run or extract `L-3` results if the large/full Tower-derived network is included in the network-scale table.
- Run or extract `B-10-C1` through `B-10-C5` capacity-coupling results.
- Remove framework-style section roadmaps and subsection summary sentences from the final Section 5 draft unless a target-journal reference uses the same device in the same location.

## Section 6 Reset Plan

`6 Conclusion`

- Summarize the planning model and its two-stage information structure.
- Summarize the algorithmic chain: lifted reformulation, Phase 1 LP lower bounds, Phase 2 exact verification, and monotonicity-based cuts.
- State the strongest computational evidence: the full method solves the largest Tower instance while alternatives fail or time out.
- State limitations: constructed service-plan scenarios, exogenous plug-in infrastructure, omitted energy-consumption feedback from battery weight, and case-study-specific parameterization.
- State future work: richer scenario generation, candidate-facility-set expansion, operational uncertainty, and application to broader shared-resource stochastic MIPs.

## Experiment Checklist

- [ ] Section 5.2: representative solution table for `B-3`.
- [ ] Section 5.2: selected SWC count, selected DWC count, facility cost, expected battery cost, expected operating cost, total objective value, average battery capacity, and maximum battery capacity.
- [ ] Section 5.2: representative selected stations or links with route-sharing counts.
- [ ] Section 5.3: network-scale ablation table with solver, natural ILS, lifted ILS, lifted two-phase, and full method.
- [ ] Section 5.3: rerun network-scale ablation after finalizing instance names, solver settings, and algorithm variants.
- [ ] Section 5.3: table notes for Conv., TL, OOM, gaps, and skipped-case logic.
- [ ] Section 5.3: mechanism contrast between lifted two-phase and full method on `B-3`.
- [ ] Section 5.4: scenario-count scalability table for direct solver, lifted two-phase, and full method where available.
- [ ] Section 5.5: capacity-coupling table for lifted two-phase and full method on `C1` through `C5`, all based on `B-10`.
- [ ] Appendix: reproducibility details and additional logs if the main text becomes crowded.
