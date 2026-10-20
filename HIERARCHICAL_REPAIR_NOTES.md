# Hierarchical Repair and Strengthened Monotone Cuts

## Current Scope and Priority

- Optional Phase 2 RSP is implemented and committed. Combined experiments use
  the order Master -> RSP -> Exact SP -> repair-enhanced monotone cut. Repair
  runs only when RSP cannot cut the candidate and Exact SP certifies it
  infeasible.
- The former `final` route-energy-budget variant is no longer required and will
  not be migrated to the COPT rewrite.
- The immediate priority is to find a component-wise larger resource design
  that is still certified infeasible, using as few additional Exact SP solves
  as possible, and to generate the monotone feasibility cut from that stronger
  anchor.
- This work remains focused on stronger infeasibility certificates and cuts.
  RSP implementation, additional lower-bound inequalities, and unrelated
  diagnostics must not be mixed into the current experiments.

## Objective

When a Phase 2 Exact SP proves a scenario design `(w, B_omega)` infeasible,
search for a component-wise larger design that is still certified infeasible and
generate the monotone feasibility cut from that stronger anchor. Repair never
changes the original model. A time limit or unknown status is not an
infeasibility certificate.

## Preconditions

The current model is feasibility-monotone in the resources used by repair:

- Opening a DWC facility only adds a charging opportunity. Existing operations
  remain feasible by assigning zero charging to it.
- Increasing a route battery preserves feasibility. Energy consumption is
  independent of battery capacity, and the SOC formulation permits the same
  operating schedule with the larger capacity.

Therefore, a certified-infeasible anchor excludes every component-wise smaller
resource design. A cut from a larger certified-infeasible anchor dominates the
ordinary monotone cut from the original Master candidate.

## Worktree Implementation

The uncommitted repair experiment currently provides:

- `--battery-repair-cuts <0|1>` to enable repair-enhanced cuts for `full`;
- `--repair-facilities <0|1>` to include or exclude facility dimensions;
- `--repair-boundary-limit <n>` to stop after `n` prefix boundaries in each
  resource group;
- `--repair-probe-limit <n>` to cap Exact SP probes per scenario repair;
- `--repair-single-step <0|1>` for the cheap one-level battery experiment.

The hierarchical method first searches the battery-resource sequence using
prefix binary search and discrete battery-level refinement. It then fixes that
certified-infeasible battery anchor and searches the facility-resource sequence.
It distinguishes certified infeasible, feasible, and unknown outcomes, uses the
remaining one-hour global time budget, and restores the Exact SP resource bounds
after every search. If a search stops early, the cut uses only the largest
anchor already certified infeasible.

Definitive Exact SP results are cached by scenario and resource configuration in
the committed implementation. Cache hits do not consume solver time and do not
change the cut sequence.

Feasible repair probes may update the UB only when all scenarios can be combined
under one shared facility design. Different scenario battery choices remain
scenario-indexed.

## COPT Test Settings

All results below use the Gurobi-comparable baseline:

- COPT 8.0.7;
- 12 threads for Master and Exact SP;
- one thread per Phase 1 route LP, with up to 12 routes in parallel;
- relative MIP gap `1e-4` (0.01%);
- Exact SP dual simplex;
- `rsp=0`.

## Results

### `tower_small`, one scenario, `cap=1`, `dwc-top-n=1`

| Method | P2 rounds | Infeasible rounds | Repair probes | Total time |
| --- | ---: | ---: | ---: | ---: |
| Ordinary monotone cut | 12 | 6 | 0 | 4.352 s |
| First mixed-resource boundary | 10 | 4 | 16 | 5.869 s |

Repair reduces infeasible iterations and supplies an early UB, but its Exact SP
overhead is larger than the saved search time.

### `tower_small`, three scenarios, `cap=1`, `dwc-top-n=1`

| Method | P2 rounds | Infeasible rounds | Repair probes | Strengthened cuts | Total time |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ordinary monotone cut | 10 | 5 | 0 | 0 | 15.438 s |
| Battery-only, first boundary | 8 | 3 | 30 | 3 | 20.216 s |
| Facilities + batteries, first boundary | 8 | 3 | 26 | 1 | 20.235 s |
| Battery-only, full scan | 8 | 3 | 80 | 4 | 29.191 s |
| Battery-only, four-probe cap | 8 | 3 | 24 | 1 | 18.776 s |
| One-level battery step, all scenarios | 9 | 4 | 7 | 3 | 16.143 s |
| One-level step, stop after first strengthened cut | 9 | 4 | 4 | 2 | 16.073 s |

The complete and mixed-resource searches are not competitive on this case. A
single facility-boundary probe consumed about 9.4 seconds. The four-probe cap is
the cheapest hierarchical setting that found a stronger anchor, but it remains
slower than baseline.

The one-level battery step is much cheaper. It reduces one Phase 2 round and is
within about 0.7 seconds of baseline, but does not yet demonstrate a runtime
improvement.

### RSP combined with repair: `tower_medium`, three scenarios, `cap=2`, `dwc-top-n=10`

These 300-second stress tests are diagnostic only. The final medium experiments
prefer `cap=3`; under that setting RSP avoids the Exact-infeasible candidates,
so repair does not trigger.

| Method | P2 rounds | RSP-infeasible rounds | Exact-infeasible rounds | Repair probes | Repair time | First UB | Last LB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| RSP only | 40 | 31 | 2 | 0 | 0 s | none | 9342.182 |
| RSP + one battery boundary | 32 | 24 | 2 | 37 | 49.8 s | 262.98 s | 9339.992 |
| RSP + one-level battery step | 40 | 32 | 2 | 2 | 2.46 s | none | 9342.211 |

The boundary search found a valid UB, but its Exact SP overhead reduced the
number of completed Master rounds and slowed LB progress. The one-level step
strengthened both encountered Exact-infeasible cuts at low cost and marginally
improved the final LB. It is the better candidate for a cut-strengthening
component; the boundary search should be treated separately as an optional UB
heuristic.

### `tower_small`, five scenarios, `cap=1`, `dwc-top-n=1`

| Method | P2 rounds | Infeasible rounds | Repair probes | Strengthened cuts | First UB | Total time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Ordinary monotone cut | 18 | 10 | 0 | 0 | 7.714 s | 25.620 s |
| One-level step, stop after first strengthened cut | 15 | 9 | 12 | 5 | 6.416 s | 26.023 s |

The light repair finds an UB earlier and reduces three Phase 2 rounds, while the
total runtime is effectively tied. More cases are required before treating it
as an acceleration rather than a search-path diagnostic.

### `tower_medium`, three scenarios, `cap=2`, `dwc-top-n=10`

These COPT runs use a 300-second diagnostic time limit. This case was selected
from the earlier Gurobi logs because Phase 1 is tractable while ordinary Phase 2
visits many infeasible designs without finding an UB in a short run.

| Method | Completed P2 rounds | First UB | Last LB | Last valid gap |
| --- | ---: | ---: | ---: | ---: |
| Ordinary monotone cut | 65 | none | 9310.329 | none |
| Battery search, 30 probes per scenario | 3 | 168.2 s | 9293.015 | 0.7220% |
| One battery boundary | 10 | 49.5 s | 9297.741 | 0.6714% |
| One battery boundary, then one facility boundary | 9 | 54.2 s | 9297.373 | 0.6753% |

The shallow battery search obtains a valid UB much earlier than the deep
search. The subsequent facility search executes, but accepts no additional
facility on this case: every logged strengthened anchor is identical to the
battery-only anchor. It therefore adds probe overhead without strengthening the
cut. The ordinary method advances the LB further because it completes many more
iterations, but it still has no valid UB at the time limit.

## Current Assessment

1. Full hierarchical maximal-anchor search is mathematically valid but too
   expensive for routine use.
2. Facility repair should remain disabled by default. Battery-then-facility
   search is retained for ablation, but the medium diagnostic accepted no
   additional facility and delayed the first UB.
3. A strict probe cap below four is too small for the current prefix search and
   generally returns the ordinary anchor.
4. With RSP enabled, a one-level battery step is the best current cut-oriented
   repair. It adds one certified resource increment with one Exact SP probe and
   does not materially reduce the number of Master iterations completed.
5. Prefix-boundary repair can produce an UB, but currently behaves as an
   expensive primal heuristic rather than an efficient cut-strengthening step.
6. No repair variant has yet shown a robust total-time advantage. It must remain
   experimental and uncommitted.

## Verification

- Direct Ninja Release build: passed.
- `git diff --check`: passed.
- Baseline and repair smoke tests: passed.
- Final objectives agree across compared variants.
- The three-scenario EF, Lifted Two-Phase, and Full runs all return objective
  `1606.794953` on `tower_small / cap1 / dwc-top-n=1`.
- The stable COPT rewrite and Exact SP cache are committed; only repair code,
  repair options, this note, and the user-owned `.vscode/` remain outside the
  stable commit chain.
