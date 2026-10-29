# RSP-Guided Facility Prefilter for Repair-Based Monotone Cuts

## 1. Scope

The Full algorithm starts from a Master design that has been strictly proved
infeasible for a scenario. It enlarges that design while preserving a strict
infeasibility certificate, then builds the existing monotone feasibility cut
from the enlarged anchor.

The repair keeps the two resource types separate:

1. scenario-indexed battery capacities are repaired with zero-objective Exact
   SP probes;
2. shared binary facilities are first filtered by the current RSP Farkas cut,
   then handled by the existing shallow zero-objective Exact search.

No new optimization model is introduced. The RSP cut is used only as a valid
analytical infeasibility certificate for selected facility prefixes.

## 2. Complete design used by the RSP certificate

For an RSP-infeasible scenario, let the valid feasibility cut be

\[
H_\omega(w,z)=\beta_0+\sum_f\beta_f w_f+
\sum_r\sum_\ell\gamma_{r\ell}z_{\omega r\ell}\ge 0.
\]

After battery repair, the facility prefilter evaluates this cut at a complete
design. That design contains both:

- the repaired one-hot battery choices of the current scenario; and
- every facility already selected by the current Master candidate.

Thus an existing facility contribution is never omitted when the residual RSP
deficit is calculated. Only relevant facilities that are currently unbuilt
are candidates for addition.

## 3. Battery stage

The battery stage retains the deterministic incremental-cost order and the
existing coarse search. Every tested battery configuration is classified by
the zero-objective Exact SP as:

- strictly proven infeasible;
- feasible because an integer operating solution is available; or
- unknown.

The RSP cut does not replace any battery Exact probe. This keeps the
multi-level one-hot battery decisions outside the facility prefilter.

## 4. Facility prefilter

Let `base` be the strictly Exact-infeasible design after battery repair,
including its already selected facilities. If `base` still strictly violates
the old RSP cut, the relevant unbuilt facilities are ordered by:

1. increasing RSP coefficient `beta_f`;
2. increasing construction cost for equal coefficients;
3. facility index.

Starting from `base`, facilities are added one at a time in this order. The
full affine value `H_omega(w,z)` is recomputed for every prefix. Every prefix
that remains strictly below zero is RSP-infeasible and therefore also
Exact-infeasible; it is accepted into the strengthened anchor without an Exact
solve.

The first facility whose addition makes the old cut no longer strictly
violated is the boundary facility. It is **not** inserted into the infeasible
anchor. Instead, it becomes the first candidate of the subsequent Exact
search. The other deferred facilities remain behind it in the same increasing
RSP-coefficient order.

This is equivalent to pre-excluding the facilities that cannot be certified
for free, while preserving the boundary identified by the RSP cut. It does
not remove any facility from the Master and does not claim that a deferred
facility makes the Exact SP feasible.

If the battery-repaired `base` no longer violates the old RSP cut, or no valid
Farkas coefficients are available, the prefilter accepts nothing and the
facility stage uses the original construction-cost order.

## 5. Shallow Exact facility search

Starting from the last certified infeasible anchor, the deferred list is
tested with the original nested prefix schedule

\[
1,2,4,8,\ldots.
\]

The first prefix contains exactly the RSP boundary facility when a boundary
exists. Larger prefixes add the following facilities in increasing
RSP-coefficient order. Each prefix is solved by the zero-objective Exact SP:

- a proven-infeasible prefix replaces the current anchor;
- the first feasible prefix is retained as a repair endpoint and stops the
  facility search;
- an unknown result safely stops the repair and returns the last strictly
  certified infeasible anchor.

The method deliberately does not scan later feasibility boundaries and does
not claim an inclusion-wise maximal anchor. Its purpose is to enlarge the
anchor cheaply without replacing the bounded shallow search that controls
repair time.

## 6. Shared facilities and repair UB

RSP coefficients and certified prefixes are scenario-specific, but facilities
are shared. Scenario repair endpoints are therefore never stitched by adding
their objective values. For a repair UB candidate, the implementation:

1. takes the union of all facilities requested by the scenario endpoints;
2. retains each scenario's repaired battery choices;
3. validates every scenario under that one shared layout with its original
   objective Exact SP; and
4. updates the official UB only when all required integer-feasibility and
   objective evidence is available.

## 7. Safety and diagnostics

- Only a strict RSP-cut violation or an Exact infeasibility status can enlarge
  the infeasible anchor.
- A timeout without an incumbent, numerical failure, or other unknown status
  is never treated as infeasible.
- Exact probes receive the remaining global time, and resource bounds and the
  original objective are restored on normal and exceptional exits.
- `battery_probes` and `facility_probes` count zero-objective Exact solves.
- `rsp_cert_checks` and `rsp_cert_hits` count affine cut evaluations and strict
  violations.
- `rsp_certified_facilities` counts facilities accepted without Exact probes.
- `rsp_deferred_facilities` records the boundary-plus-tail list handed to the
  shallow Exact facility search.
