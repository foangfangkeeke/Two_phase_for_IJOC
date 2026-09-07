# DWC Inter-Trip Travel-Time Correction

## Status

Confirmed data-generation issue. The C++ model can keep its current
arrival/departure-time semantics if the generated trip-chain data records
chain-dependent first-stop arrivals for noninitial trips.

The generator has been updated outside this repository:

```text
C:\faq_work\Tools\all_you_need_is_selenium\handle\caseGeneration.py
```

Tower Transit scenario data in this repository still needs to be regenerated
from the corrected generator before rerunning representative solutions and
Chapter 5 experiments.

## Confirmed Issue

For an in-service arc within a trip, the solver computes the DWC charging upper
bound from the departure time at the current stop and the arrival time at the
next stop. This is consistent with the generated station schedule.

For an arc connecting two consecutive trips assigned to the same bus, the same
calculation is currently used:

```text
travel_time = next_trip_first_arrival - previous_trip_last_departure
dwc_charge_upper_bound = dwc_rate * travel_time
```

The generated first-stop arrival time was the timetable start time.
Consequently, `travel_time` includes both the deadhead movement and the idle
time before the next scheduled departure. The solver therefore treats the full
inter-trip interval as time spent traversing the DWC link.

The inconsistency is especially clear because traction energy on the same
connector is calculated from its physical link distance. For example, link
`178 -> 476` has a normalized distance of 0.5 km and therefore a one-minute
travel time at the generator's assumed speed of 30 km/h, while some generated
trip chains expose inter-trip intervals of several hours.

## Affected Solver Locations With Old Data

With the old generated data, the same arrival/departure-time calculation affects
all operational formulations:

- `src/problem/wireless_reform/wireless_reform.cpp`: Phase 1 resource
  preprocessing near lines 765--785.
- `src/problem/wireless_reform/wireless_reform.cpp`: exact scenario MIP DWC
  upper bounds near lines 1384--1424.
- `src/problem/wireless_reform/wireless_reform_route.cpp`: route-level LP DWC
  upper bounds near lines 860--897.
- `src/problem/wireless/wireless.cpp`: original formulation DWC upper bounds
  near lines 1011--1047.

The data generator is outside this repository:

```text
C:\faq_work\Tools\all_you_need_is_selenium\handle\caseGeneration.py
```

Relevant previous generator behavior:

- `trip.start_time` is read from the route timetable.
- `generate_detail_trips()` set first arrival to `trip.start_time`
  and first departure to `trip.start_time` plus a fixed dwell time.
- Trip-chain feasibility correctly includes the connector travel time through
  `ready_time = previous_trip.end_time + connect_time`.
- The output step did not revise first-stop arrivals after trip chains were
  fixed.

## Required Time Semantics

Preserve the generated first-stop departure time as the scheduled departure:

```text
departure[next_trip, first_stop] = generated departure[next_trip, first_stop]
```

For every noninitial trip in a bus duty, derive its first-stop arrival from the
preceding trip and the physical connector travel time:

```text
arrival[next_trip, first_stop]
    = departure[previous_trip, last_stop] + connector_travel_time
```

The residual interval is the terminal layover:

```text
layover_time
    = departure[next_trip, first_stop] - arrival[next_trip, first_stop]
```

This construction assigns the two resources correctly:

- DWC on the connector is bounded by `connector_travel_time` only.
- Plug-in charging or SWC at the next trip's first stop may use the resulting
  layover time, subject to the existing facility and occupancy constraints.

The adopted convention is immediate deadheading: after completing one trip, the
bus immediately travels to the next trip's first stop and any residual slack is
modeled as layover at that first stop. This is consistent with the generated
chain filtering, which already enforces connector-time feasibility.

The first trip of a bus duty retains the existing timetable first-stop
arrival/departure and depot-to-first-stop energy accounting.

## Implementation Plan for the `coding` Branch

1. Keep `generate_detail_trips()` responsible for timetable-based absolute
   trip times.
2. Complete trip-chain assignment.
3. After chains are fixed and before writing `tripArrTimes.txt`, revise each
   noninitial trip's first-stop arrival:
   `arrival[next, first] = departure[previous, last] + connector_time`.
4. Preserve the original first-stop departure time, so the residual interval is
   available as first-stop layover.
5. Regenerate every Tower Transit scenario dataset.
6. Rerun the representative no-WPT/WPT solutions and all Chapter 5 performance
   experiments because facility selections, battery choices, objectives, and
   runtimes may change.

No C++ model change is required under the immediate-deadheading convention:
the existing DWC constraint continues to use
`next_arrival - current_departure`, and the corrected data makes that interval
equal to connector traversal time for inter-trip arcs.

## Required Validation

- For every consecutive trip pair in a duty, verify
  `previous_last_departure + connector_travel_time <= next_first_departure`.
- Verify that the generated next-trip first arrival equals the left-hand side
  above and that the difference to first departure is nonnegative layover.
- Verify that each DWC upper bound equals rate times physical traversal time.
- Verify that a 0.5-km connector at 30 km/h receives a one-minute charging
  window, regardless of the scheduled headway between trips.
- Check Phase 1, route-level LP, and exact Phase 2 models against the same test
  duty and confirm identical connector-time semantics.
- Recheck SWC and plug-in charging windows at terminal visits after the time
  reconstruction.

## Temporary Reporting Rule

Until the correction is implemented and all affected cases are rerun, any
quantity derived from the existing DWC time constraint must be labeled
**charging upper bound in the current solver model**. It must not be described
as actual energy received on a trip, physically available DWC energy, or a
route's realizable replenishment capacity.

Do not use this temporary quantity to compare SWC and DWC effectiveness or to
support physical interpretations. It may only be used as a descriptive model
bound, accompanied by the qualification that inter-trip connector bounds may
include layover time in the current data representation.
