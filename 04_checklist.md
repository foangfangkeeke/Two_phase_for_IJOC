# Chapter 4 Mathematical Closure Checklist

Scope: accept only changes needed for mathematical definitions, proof closure, cut validity, variable/value-function consistency, and pseudocode consistency. Do not handle literature, `.bib`, English polishing, section compression, experiments, or layout.

## Accepted Items

- [x] Define the induced route battery capacity used in Chapter 4:
  `B_{\omega,r}(z_\omega)=\sum_{k\in\mathcal K}\beta_k z_{\omega,r,k}`.
- [x] Define the base facility domain and battery-choice domain explicitly, so `dom Q_\omega` is not defined through the current master cut pools.
- [x] Remove `eq:battery-size` from the exact operational subproblem constraints in Chapter 4; treat `B_{\omega,r}(z_\omega)` as known after `z_\omega` is fixed.
- [x] Define the route-level operating block `y_{\omega,r}` and route-additive charging cost `C_{\omega,r}^{chg}` before using route LP values.
- [x] Clarify Phase 1 dual cut generation: scenario optimality cuts require all route LPs feasible and bounded; infeasible route LPs trigger Farkas feasibility separation; unbounded route LPs are ruled out by nonnegative costs and retained bounds.
- [x] State that, after the `B(z)` convention, master dependence in route LPs enters only through RHS terms: wireless upper bounds through `w`, and SoC/boundary constraints through induced battery capacity.
- [x] Handle duplicate battery capacities by using capacity-based dominance and describing the resource relation as an order on induced resource vectors, avoiding an antisymmetry claim on raw `z_\omega` if duplicate capacities exist.
- [x] Strengthen finite-convergence proof by explicitly deriving `theta_\omega <= Q_\omega`, absence of violated exact optimality cuts implying `theta_\omega >= Q_\omega`, hence `theta_\omega=Q_\omega` and `LB=UB`.
- [x] Change scenario probabilities from `p_\omega\ge0` to `p_\omega>0` in Chapter 3 so every `theta_\omega` has positive objective weight in the convergence proof.

## Not Accepted / Out of Scope

- [ ] Do not rework the frozen value-function notation.
- [ ] Do not introduce new `\mathcal Y` feasible-set symbols.
- [ ] Do not add literature or `.bib` changes.
- [ ] Do not rewrite the section/subsection structure.
- [ ] Do not move proofs to the appendix or compress the chapter.

## Verification

- [x] Search for stale terms: `master-feasible` in `dom Q_\omega`, `eq:battery-size` inside `Q_\omega`, old pointwise/monotone labels, `\mathcal E_\omega`.
- [x] Run `git diff --check`.
- [x] Compile from `manuscript/` with `pdflatex`, `bibtex`, and `pdflatex` twice.

## Follow-Up Issues Accepted and Resolved

- [x] Keep Chapter 3 and Chapter 4 scenario probabilities consistent with `p_\omega>0`.
- [x] Rename the evaluated exact objective in the upper-bound formula to `UB^{cand}(w,z)` and reserve `UB` for the incumbent bound.
- [x] Clarify that proven-optimal exact values are required for exact cuts and certified exact gaps, while feasible time-limited operating costs can support only a heuristic incumbent bound unless separately reported.
- [x] Distinguish complete cut families `\mathcal F_\omega,\mathcal O_\omega` from current finite subsets `\mathcal F_\omega^{cur},\mathcal O_\omega^{cur}`.
- [x] Add a formal Phase 1 Farkas ray and feasibility-cut expression.
- [x] Define `\mathcal J` as a typed disjoint union of SWC and DWC decision indices.
- [x] Rewrite `\mathcal Z_\omega` in standard binary-vector set form.
- [x] Define `B_{\omega,r}` and `\bar B_{\omega,r}` before using them in the monotonicity proof.
- [x] Replace unstable `master-feasible configuration` language with the lifted design domain where cut validity is stated.
- [x] Replace the pointwise-cut wording with redundancy relative to `\theta_\omega\ge L_\omega`.
- [x] Consolidate repeated Phase 1/RSP exactness statements into one sentence.
- [x] Compress the Phase 1 occupancy-variable explanation to exact elimination from the capacity-relaxed model.
