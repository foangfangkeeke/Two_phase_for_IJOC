"""Export Section 5.3 tables from retained logs without launching a solver.

Usage: python scripts/analysis/export_section53.py
"""

import argparse
import csv
import hashlib
import json
import math
import re
from collections import defaultdict
from datetime import datetime, timezone
from decimal import Decimal
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "experiments/section53_2026-11-09"
OUTPUT = PACKAGE / "exports"
COST_TOLERANCE = 0.001
ENERGY_TOLERANCE_KWH = 0.001
FACTORS = ("0p5", "0p75", "1", "1p25", "1p5")
EXPECTED_IDS = {
    "C4_DWC", "C4_noDWC_EF",
    *(f"C4_DWCcost{factor}" for factor in FACTORS if factor != "1"),
    *(f"C4_battery{factor}_{arm}" for factor in FACTORS if factor != "1"
      for arm in ("DWC", "noDWC_EF")),
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def field(line: str, key: str) -> str:
    match = re.search(rf"(?:^|[, ]){re.escape(key)}=([^, ]+)", line)
    if match is None:
        raise ValueError(f"Missing {key}: {line[:180]}")
    return match.group(1)


def json_records(lines: list[str], label: str) -> list[dict]:
    prefix = label + ": "
    return [json.loads(line[len(prefix):]) for line in lines if line.startswith(prefix)]


def one(records: list[dict], label: str) -> dict:
    if len(records) != 1:
        raise ValueError(f"Expected exactly one {label}, got {len(records)}")
    return records[0]


def parse_plan(path: Path) -> dict:
    header = {}
    facilities = []
    batteries = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split()
        if parts[0] == "facility":
            facilities.append(parts[1])
        elif parts[0] == "battery":
            key = (int(parts[1]), int(parts[2]))
            if key in batteries:
                raise ValueError(f"Duplicate plan battery {key}: {path}")
            batteries[key] = int(parts[3])
        else:
            if parts[0] in header:
                raise ValueError(f"Duplicate plan header {parts[0]}: {path}")
            header[parts[0]] = parts[1]
    if len(set(facilities)) != len(facilities) or len(batteries) != 140:
        raise ValueError(f"Incomplete fixed design: {path}")
    return {"header": header, "facilities": facilities, "batteries": batteries}


def parse_optimization(path: Path) -> dict:
    lines = path.read_text(encoding="utf-8").splitlines()
    runtime = next(line for line in lines if line.startswith("Runtime options: "))
    instance = next(line for line in lines if line.startswith("Instance: "))
    terminal = [line for line in lines if line.startswith("Algorithm termination: ")]
    if len(terminal) != 1:
        raise ValueError(f"Expected one final termination: {path}")
    master = None
    incumbent = None
    best = None
    for line in lines:
        if line.startswith("Search master: "):
            event = json.loads(line.split(": ", 1)[1])
            if event.get("has_solution"):
                master = event["design"]
        elif line.startswith("Search incumbent: "):
            incumbent = json.loads(line.split(": ", 1)[1])["design"]
        elif line.startswith("Incumbent cost distribution: "):
            kind = field(line, "source")
            if kind == "master":
                design = master
            elif kind in ("repair-exact", "repair-rsp", "ef"):
                design = incumbent
            else:
                raise ValueError(f"Unknown costed design source: {kind}")
            if design is None:
                raise ValueError(f"Costed record lacks an associated design: {path}")
            value = float(field(line, "total"))
            if best is None or value < best["total"]:
                best = {"total": value, "kind": kind, "design": design,
                        "line": line}
    if best is None:
        raise ValueError(f"No costed incumbent: {path}")
    final = terminal[0]
    if field(final, "status") != "OPTIMAL" or field(final, "has_solution") != "1":
        raise ValueError(f"Incomplete selected optimization: {path}")
    lb, ub = float(field(final, "LB")), float(field(final, "UB"))
    if abs(best["total"] - ub) > COST_TOLERANCE:
        raise ValueError(f"Final UB differs from selected costed incumbent: {path}")
    components = None
    if field(final, "algorithm") == "full":
        components = {
            "facility_cost": float(field(best["line"], "facility")),
            "battery_cost": float(field(best["line"], "battery")),
            "day_energy_cost": float(field(best["line"], "daytime_energy")),
            "night_energy_cost": float(field(best["line"], "overnight_energy")),
        }
        if abs(sum(components.values()) - ub) > COST_TOLERANCE:
            raise ValueError(f"Full incumbent components do not sum to UB: {path}")
    return {"runtime": runtime, "instance": instance, "terminal": final,
            "lb": lb, "ub": ub, "gap_percent": float(field(final, "gap_percent")),
            "status": field(final, "status"),
            "algorithm": field(final, "algorithm"), "best": best,
            "components": components}


def parse_audit(path: Path) -> dict:
    lines = path.read_text(encoding="utf-8").splitlines()
    source = one(json_records(lines, "Plan audit source"), "audit source")
    total = one(json_records(lines, "Plan audit total"), "audit total")
    scenarios = json_records(lines, "Plan audit scenario")
    routes = json_records(lines, "Plan audit route")
    stations = json_records(lines, "Plan audit station")
    facilities = json_records(lines, "Plan audit facility")
    statuses = [line for line in lines if line.startswith("Plan audit status: ")]
    if statuses != ["Plan audit status: COMPLETE_OPTIMAL_RECOURSE"]:
        raise ValueError(f"Incomplete fixed-design audit: {path}")
    if len(scenarios) != 5 or len(routes) != 140 or len(stations) != 25:
        raise ValueError(f"Incomplete audit detail: {path}")
    if any(row["status"] != "OPTIMAL" for row in scenarios):
        raise ValueError(f"Nonoptimal scenario audit: {path}")
    if abs(sum(row["probability"] for row in scenarios) - 1.0) > 1e-6:
        raise ValueError(f"Scenario probabilities do not sum to one: {path}")
    if abs(sum(total[key] for key in ("facility_cost", "battery_cost",
            "day_energy_cost", "night_energy_cost")) - total["total"]) > 1e-5:
        raise ValueError(f"Audit cost components do not sum to total: {path}")
    audit_bound = total["facility_cost"] + sum(row["bound"] for row in scenarios)
    objective = total["facility_cost"] + sum(row["objective"] for row in scenarios)
    if abs(objective - total["total"]) > 1e-5:
        raise ValueError(f"Audit scenario objectives do not reconcile: {path}")
    if audit_bound > objective + 1e-5:
        raise ValueError(f"Audit bound exceeds feasible objective: {path}")
    return {"source": source, "total": total, "scenarios": scenarios,
            "routes": routes, "stations": stations, "facilities": facilities,
            "bound": audit_bound}


def verify_variant(name: str, factor: str, kind: str) -> None:
    base = ROOT / "data/R28_N5_data"
    variant = ROOT / "data" / f"{name}_data"
    record = json.loads((variant / "cost_case_manifest.json").read_text(
        encoding="utf-8"))
    if record["data_name"] != name or record["kind"] != kind or \
            float(record["factor"]) != float(factor) or \
            record["changed_files"] != ["globalCfg.txt"] or \
            digest(base / "globalCfg.txt") != record["base_global_sha256"] or \
            digest(variant / "globalCfg.txt") != record["variant_global_sha256"]:
        raise ValueError(f"Variant manifest mismatch: {name}")
    files = [path for path in base.rglob("*") if path.is_file() and
             path.name != "globalCfg.txt"]
    if len(files) != record["other_file_count_unchanged"]:
        raise ValueError(f"Variant file count mismatch: {name}")
    for path in files:
        if digest(path) != digest(variant / path.relative_to(base)):
            raise ValueError(f"Unexpected variant input change: {name}, {path}")
    before = (base / "globalCfg.txt").read_text(encoding="utf-8").splitlines()
    after = (variant / "globalCfg.txt").read_text(encoding="utf-8").splitlines()
    label, changed_columns = (("# priceOfUnitCharger", (2, 3)) if kind == "dwc"
                              else ("# priceOfPowerDay", (2,)))
    targets = [index + 1 for index, line in enumerate(before)
               if line.startswith(label)]
    if len(targets) != 1 or len(before) != len(after):
        raise ValueError(f"Variant cost row mismatch: {name}")
    target = targets[0]
    if any(old != new for index, (old, new) in enumerate(zip(before, after))
           if index != target):
        raise ValueError(f"Extra cost row changed: {name}")
    original = before[target].split(",")
    revised = after[target].split(",")
    if len(original) != len(revised):
        raise ValueError(f"Variant cost column count mismatch: {name}")
    for index, (old, new) in enumerate(zip(original, revised)):
        if index in changed_columns:
            if Decimal(new) != Decimal(old) * Decimal(factor):
                raise ValueError(f"Wrong cost factor: {name}, column {index}")
        elif old != new:
            raise ValueError(f"Unrequested cost field changed: {name}, {index}")


def load_run(item: dict) -> dict:
    plan_path = ROOT / item["plan"]
    raw_path = ROOT / item["optimization_log"]
    audit_path = ROOT / item["audit_log"]
    for label, path in (("plan", plan_path), ("optimization_log", raw_path),
                        ("audit_log", audit_path)):
        if digest(path) != item[label + "_sha256"]:
            raise ValueError(f"Published hash mismatch: {path}")
    plan = parse_plan(plan_path)
    opt = parse_optimization(raw_path)
    audit = parse_audit(audit_path)
    header = plan["header"]
    if header["data"] != item["data_name"] or int(header["scenarios"]) != 5 or \
            int(header["capacity"]) != 4 or int(header["candidate_count"]) != 50 or \
            int(header["battery_step"]) != 50 or \
            int(header["wireless"]) != item["wireless"] or \
            header["source_log_sha256"] != item["optimization_log_sha256"] or \
            abs(float(header["source_ub"]) - opt["ub"]) > 1e-5:
        raise ValueError(f"Plan header mismatch: {item['id']}")
    runtime = opt["runtime"]
    for key, expected in (("scenario_count", 5), ("scenario_start", 0),
                          ("cap", 4), ("dwc_top_n", 50),
                          ("battery_step", 50),
                          ("wireless_option", item["wireless"])):
        if int(field(runtime, key)) != expected:
            raise ValueError(f"Runtime option mismatch: {item['id']}, {key}")
    if field(opt["instance"], "data") != item["data_name"] or \
            opt["algorithm"] != ("full" if item["wireless"] else "ef"):
        raise ValueError(f"Selected case mismatch: {item['id']}")
    selected = opt["best"]["design"]
    by_scenario = [[plan["batteries"][(scenario, route)] for route in range(28)]
                   for scenario in range(5)]
    if set(selected["facilities"]) != set(plan["facilities"]) or \
            selected["batteries"] != by_scenario:
        raise ValueError(f"Plan is not the final costed incumbent: {item['id']}")
    if audit["source"]["sha256"] != item["optimization_log_sha256"] or \
            abs(audit["source"]["source_ub"] - opt["ub"]) > 1e-5 or \
            set(row["name"] for row in audit["facilities"]) != \
            set(plan["facilities"]):
        raise ValueError(f"Audit source mismatch: {item['id']}")
    route_keys = set()
    for route in audit["routes"]:
        key = (route["scenario"], route["route_index"])
        if key in route_keys or route["battery_kwh"] != plan["batteries"][key] or \
                route["buses"] <= 0:
            raise ValueError(f"Audit route/design mismatch: {item['id']}, {key}")
        route_keys.add(key)
    if route_keys != set(plan["batteries"]):
        raise ValueError(f"Missing route record: {item['id']}")
    if abs(audit["total"]["source_ub"] - opt["ub"]) > 1e-5 or \
            abs(audit["total"]["difference"] -
                (audit["total"]["total"] - opt["ub"])) > 1e-5:
        raise ValueError(f"Audit/original UB mismatch: {item['id']}")
    difference = audit["total"]["total"] - opt["ub"]
    if difference < -COST_TOLERANCE:
        final_ub = audit["total"]["total"]
        cost_source = "fixed_design_audit"
        components = {key: audit["total"][key] for key in (
            "facility_cost", "battery_cost", "day_energy_cost", "night_energy_cost")}
    else:
        final_ub = opt["ub"]
        if opt["components"] is not None:
            components = opt["components"]
            cost_source = "original_full_incumbent"
        elif abs(difference) <= COST_TOLERANCE:
            components = {key: audit["total"][key] for key in (
                "facility_cost", "battery_cost", "day_energy_cost", "night_energy_cost")}
            cost_source = "fixed_design_audit_matching_ef_ub"
        else:
            raise ValueError(f"EF breakdown unavailable for original UB: {item['id']}")
    if difference > COST_TOLERANCE and not (
            audit["bound"] - COST_TOLERANCE <= opt["ub"] <=
            audit["total"]["total"] + COST_TOLERANCE):
        raise ValueError(f"Higher audit conflicts with original UB: {item['id']}")
    if abs(sum(components.values()) - final_ub) > COST_TOLERANCE or \
            final_ub < opt["lb"] - COST_TOLERANCE:
        raise ValueError(f"Final cost/bound reconciliation failed: {item['id']}")
    probabilities = {row["scenario"]: row["probability"]
                     for row in audit["scenarios"]}
    weighted_battery = sum(
        probability * sum(route["buses"] * route["battery_kwh"]
                          for route in audit["routes"]
                          if route["scenario"] == scenario) /
        sum(route["buses"] for route in audit["routes"]
            if route["scenario"] == scenario)
        for scenario, probability in probabilities.items())
    return {"item": item, "plan": plan, "optimization": opt, "audit": audit,
            "lb": opt["lb"], "original_ub": opt["ub"], "final_ub": final_ub,
            "gap_final_percent": 100 * (final_ub - opt["lb"]) / final_ub,
            "cost_source": cost_source, "components": components,
            "weighted_battery_kwh": weighted_battery,
            "probabilities": probabilities}


def write_csv(output_dir: Path, name: str, fields: list[str], rows: list[dict]) -> str:
    path = output_dir / name
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for row in rows:
            formatted = {}
            for key, value in row.items():
                if isinstance(value, float):
                    if not math.isfinite(value):
                        raise ValueError(f"Nonfinite CSV value: {name}, {key}")
                    if abs(value) < 0.0000005:
                        value = 0.0
                    formatted[key] = f"{value:.6f}"
                else:
                    formatted[key] = value
            writer.writerow(formatted)
    return digest(path)


def sensitivity_row(factor: str, dwc: dict, no_dwc: dict) -> dict:
    dwc_cost = dwc["components"]
    no_cost = no_dwc["components"]
    return {
        "factor": factor.replace("p", "."),
        "dwc_run_id": dwc["item"]["id"],
        "nodwc_run_id": no_dwc["item"]["id"],
        "dwc_lb": dwc["lb"],
        "dwc_ub_original": dwc["original_ub"],
        "dwc_ub_final": dwc["final_ub"],
        "nodwc_lb": no_dwc["lb"],
        "nodwc_ub_original": no_dwc["original_ub"],
        "nodwc_ub_final": no_dwc["final_ub"],
        "certified_saving_lower": no_dwc["lb"] - dwc["final_ub"],
        "certified_saving_upper": no_dwc["final_ub"] - dwc["lb"],
        "incumbent_cost_difference": no_dwc["final_ub"] - dwc["final_ub"],
        "selected_dwc_links": len(dwc["plan"]["facilities"]),
        "dwc_weighted_battery_kwh": dwc["weighted_battery_kwh"],
        "nodwc_weighted_battery_kwh": no_dwc["weighted_battery_kwh"],
        **{f"dwc_{key}": value for key, value in dwc_cost.items()},
        **{f"nodwc_{key}": value for key, value in no_cost.items()},
    }


def export_all(runs: dict[str, dict], source_manifest_sha: str,
               output_dir: Path) -> None:
    summaries = []
    reconciliations = []
    facilities = []
    batteries = []
    for run_id, run in sorted(runs.items()):
        item, audit = run["item"], run["audit"]
        name = item["data_name"]
        if name == "R28_N5":
            dimension, factor = "baseline", "1.0"
        else:
            match = re.fullmatch(r"R28_N5_(dwc|battery)(0p5|0p75|1p25|1p5)", name)
            if match is None:
                raise ValueError(f"Unexpected sensitivity dataset: {name}")
            dimension = ("dwc_construction_cost" if match.group(1) == "dwc"
                         else "battery_unit_cost")
            factor = match.group(2).replace("p", ".")
        summaries.append({
            "run_id": run_id, "data_name": name,
            "cost_dimension": dimension, "cost_factor": factor,
            "wireless_option": item["wireless"],
            "algorithm": run["optimization"]["algorithm"],
            "termination_status": run["optimization"]["status"],
            "lb_original": run["lb"], "ub_original": run["original_ub"],
            "ub_final": run["final_ub"],
            "gap_original_percent": run["optimization"]["gap_percent"],
            "gap_final_percent": run["gap_final_percent"],
            "final_ub_and_cost_source": run["cost_source"],
            **run["components"], "total_cost": run["final_ub"],
            "selected_dwc_links": len(run["plan"]["facilities"]),
            "weighted_battery_kwh": run["weighted_battery_kwh"],
            "optimization_log_sha256": item["optimization_log_sha256"],
            "audit_log_sha256": item["audit_log_sha256"],
        })
        higher = audit["total"]["difference"] > COST_TOLERANCE
        reconciliations.append({
            "run_id": run_id,
            "original_ub": run["original_ub"],
            "audit_total": audit["total"]["total"],
            "audit_minus_original_ub": audit["total"]["difference"],
            "audit_fixed_design_lower_bound": audit["bound"],
            "original_ub_within_audit_bounds": (
                "yes" if higher else "not_applicable"),
            "final_ub": run["final_ub"],
            "final_ub_and_cost_source": run["cost_source"],
            "audit_status": "COMPLETE_OPTIMAL_RECOURSE",
        })
        for link in audit["facilities"]:
            facilities.append({"run_id": run_id, "data_name": name,
                               "link_name": link["name"],
                               "from_station": link["from"],
                               "to_station": link["to"]})
        for route in audit["routes"]:
            batteries.append({
                "run_id": run_id, "data_name": name,
                "scenario": route["scenario"],
                "scenario_probability": run["probabilities"][route["scenario"]],
                "route_index": route["route_index"],
                "route_name": route["route"], "buses": route["buses"],
                "battery_kwh": route["battery_kwh"],
            })

    baseline_ids = ("C4_DWC", "C4_noDWC_EF")
    route_rows = []
    station_rows = []
    scenario_rows = []
    by_route = {}
    by_station = {}
    for run_id in baseline_ids:
        run = runs[run_id]
        audit = run["audit"]
        for route in audit["routes"]:
            key = (route["scenario"], route["route_index"])
            by_route[(run_id, *key)] = route
            route_rows.append({"run_id": run_id,
                               "scenario_probability":
                               run["probabilities"][route["scenario"]],
                               **route})
        for station in audit["stations"]:
            key = (station["scenario"], station["station"])
            by_station[(run_id, *key)] = station
            station_rows.append({"run_id": run_id,
                                 "scenario_probability":
                                 run["probabilities"][station["scenario"]],
                                 **station})
        for scenario in audit["scenarios"]:
            index = scenario["scenario"]
            routes = [row for row in audit["routes"]
                      if row["scenario"] == index]
            stations = [row for row in audit["stations"]
                        if row["scenario"] == index]
            if len(routes) != 28 or len(stations) != 5:
                raise ValueError(f"Incomplete baseline scenario: {run_id}, {index}")
            plug = sum(row["plug_grid_kwh"] for row in routes)
            station_plug = sum(row["plug_grid_kwh"] for row in stations)
            if abs(plug - station_plug) > ENERGY_TOLERANCE_KWH:
                raise ValueError(f"Plug energy does not reconcile: {run_id}, {index}")
            dwc = sum(row["dwc_grid_kwh"] for row in routes)
            if run["item"]["wireless"] == 0 and abs(dwc) > ENERGY_TOLERANCE_KWH:
                raise ValueError("No-DWC audit contains DWC charging")
            scenario_rows.append({
                "run_id": run_id, "scenario": index,
                "scenario_folder": scenario["folder"],
                "probability": scenario["probability"],
                "plug_grid_kwh": plug,
                "dwc_grid_kwh": dwc,
                "night_grid_kwh": sum(row["night_grid_kwh"] for row in routes),
                "fixed_design_recourse_objective": scenario["objective"],
                "fixed_design_recourse_bound": scenario["bound"],
            })

    changes = []
    battery_counts = defaultdict(int)
    reduced_and_uses_dwc = 0
    for scenario in range(5):
        for route_index in range(28):
            with_dwc = by_route[("C4_DWC", scenario, route_index)]
            without = by_route[("C4_noDWC_EF", scenario, route_index)]
            if with_dwc["route"] != without["route"] or \
                    with_dwc["buses"] != without["buses"]:
                raise ValueError(f"Baseline route mismatch: {scenario}, {route_index}")
            delta = with_dwc["battery_kwh"] - without["battery_kwh"]
            change = ("decreased" if delta < 0 else
                      "increased" if delta > 0 else "unchanged")
            uses = with_dwc["dwc_grid_kwh"] > ENERGY_TOLERANCE_KWH
            battery_counts[change] += 1
            reduced_and_uses_dwc += int(change == "decreased" and uses)
            changes.append({
                "scenario": scenario,
                "scenario_probability": runs["C4_DWC"]["probabilities"][scenario],
                "route_index": route_index, "route_name": with_dwc["route"],
                "buses": with_dwc["buses"],
                "battery_dwc_kwh": with_dwc["battery_kwh"],
                "battery_nodwc_kwh": without["battery_kwh"],
                "battery_change_kwh": delta, "battery_change": change,
                "dwc_grid_kwh": with_dwc["dwc_grid_kwh"],
                "uses_dwc": int(uses),
                "dwc_available_minutes": with_dwc["dwc_available_minutes"],
                "plug_grid_kwh_dwc": with_dwc["plug_grid_kwh"],
                "plug_grid_kwh_nodwc": without["plug_grid_kwh"],
            })
    if sum(battery_counts.values()) != 140:
        raise ValueError("Battery change count does not cover 140 route-scenarios")

    station_changes = []
    station_counts = defaultdict(int)
    station_ids = {key[2] for key in by_station}
    if len(station_ids) != 5:
        raise ValueError("Expected five existing Plug-in stations")
    for station in sorted(station_ids):
        expected = {}
        for run_id in baseline_ids:
            expected[run_id] = sum(
                runs[run_id]["probabilities"][scenario] *
                by_station[(run_id, scenario, station)]["plug_grid_kwh"]
                for scenario in range(5))
        difference = expected["C4_DWC"] - expected["C4_noDWC_EF"]
        direction = ("increased" if difference > ENERGY_TOLERANCE_KWH else
                     "decreased" if difference < -ENERGY_TOLERANCE_KWH else
                     "unchanged")
        station_counts[direction] += 1
        station_changes.append({
            "station": station,
            "expected_plug_grid_kwh_dwc": expected["C4_DWC"],
            "expected_plug_grid_kwh_nodwc": expected["C4_noDWC_EF"],
            "difference_kwh": difference, "change": direction,
        })
    plug_expected = {
        run_id: sum(row["probability"] * row["plug_grid_kwh"]
                    for row in scenario_rows if row["run_id"] == run_id)
        for run_id in baseline_ids
    }
    if plug_expected["C4_noDWC_EF"] <= 0:
        raise ValueError("No-DWC Plug-in energy is zero")
    base_dwc, base_no = (runs[run_id] for run_id in baseline_ids)
    baseline_summary = [{
        "case": "R28-S5-F50-C4", "route_scenario_count": 140,
        "battery_decreased": battery_counts["decreased"],
        "battery_unchanged": battery_counts["unchanged"],
        "battery_increased": battery_counts["increased"],
        "battery_decreased_and_uses_dwc": reduced_and_uses_dwc,
        "expected_plug_grid_kwh_dwc": plug_expected["C4_DWC"],
        "expected_plug_grid_kwh_nodwc": plug_expected["C4_noDWC_EF"],
        "plug_change_percent": 100 * (
            plug_expected["C4_DWC"] - plug_expected["C4_noDWC_EF"]) /
            plug_expected["C4_noDWC_EF"],
        "stations_plug_increased": station_counts["increased"],
        "stations_plug_unchanged": station_counts["unchanged"],
        "stations_plug_decreased": station_counts["decreased"],
        "weighted_battery_dwc_kwh": base_dwc["weighted_battery_kwh"],
        "weighted_battery_nodwc_kwh": base_no["weighted_battery_kwh"],
        "dwc_ub_final": base_dwc["final_ub"],
        "nodwc_ub_final": base_no["final_ub"],
        "certified_saving_lower": base_no["lb"] - base_dwc["final_ub"],
        "certified_saving_upper": base_no["final_ub"] - base_dwc["lb"],
    }]
    if sum(station_counts.values()) != 5:
        raise ValueError("Station change count does not cover five stations")

    battery_pairs = []
    construction_pairs = []
    for factor in FACTORS:
        dwc_id = "C4_DWC" if factor == "1" else f"C4_battery{factor}_DWC"
        no_id = ("C4_noDWC_EF" if factor == "1" else
                 f"C4_battery{factor}_noDWC_EF")
        battery_pairs.append(sensitivity_row(factor, runs[dwc_id], runs[no_id]))
        dwc_id = "C4_DWC" if factor == "1" else f"C4_DWCcost{factor}"
        construction_pairs.append(sensitivity_row(
            factor, runs[dwc_id], runs["C4_noDWC_EF"]))

    tables = {
        "run_summary.csv": (list(summaries[0]), summaries),
        "ub_reconciliation.csv": (list(reconciliations[0]), reconciliations),
        "final_design_facilities.csv": (list(facilities[0]), facilities),
        "final_design_batteries.csv": (list(batteries[0]), batteries),
        "baseline_routes.csv": (list(route_rows[0]), route_rows),
        "baseline_stations.csv": (list(station_rows[0]), station_rows),
        "baseline_scenarios.csv": (list(scenario_rows[0]), scenario_rows),
        "baseline_route_changes.csv": (list(changes[0]), changes),
        "baseline_station_changes.csv": (list(station_changes[0]), station_changes),
        "baseline_summary.csv": (list(baseline_summary[0]), baseline_summary),
        "battery_cost_sensitivity.csv": (list(battery_pairs[0]), battery_pairs),
        "dwc_cost_sensitivity.csv": (list(construction_pairs[0]), construction_pairs),
    }
    output_records = {}
    for name, (fields, rows) in tables.items():
        output_records[name] = {"rows": len(rows),
                                "sha256": write_csv(output_dir, name, fields, rows)}
    manifest = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "source_package_manifest_sha256": source_manifest_sha,
        "export_script_sha256": digest(Path(__file__)),
        "cost_tolerance": COST_TOLERANCE,
        "energy_tolerance_kwh": ENERGY_TOLERANCE_KWH,
        "tables": output_records,
    }
    (output_dir / "export_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"runs": len(runs), "tables": {
        name: info["rows"] for name, info in output_records.items()},
        "baseline_summary": baseline_summary[0]}, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=OUTPUT)
    args = parser.parse_args()
    manifest_path = PACKAGE / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    records = {item["id"]: item for item in manifest["records"]
               if item["capacity"] == 4}
    if set(records) != EXPECTED_IDS:
        raise ValueError("Section 5.3 C4 run coverage mismatch")
    for version in ("full_executable", "ef_audit_executable"):
        entry = manifest[version]
        if digest(ROOT / entry["path"]) != entry["sha256"]:
            raise ValueError(f"Executable hash mismatch: {version}")
    snapshot = manifest["ef_audit_executable"]
    if digest(ROOT / snapshot["source_snapshot"]) != \
            snapshot["source_snapshot_sha256"]:
        raise ValueError("Audit source snapshot hash mismatch")
    case = json.loads((ROOT / manifest["base_case_manifest"]).read_text(
        encoding="utf-8"))["cases"]["R28-N5-F50-C4"]
    for relative, expected in case["dataset_files_sha256"].items():
        if digest(ROOT / relative) != expected:
            raise ValueError(f"Base dataset changed: {relative}")
    if digest(ROOT / "solverCfg.txt") != case["solver_config_sha256"]:
        raise ValueError("Solver configuration changed")
    for kind in ("dwc", "battery"):
        for factor in FACTORS:
            if factor != "1":
                verify_variant(f"R28_N5_{kind}{factor}",
                               factor.replace("p", "."), kind)
    runs = {name: load_run(item) for name, item in records.items()}
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    export_all(runs, digest(manifest_path), output_dir)


if __name__ == "__main__":
    main()
