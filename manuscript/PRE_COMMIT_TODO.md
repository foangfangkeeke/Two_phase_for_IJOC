# Current-Round Pre-Commit Checklist

Resolve or explicitly defer the following items before creating the next manuscript commit.

## Appendix Review

- [ ] Review all appendices after the main chapters are stabilized, including notation tables, model details, proofs, algorithm supplements, equation labels, and all body-to-appendix cross-references. The appendices have not yet been reviewed in this round.

## Section 1

### Introduction revisions to implement

- [ ] Add a compact research-gap paragraph between the application background and the method overview. Cover both the application gap (joint long-term wireless-facility planning, scenario-adaptive route-level battery configuration, representative-day operations, and shared plug-in capacity) and the computational gap (large deterministic-equivalent and natural integer L-shaped scenario MIPs with discrete battery and occupancy decisions). Do not claim novelty beyond the citations.
- [ ] Add the required literature TODOs at the research-gap paragraph without fabricating references:
  `% TODO: Add citations on integrated electric-bus infrastructure, battery-sizing, and charging-scheduling models.`
  `% TODO: Add citations on stochastic infrastructure planning and integer L-shaped decomposition with mixed-integer recourse.`
- [ ] Clarify the decision sequence in the introduction: long-term wireless-facility planning, realization of a medium-term service-plan scenario, conditional fleet--battery configuration/procurement, and representative-day charging and operations. State explicitly that a scenario is a service-plan realization rather than a battery plan and that the model does not imply daily battery replacement.
- [ ] Define stationary wireless charging (SWC) and dynamic wireless charging (DWC) at first use, with only the minimum distinction needed in the introduction: station-based versus road-link-based facilities. Keep detailed technology descriptions in the model section.
- [ ] Strengthen the causal chain from model structure to algorithm motivation: shared plug-in capacity creates cross-route coupling; discrete battery and occupancy decisions make scenario recourse mixed integer; natural integer L-shaped separation leaves a large scenario MIP; pointwise cuts do not exploit facility--battery resource monotonicity. Then introduce the three matching components: lifted battery variables, two-phase route-level LP relaxations plus exact scenario verification, and monotonicity-strengthened cuts. Do not add formulas or pseudocode.
- [ ] Rewrite the contribution paragraph as three parallel, verifiable contributions: (i) the two-stage stochastic model and its conditional route locality/resource monotonicity; (ii) the exact reformulation-enabled decomposition method and the roles of lifting, two-phase separation, and strengthened cuts; and (iii) the computational study and its stated comparison dimensions. Do not present `layered structure` as an independent contribution.
- [ ] Replace the current wording `scenario-dependent decision variables lifting` with natural terminology such as `lifting scenario-dependent battery variables into the decomposition master`.
- [ ] Replace `joint operational resource order` with the established term `facility--battery resource monotonicity` (or the exact term used consistently in Chapter 4).
- [ ] Remove or qualify `This design preserves exactness when the cut loop converges`. Use the convergence statement established in Chapter 4, such as finite convergence under the stated exact-separation assumptions or an $\epsilon$-optimality statement under the specified tolerance, unless the theorem wording is revised first.
- [ ] Remove unsupported strength claims such as `comprehensive` and `extensive` from the computational contribution. Describe the planned comparison of EF, Natural ILS, Lifted ILS, Lifted two-phase, and Full, and the study dimensions (network size, scenario count, candidate facilities, and shared plug-in capacity) without implying results that are not yet available.
- [ ] Replace the current experimental-contribution paragraph with quantitative findings after the computational study is final:
  `% TODO: Replace the experimental-contribution paragraph with quantitative findings after completing the computational study.`
  `% TODO: Report the largest solved instance, relative runtime/gap improvements, and the separate effects of lifting, two-phase separation, and strengthened cuts.`
- [ ] Replace `more or less aligned with exact verification` with a precise statement tied to reported Phase 1 relaxation quality, relaxation gap, or shared-capacity coupling effects.
- [ ] Check all references to economic trade-offs. If retained, identify the specific facility-investment, battery-cost, and operational-charging-cost components rather than describing the model coupling as an unspecified economic trade-off.
- [ ] Add a source citation for the Singapore/Tower Transit data or retain an explicit citation TODO until the data provenance is documented:
  `% TODO: Add the authoritative citation or data-source reference for the Singapore/Tower Transit instances.`

### Cross-section consistency checks

- [ ] Verify that the introduction uses the same terms and timing as Chapters 3 and 4: service-plan scenario, scenario-adaptive battery configuration, representative-day operations, lifted reformulation, Phase 1/Phase 2, route-level LP relaxations, exact scenario operational MIP, and resource-monotonicity-strengthened cuts.
- [ ] Ensure the revised introduction does not introduce model variables, constraint labels, cut formulas, or algorithm steps that belong in Chapters 3 and 4, and does not imply that scenario and battery configuration are the same decision.
- [ ] Keep the introduction within approximately 800--1,000 English words after the missing literature and gap material is added; avoid expanding the literature review beyond the role needed to establish the gap.

## Section 3

### Modeling issues to resolve

- [ ] Repair visit-level energy continuity in `03_model.tex`. Check whether plug-in-eligible and SWC-eligible visit-event sets can overlap. Then use either one unified visit-energy balance or mutually exclusive charging-event cases, and explicitly impose `e_v^{\mathrm{dep}}=e_v^{\mathrm{arr}}` at visits with no charging variable. Do not allow energy to change without charging.
- [ ] Complete the route quantification for all route-local constraint families, including SWC and DWC charging bounds, travel-energy relations, state-of-charge bounds, boundary conditions, and overnight balance. Use a group-level statement such as `For each route $r\in\mathcal R$` where this avoids repeated long qualifiers, while keeping every displayed constraint mathematically unambiguous.
- [ ] Add first-use definitions for core symbols that currently rely only on the notation table: `\mathcal V_{\omega,r}^{\mathrm S}`, `\bar q_v^{\mathrm S}`, `\bar q_a^{\mathrm D}`, `\eta^{\mathrm P}`, `\eta^{\mathrm S}`, `\eta^{\mathrm D}`, `v_m^0`, and `v_m^N`. Check the rest of the section for any other core symbol first used in a constraint without a short in-text definition.
- [ ] State the energy basis of `q_v^{\mathrm P}`, `q_v^{\mathrm S}`, `q_a^{\mathrm D}`, and `q_m^{\mathrm N}` consistently. Verify that charging costs, efficiency factors in energy balances, and time/rate-based charging bounds use the same grid-side or battery-side convention. Confirm the convention for overnight charging from the implementation/model data before editing; do not infer it from notation alone.
- [ ] Define the infeasible-scenario convention immediately after the scenario recourse value: set `\Phi_\omega(w)=+\infty` when the scenario recourse problem is infeasible, or use the existing feasible-set notation consistently if that is the formulation actually used.
- [ ] Tighten the scope of the resource-monotonicity statement in Section 3.3. State that it concerns the conditional operational feasible set and minimum charging cost for a fixed facility--battery configuration, and does not imply monotonicity of the total recourse value when battery investment cost is included.
- [ ] Clarify the trip-chain energy connection without adding unnecessary equations: identify the terminal-layover visit event that connects consecutive trips, define the roles of `v_m^0` and `v_m^N` as the duty boundary visits, and explain how the overnight balance closes the daily energy cycle.

### Recommended clarification

- [ ] Clarify the decision time scale in the scenario-recourse introduction: scenarios represent service-plan realizations rather than daily battery changes; after realization, the model selects the scenario-adaptive fleet--battery configuration and evaluates representative-day charging operations.
- [ ] State near the battery-choice variables that all buses assigned to a route use one common battery option in a scenario, identifying this as a route-level homogeneous battery deployment assumption.
- [ ] Rewrite the discussion of fleet-size adjustment costs, residual values, and other omitted terms as scenario-dependent constants omitted because they do not affect optimal facility or recourse decisions. Mention their addition in result accounting only if the final experiments report complete system cost.
- [ ] Keep the Natural ILS discussion in Chapter 3 limited to its structural role: shared first-stage facility design, scenario-wise recourse separability, and within-scenario coupling through shared plug-in capacity. Leave cut generation, algorithm steps, and baseline implementation details to Chapter 4.

### Scope and verification notes

- [ ] Before editing, reconcile the visit-event ownership and charging-variable index sets with the implementation/data model, especially the possible overlap between plug-in and SWC visits and the definition of overnight charging quantity.
- [ ] Preserve the existing Chapter 3 structure, equation labels, cross-references, decision timing, daily-cost treatment, and model meaning. Do not introduce algorithmic material or prove the Section 4 resource-monotonicity results in Chapter 3.
- [ ] If page limits require figure reduction, consider moving the charging-resources illustration to Chapter 1 or removing it while retaining the daily-duty figure; do not remove or modify figures in this review round.

## Section 4

- [x] Give RSP its formal name and state which scenario-level LP relaxation it solves.
- [ ] Restrict RSP to feasibility screening and RSP feasibility cuts; do not introduce $Q_\omega^{\mathrm{RSP}}$, a separate RSP cut pool, or RSP optimality cuts.
- [x] Replace `solved to proof` with `solved to proven optimality or infeasibility` throughout the method section.
- [ ] Add one orienting sentence at the start of Section 4.4 before introducing the Phase 1 bounds.
- [ ] Clean up the placement of the route index in the compact operational recourse formulation.

## Chapter 4 Review

- [ ] Rename the method section to `A Two-Phase Integer L-Shaped Method` and align the opening paragraph so that the two-phase separation is the main method, the lifted reformulation is its equivalent reformulation, and resource-monotonicity cuts are the Phase 2 strengthening mechanism.
- [ ] Move the complete `Pointwise and strengthened cuts` block from Section 4.3 to Section 4.4 after the Phase 2 restricted master; rename Section 4.3 to `Resource monotonicity` and leave only the resource order, proposition, proof, and dominance corollary there.
- [ ] Clarify the Phase 1 bounds: define $LB^R$ as the current Phase 1 restricted-master bound and $UB^R$ as the best fully evaluated Phase 1 objective over a master candidate; replace `evaluated Phase 1 candidate value` in the algorithm with precise wording.
- [ ] Use strengthened feasibility and optimality cuts in the complete algorithm; retain pointwise cuts only as the unstrengthened baseline and add the sentence explaining their use in the computational comparison. Phase 1 continues to use only route-level dual and Farkas cuts.
- [ ] Simplify the finite-convergence theorem assumptions by removing the separate finite-termination assumption for auxiliary RSP separation, and extend the appendix proof to state that every nonterminating Phase 2 iteration adds a retained violated strengthened feasibility or optimality cut.
- [ ] Add the missing literature TODOs: cite the classical integer L-shaped method at the first occurrence of Natural ILS/integer L-shaped method, and cite standard pointwise integer L-shaped feasibility and optimality cuts before their first use.
- [ ] Run a final Section 4 consistency check after the edits: define $\theta_\omega$ before its first cut use, keep the order `Phase 2 master -> cuts -> RSP -> bounds -> algorithm -> convergence`, remove cut formulas and $D_\omega,I_\omega$ from Section 4.3, and preserve the existing mathematical definitions and labels.

## Section 5

- [x] State the Phase 1 switch tolerance as $\varepsilon_1=10^{-4}$ (0.01\%).
- [x] State that the master and exact scenario subproblems are solved with zero solver gap.
- [ ] Confirm and report the final outer tolerance $\varepsilon$, hardware, operating system, software versions, thread configuration, seed, time-limit policy, and solver settings needed for reproducibility.
- [ ] Align the method ladder with Chapter 4: EF, Natural ILS, Lifted ILS, Lifted two-phase, and Full; remove all references to tailored Phase 1 cuts, Phase 1 monotonicity cuts, and their ablations, and state that RSP settings are common across methods or explicitly report the common policy.
- [ ] Reorganize Chapter 5 into five subsections: experimental design and instance statistics; representative planning solution; algorithmic ablation and network scalability; scenario-count scalability; and shared-capacity sensitivity and Phase 1 relaxation tightness.
- [ ] Replace the instance-family comparison table with an instance-statistics table covering routes, trips or trip chains, candidate SWC stations, candidate DWC links, plug-in locations, battery options, scenarios, and extensive-form size; move experimental use and comparison-method descriptions into the text.
- [ ] Resolve the planning-horizon, facility-lifetime, capital-cost scaling, daily-equivalent cost, terminal-value or residual-value, and battery-capacity-dependent energy assumptions so that Section 5.1 is consistent with Chapter 3, including the $\texttt{weightPerEnergy}=0$ setting.
- [ ] Rewrite Section 5.2 as a result-based representative-solution analysis rather than future-tense planning text; populate or explicitly defer the requested facility, battery, charging-share, utilization, and shared-facility outputs without fabricating missing decisions.
- [ ] Rebuild the algorithmic comparison and mechanism table so that it identifies lifting, route-level lower bounding, and monotonicity strengthening separately; report status, UB, LB, gap, time, phase times, iterations, exact MIP evaluations, and feasibility/optimality cut counts, using defined symbols such as `n.r.`, `t.l.`, `m.l.`, and `—` rather than `not reported in the current run set`.
- [ ] Define the scenario-count and shared-capacity experiments precisely, including scenario-set construction, probabilities, seeds or replications, initial Phase 2 gap formula and event, time-limit/status rules, displayed precision, and evidence limits for causal or bottleneck claims; rename 5.5 and add a short chapter-end synthesis with `% TODO:` markers where results are still missing.

## Terminology

- [ ] Replace or justify the working baseline name `Natural ILS` before submission.

## Section 6

- [ ] Replace `route-level LP recourse` with `route-level LP relaxations` and describe the three method components separately: lifted reformulation, two-phase relaxation--verification, and resource-monotonicity-strengthened cuts.
- [ ] Replace the unconditional finite-convergence wording with `converges finitely under the stated exact-separation assumptions`, and avoid introducing `layered decomposition` as a new term in the conclusion.
- [ ] Reconcile the computational attribution with the final Section 5 ablation: describe two-phase relaxation--verification as the source of computational tractability only if supported, describe strengthened cuts as additional improvement only at the strength justified by the reported mechanism metrics, and avoid attributing the result to one component alone.
- [ ] Replace broad experimental claims with the exact tested scenario set, baseline names, time-limit policy, and termination language used in Section 5; distinguish time-limited Phase 2 verification from failure of the method and restrict bottleneck claims to tested boundary cases.
- [ ] Verify every numerical statement in the conclusion against the final Section 5 tables, including total time, final gap, initial Phase 2 gap, scenario counts, time limit, and the termination status of the hardest instances; retain `% TODO: verify against final computational tables` where the final data are not yet fixed.
- [ ] Add one concise application-level sentence only if the representative-solution results support it, using `scenario-adaptive route battery configurations` and shared wireless facilities without implying that facility and battery decisions occur in the same stage.
- [ ] Keep the three-paragraph conclusion structure and revise the limitations and future-work paragraph to match documented model and computational limits: tight shared-capacity cases, exact Phase 2 verification, stronger exact solution or parallelization, scenario uncertainty, and operational extensions already discussed in the paper.
