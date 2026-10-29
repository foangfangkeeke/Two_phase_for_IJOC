# Cost-Guided Facility Repair with RSP Certification

## 1. Purpose

The Full algorithm strengthens a monotone feasibility cut by replacing the
current infeasible Master design with a component-wise larger design that is
still strictly certified infeasible. The repair keeps the existing two-stage
structure:

1. repair scenario-indexed battery capacities with zero-objective Exact SP;
2. repair shared facilities in increasing construction-cost order.

An RSP Farkas cut is used only to certify cost-ordered facility prefixes
without additional Exact solves. Its coefficients do not define the facility
search order.

## 2. Why construction cost defines the order

For an infeasible anchor, every relevant facility that is still zero appears
in the monotone feasibility cut with unit coefficient. The Master can satisfy
that cut by choosing one available resource improvement, and its objective
therefore favors low-cost facilities.

Prioritizing low-cost facilities in the repair attempts to absorb the Master's
cheapest escape directions into the infeasible anchor. This is different from
ordering by Farkas coefficient, which favors a large-cardinality certified
prefix but need not target the resource choices preferred by the Master.

The RSP cut remains useful as a validity oracle. For an RSP-infeasible
scenario, let

\[
H_\omega(w,z)=\beta_0+\sum_f\beta_f w_f+
\sum_r\sum_\ell\gamma_{r\ell}z_{\omega r\ell}\ge 0
\]

be its globally valid feasibility cut. Any complete design with
`H_omega(w,z) < 0` is also infeasible for the Exact SP.

## 3. Battery stage

Battery repair is unchanged. Routes are ordered by the incremental cost of a
one-level capacity increase. The zero-objective Exact SP tests one complete
layer and, when that layer is feasible, the nested prefix sequence
`R, R/2, R/4, ...` until the first proven-infeasible prefix is found.

The RSP cut never replaces a battery Exact probe. This keeps the one-hot,
multi-level battery decisions separate from the binary facility stage.

## 4. Cost-ordered RSP facility prefilter

After battery repair, `base` is a complete design containing:

- the repaired battery choices for the current scenario; and
- every shared facility already selected by the current Master candidate.

All facilities are ordered once, before Phase 1, by increasing construction
cost, with facility index as the deterministic tie-break. At each Master
candidate, the already built facilities are removed from that global order.
Every scenario therefore searches the same remaining sequence. Facilities
irrelevant to one scenario do not cause duplicate Exact work because the
existing scenario-state cache ignores them.

If `base` still strictly violates the current RSP feasibility cut, facilities
are added one at a time in that cost order. The complete affine cut value is
recomputed for every prefix:

- every prefix that remains strictly violated is accepted into the infeasible
  anchor without an Exact solve;
- the first prefix that is no longer certified identifies the boundary
  facility;
- that boundary facility is not inserted into the anchor without Exact
  evidence.

If all facilities remain certified, the facility stage finishes immediately.
If the battery-repaired base no longer violates the old RSP cut, or no valid
Farkas coefficients are available, no facility is accepted analytically and
the original cost-ordered Exact search is used.

## 5. Shallow Exact facility search

The deferred list begins with the first non-certified boundary facility and
retains the same construction-cost order. Starting from the last strictly
infeasible anchor, the zero-objective Exact SP tests nested prefixes

\[
1,2,4,8,\ldots.
\]

- A proven-infeasible prefix replaces the anchor.
- The first feasible prefix is saved as a repair endpoint and stops the
  facility search.
- An unknown result stops safely and returns the last strictly certified
  infeasible anchor.

The procedure is intentionally shallow. It does not scan later boundaries and
does not claim an inclusion-wise maximal infeasible design.

## 6. Shared facilities and repair UB

Facility repairs are scenario-specific feasibility checks, but their facility
endpoints are prefixes of the same global cost order. Their component-wise
union is therefore exactly the longest requested shared prefix. A repair UB
combines that one facility prefix with each scenario's own battery choices and
validates every scenario under the common layout with the original-objective
Exact SP. Scenario objective values from different facility layouts are never
stitched together.

## 7. Safety and diagnostics

- Only a strict RSP-cut violation or an Exact infeasibility status can enlarge
  the infeasible anchor.
- An Exact incumbent is sufficient to classify a zero-objective probe as
  feasible.
- Timeout without an incumbent, numerical failure, and other unknown statuses
  never certify infeasibility.
- The original Exact objective and all resource bounds are restored on normal
  and exceptional exits.
- `battery_probes` and `facility_probes` count zero-objective Exact solves.
- `rsp_cert_checks` and `rsp_cert_hits` count affine certificate evaluations.
- `rsp_certified_facilities` counts cost-ordered facilities accepted without
  Exact probes.
- `rsp_deferred_facilities` records the boundary-plus-tail list passed to the
  shallow Exact search.
