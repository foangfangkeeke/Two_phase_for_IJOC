"""Recompute the R28-C4 planning comparison from exported solution CSVs."""

import csv
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def read_rows(case, name):
    with (ROOT / case / name).open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def one_row(case, name):
    rows = read_rows(case, name)
    assert len(rows) == 1
    return rows[0]


off_case = "r28_c4_dwc_off"
on_case = "r28_c4_dwc_on"
off = one_row(off_case, "summary.csv")
on = one_row(on_case, "summary.csv")
off_routes = read_rows(off_case, "route_energy.csv")
on_routes = read_rows(on_case, "route_energy.csv")
assert len(off_routes) == len(on_routes) == 140

for case, summary, routes in ((off_case, off, off_routes), (on_case, on, on_routes)):
    assert abs(float(summary["difference"])) < 1e-8
    for field, summary_field in (
        ("plug_in_kwh", "expected_plug_in_kwh"),
        ("dwc_kwh", "expected_dwc_kwh"),
        ("overnight_kwh", "expected_overnight_kwh"),
    ):
        weighted = sum(float(row["probability"]) * float(row[field]) for row in routes)
        assert abs(weighted - float(summary[summary_field])) < 1e-6, (case, field)

patterns = Counter()
routes_with_no_dwc_and_smaller_battery = set()
route_detail = defaultdict(lambda: {"dwc": 0.0, "battery_drop": 0.0})
for a, b in zip(off_routes, on_routes):
    assert (a["scenario"], a["route_name"]) == (b["scenario"], b["route_name"])
    dwc_used = float(b["dwc_kwh"]) > 0.001
    battery_drop = int(a["capacity_kwh"]) - int(b["capacity_kwh"])
    assert battery_drop >= 0
    patterns[(dwc_used, battery_drop > 0)] += 1
    if not dwc_used and battery_drop > 0:
        routes_with_no_dwc_and_smaller_battery.add(b["route_name"])
        plug_change = float(b["plug_in_kwh"]) - float(a["plug_in_kwh"])
        night_change = float(b["overnight_kwh"]) - float(a["overnight_kwh"])
        assert plug_change > 0 and abs(plug_change + night_change) < 1e-6
    route_detail[b["route_name"]]["dwc"] += float(b["probability"]) * float(b["dwc_kwh"])
    route_detail[b["route_name"]]["battery_drop"] += float(b["probability"]) * battery_drop

lo = 18011.807813
uo = float(off["reported_objective"])
ld = 17425.712220
ud = float(on["reported_objective"])
print("incumbent saving:", round(uo - ud, 6))
print("certified absolute saving interval:", round(lo - ud, 6), round(uo - ld, 6))
print("certified percentage interval:", round(100 * (1 - ud / lo), 4), round(100 * (1 - ld / uo), 4))
print("DWC use / battery drop cross-tab:", dict(sorted(patterns.items())))
print("smaller batteries without direct DWC use:", sorted(routes_with_no_dwc_and_smaller_battery))
for name in ("41", "66"):
    print("route", name, route_detail[name])
