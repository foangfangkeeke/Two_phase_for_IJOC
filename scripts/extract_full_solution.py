#!/usr/bin/env python3
"""Extract manuscript-facing solution tables from full-method raw variable dumps.

Usage:
    python scripts/extract_full_solution.py results/tower_data/full_solution

The C++ solver exports raw Gurobi variables. This script performs the
interpretive parsing used for Section 5.2, so manuscript tables can evolve
without changing the solver.
"""

from __future__ import annotations

import argparse
import csv
import re
from collections import defaultdict
from pathlib import Path


VALUE_TOL = 1e-7

BATTERY_RE = re.compile(r"^sc(?P<scenario>\d+)_E_r(?P<route>\d+)_cap(?P<capacity>\d+)$")
SWC_RE = re.compile(r"^w_station_s(?P<station>\d+)$")
DWC_RE = re.compile(r"^w_link_l(?P<from_station>\d+)To(?P<to_station>\d+)$")
SCENARIO_RE = re.compile(r"^sc(?P<scenario>\d+)_")
STATION_RE = re.compile(r"_s(?P<station>\d+)")
TIME_RE = re.compile(r"_t(?P<time>\d+)")


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def fnum(row: dict[str, str], key: str) -> float:
    return float(row[key])


def selected(row: dict[str, str]) -> bool:
    return abs(fnum(row, "value")) > VALUE_TOL


def classify_master(name: str) -> tuple[str, dict[str, object]]:
    if m := SWC_RE.match(name):
        return "swc", {"station": int(m.group("station"))}
    if m := DWC_RE.match(name):
        return "dwc", {
            "from_station": int(m.group("from_station")),
            "to_station": int(m.group("to_station")),
        }
    if m := BATTERY_RE.match(name):
        return "battery", {
            "scenario": int(m.group("scenario")),
            "route": int(m.group("route")),
            "capacity": int(m.group("capacity")),
        }
    return "other", {}


def parse_scenario(name: str, fallback: str) -> int:
    if m := SCENARIO_RE.match(name):
        return int(m.group("scenario"))
    return int(fallback)


def parse_station(name: str) -> int | None:
    if m := STATION_RE.search(name):
        return int(m.group("station"))
    return None


def parse_time(name: str) -> int | None:
    if m := TIME_RE.search(name):
        return int(m.group("time"))
    return None


def charge_class(name: str, plugin_station_keys: set[tuple[int, int]]) -> str:
    if "_c_Night_b" in name:
        return "night"
    if "_c_b" not in name:
        return "other"
    if "To_n" in name:
        return "dwc_link"

    scenario_match = SCENARIO_RE.match(name)
    station = parse_station(name)
    if scenario_match and station is not None:
        key = (int(scenario_match.group("scenario")), station)
        if key in plugin_station_keys:
            return "station_with_plugin_occupancy"
    return "station_without_plugin_occupancy"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "solution_dir",
        type=Path,
        help="Directory containing master_variables.csv and subproblem_variables.csv.",
    )
    args = parser.parse_args()

    solution_dir = args.solution_dir
    master_rows = read_csv(solution_dir / "master_variables.csv")
    sub_rows = read_csv(solution_dir / "subproblem_variables.csv")
    metadata_rows = read_csv(solution_dir / "solution_metadata.csv")
    sub_status_rows = read_csv(solution_dir / "subproblem_status.csv")
    metadata = {row["metric"]: row["value"] for row in metadata_rows}

    selected_master = []
    selected_facilities = []
    selected_batteries = []

    for row in master_rows:
        if not selected(row):
            continue

        kind, parsed = classify_master(row["var_name"])
        base = {
            "var_name": row["var_name"],
            "type": kind,
            "value": row["value"],
            "obj": row["obj"],
            "cost": fnum(row, "value") * fnum(row, "obj"),
        }
        selected_master.append(base)

        if kind in {"swc", "dwc"}:
            selected_facilities.append(base | parsed)
        elif kind == "battery":
            selected_batteries.append(base | parsed)

    write_csv(
        solution_dir / "selected_master_variables.csv",
        ["var_name", "type", "value", "obj", "cost"],
        selected_master,
    )
    write_csv(
        solution_dir / "selected_facilities.csv",
        [
            "var_name",
            "type",
            "value",
            "obj",
            "cost",
            "station",
            "from_station",
            "to_station",
        ],
        selected_facilities,
    )
    write_csv(
        solution_dir / "selected_batteries.csv",
        ["var_name", "type", "value", "obj", "cost", "scenario", "route", "capacity"],
        selected_batteries,
    )

    charging_cost = sum(float(row["obj"]) for row in sub_status_rows)
    facility_cost = sum(float(row["cost"]) for row in selected_facilities)
    battery_cost = sum(float(row["cost"]) for row in selected_batteries)
    solution_summary = [
        {"metric": "best_upper_bound", "value": metadata["best_upper_bound"]},
        {"metric": "best_lower_bound", "value": metadata["best_lower_bound"]},
        {"metric": "facility_cost", "value": facility_cost},
        {"metric": "battery_cost", "value": battery_cost},
        {"metric": "charging_cost", "value": charging_cost},
        {"metric": "swc_count", "value": sum(1 for row in selected_facilities if row["type"] == "swc")},
        {"metric": "dwc_count", "value": sum(1 for row in selected_facilities if row["type"] == "dwc")},
        {"metric": "selected_facility_count", "value": len(selected_facilities)},
    ]
    write_csv(solution_dir / "solution_summary.csv", ["metric", "value"], solution_summary)

    plugin_station_keys: set[tuple[int, int]] = set()
    occupancy_by_station_time: dict[tuple[int, int, int], float] = defaultdict(float)
    occupancy_by_station: dict[tuple[int, int], dict[str, float]] = defaultdict(
        lambda: {"occupied_slots": 0.0, "peak_occupancy": 0.0}
    )

    for row in sub_rows:
        if not selected(row):
            continue
        name = row["var_name"]
        if "_x_b" not in name:
            continue

        scenario = parse_scenario(name, row["scenario_index"])
        station = parse_station(name)
        time = parse_time(name)
        if station is None or time is None:
            continue

        value = fnum(row, "value")
        plugin_station_keys.add((scenario, station))
        occupancy_by_station_time[(scenario, station, time)] += value
        occupancy_by_station[(scenario, station)]["occupied_slots"] += value

    for (scenario, station, _time), occ in occupancy_by_station_time.items():
        item = occupancy_by_station[(scenario, station)]
        item["peak_occupancy"] = max(item["peak_occupancy"], occ)

    plugin_rows = [
        {
            "scenario": scenario,
            "station": station,
            "occupied_slots": values["occupied_slots"],
            "peak_occupancy": values["peak_occupancy"],
        }
        for (scenario, station), values in sorted(occupancy_by_station.items())
    ]
    write_csv(
        solution_dir / "plugin_occupancy_by_station.csv",
        ["scenario", "station", "occupied_slots", "peak_occupancy"],
        plugin_rows,
    )

    charge_by_scenario: dict[int, dict[str, float]] = defaultdict(
        lambda: defaultdict(float)
    )
    charge_by_variable = []

    for row in sub_rows:
        if not selected(row):
            continue
        name = row["var_name"]
        if "_c" not in name:
            continue

        scenario = parse_scenario(name, row["scenario_index"])
        kind = charge_class(name, plugin_station_keys)
        value = fnum(row, "value")
        cost = value * fnum(row, "obj")
        charge_by_scenario[scenario][f"{kind}_kwh"] += value
        charge_by_scenario[scenario][f"{kind}_cost"] += cost
        charge_by_variable.append(
            {
                "scenario": scenario,
                "var_name": name,
                "charge_class": kind,
                "value": value,
                "obj": row["obj"],
                "cost": cost,
            }
        )

    charge_fields = [
        "scenario",
        "night_kwh",
        "night_cost",
        "dwc_link_kwh",
        "dwc_link_cost",
        "station_with_plugin_occupancy_kwh",
        "station_with_plugin_occupancy_cost",
        "station_without_plugin_occupancy_kwh",
        "station_without_plugin_occupancy_cost",
        "other_kwh",
        "other_cost",
    ]
    charge_rows = []
    for scenario, values in sorted(charge_by_scenario.items()):
        row = {"scenario": scenario}
        for field in charge_fields[1:]:
            row[field] = values.get(field, 0.0)
        charge_rows.append(row)

    write_csv(solution_dir / "charging_by_scenario.csv", charge_fields, charge_rows)
    write_csv(
        solution_dir / "charge_variables_classified.csv",
        ["scenario", "var_name", "charge_class", "value", "obj", "cost"],
        charge_by_variable,
    )


if __name__ == "__main__":
    main()
