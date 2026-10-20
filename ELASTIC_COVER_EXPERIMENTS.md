# Elastic Deficit Resource-Cover Experiments

## Purpose

Test whether a minimum virtual-energy deficit can produce a stronger certified
Phase 2 feasibility cut than the ordinary monotone cut. The original Exact SP
remains unchanged and is still the only feasibility oracle.

## Experimental Mechanism

- Trigger only after an Exact SP is proven infeasible.
- Solve an independent Elastic LP with the same facilities, batteries, and
  shared plug-in capacity. Plug-in occupancy variables are relaxed to avoid a
  highly degenerate auxiliary MIP; its deficit is a lower bound on the integer
  Elastic deficit.
- Add nonnegative vehicle-side virtual energy at the initial deadhead, each
  inter-stop energy transition, and the final deadhead requirement.
- Minimize total virtual energy. The optimal value is the scenario deficit
  `D_omega` in kWh.
- Always retain the ordinary monotone feasibility cut.
- Additionally require candidate resource improvements to have aggregate
  maximum energy contribution at least `D_omega`.

The initial facility coefficient is the total vehicle-side DWC energy that the
candidate link could provide over every traversal in the scenario. The initial
battery coefficient is conservatively bounded by the route's total daily
traction energy. Coefficients are capped at `D_omega` for scaling. These are
safe but deliberately conservative upper bounds; the first experiments must
measure whether the resulting cut is materially stronger than the ordinary
monotone cut.

## Correctness Boundary

- Elastic solutions are never treated as feasible solutions of the original
  model and never update the original UB.
- A cover cut is generated only when the Elastic SP is solved to optimality and
  returns a strictly positive deficit.
- Exact SP timeout or an indeterminate Elastic SP falls back to the ordinary
  monotone cut.
- The reported LB, UB, and gap remain those of the original problem.

## Planned Sequence

1. Build and smoke-test `tower_small`, one scenario.
2. Compare ordinary versus Elastic cover cuts on `tower_medium`, one scenario.
3. Expand to two scenarios only if the one-scenario cuts are nontrivial.
4. Expand to three scenarios only if the additional Elastic solve time is
   offset by fewer infeasible Master candidates or faster LB progress.

## Results

Pending.

### Initial MIP smoke test

`tower_small / 1 scenario / cap1 / top1` baseline converged in 6.09 seconds at
objective `1622.125445`. The first Elastic MIP did not complete its initial
auxiliary solve after more than 100 seconds and used about 653 MB. Minimizing
only virtual energy leaves real charging and occupancy binaries highly
degenerate, so this formulation was rejected before medium testing. The next
test relaxes only the auxiliary model's occupancy binaries; the original Exact
SP remains unchanged.

### Elastic LP smoke test

On the same `tower_small / 1 scenario / cap1 / top1` case, the Elastic LP
preserved the final objective `1622.125445` and the same 12 Phase 2 rounds. It
produced positive deficits in three infeasible iterations (`81.2716`,
`21.2716`, and `5.8440` kWh), but did not alter the Master search path. Total
time increased from 6.09 to 7.18 seconds. Three other Exact-infeasible designs
had zero LP deficit and safely fell back to the ordinary monotone cut.

`tower_medium / 1 scenario / cap3 / top50` was stopped after about 165 seconds.
After the initial Phase 2 portion it generated almost exclusively feasible
points and local optimality cuts; the LB moved only from about `8809.432` to
`8809.478`. This is an optimality-cut bottleneck rather than an infeasibility
bottleneck, so it is not an informative Elastic-cover test.

## Candidate Operational Classification

A later repair-order experiment may classify each scenario bus chain as:

1. feasible without daytime charging;
2. feasible without plug-in charging when all candidate DWC links and the
   maximum battery are available;
3. still dependent on plug-in charging under those maximum DWC resources.

This classification may prioritize DWC for the second group and batteries plus
DWC that reduces plug occupancy for the third group. It is not yet part of a
valid cut: an optimal Elastic solution can shift deficit among vehicles under
shared plug capacity, so its per-vehicle slack cannot be treated directly as a
necessary route-specific deficit.
