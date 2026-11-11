"""Recompute Chapter 5 numerical summaries and search diagnostics without calling a solver.

Usage (from repository root):
  python manuscript/chapter5_analysis/analyze.py --package PACKAGE_PATH --source-commit GIT_SHA
All times come from the published target-machine logs or controller records.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import statistics
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "figures"))
from chapter5_progress import anchor_coverage, fields, read_log, seconds, timing


TWO_PHASE = ("lifted-two-phase", "full")
CAPACITY_FACILITY_CASES = ("R28-N5-F10-C4", "R28-N5-F20-C4", "R28-N5-F50-C4",
                           "R28-N5-F50-C3", "R28-N5-F50-C2")
SCENARIO_CASES = tuple(f"R28-N{n}-F50-C4" for n in (5, 10, 20, 40))


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def verify_hashes(root, hashes):
    for name, expected in hashes.items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f"Checksum mismatch: {root / name}")


def command_settings(package, record, option):
    raw = read_json(package / record["run_record"])
    arguments = dict(zip(raw["arguments"][::2], raw["arguments"][1::2]))
    expected = str(record["vi"]) if option == "--valid-inequality" else record["algorithm"]
    if arguments.pop(option) != expected:
        raise ValueError(f"Unexpected {option}: {record['record_id']}")
    for field in ("source_commit", "executable_sha256", "raw_log_sha256"):
        if raw[field] != record[field]:
            raise ValueError(f"Record provenance mismatch: {record['record_id']}, {field}")
    execution = raw["execution_result"]
    if execution["status"] != record["status"] or not math.isclose(
            execution["actual_wall_seconds"], record["actual_wall_seconds"], abs_tol=1e-6):
        raise ValueError(f"Controller result mismatch: {record['record_id']}")
    return arguments, raw["source_commit"], raw["executable_sha256"]


def compare_previous(records, previous):
    old = {r["record_id"]: r for r in read_json(previous)["records"]}
    new = {r["record_id"]: r for r in records}
    compared = ("lb", "ub", "status", "gap_percent", "actual_wall_seconds",
                "source_commit", "executable_sha256", "raw_log_sha256")
    removed = sorted(old.keys() - new.keys())
    changed = {key: [field for field in compared if old[key][field] != new[key][field]]
               for key in old.keys() & new.keys()}
    changed = {key: values for key, values in changed.items() if values}
    return {"previous_result_slots": len(old), "overlapping_result_slots": len(old.keys() & new.keys()),
            "unchanged_result_slots": len(old.keys() & new.keys()) - len(changed),
            "removed": removed, "changed": changed, "added": sorted(new.keys() - old.keys())}


def design_key(design):
    batteries = tuple(tuple(s) for s in design["batteries"])
    if any(v is None for s in batteries for v in s):
        raise ValueError("Missing selected battery in a logged master design")
    return tuple(sorted(design["facilities"])), batteries


def distance(a, b):
    af, ab = design_key(a)
    bf, bb = design_key(b)
    if [len(s) for s in ab] != [len(s) for s in bb]:
        raise ValueError("Different battery dimensions")
    return len(set(af) ^ set(bf)), sum(x != y for u, v in zip(ab, bb) for x, y in zip(u, v))


def stat(values):
    return {"mean": statistics.mean(values), "median": statistics.median(values),
            "min": min(values), "max": max(values)} if values else None


def load_run(package, record):
    lines = (package / record["raw_log"]).read_text(encoding="utf-8").splitlines()
    switch, p1, p2, final = read_log(package / record["raw_log"])
    searches = [json.loads(line.split(": ", 1)[1]) for line in lines
                if line.startswith("Search master: ")]
    master = {row["iter"]: row for row in searches if row["phase"] == 2}
    repairs = [json.loads(line.split(": ", 1)[1]) for line in lines
               if line.startswith("Search repair: ")]
    return {"record": record, "switch": switch, "p1": p1, "p2": p2,
            "final": final, "master": master, "repairs": repairs}


def completed_at(run, cutoff, phase_clock=False):
    origin = seconds(run["switch"]) if phase_clock else 0.0
    return [row for row in run["p2"] if seconds(row) - origin <= cutoff + 1e-7]


def bound_at(run, cutoff, phase_clock=False):
    """Latest completed-iteration bound, including the final record if available.

    Search master JSON has no elapsed timestamp. Do not infer the completion
    time of an ongoing master solve or assign its eventual bound to an earlier
    checkpoint. This deliberately uses only timestamped completed records.
    """
    origin = seconds(run["switch"]) if phase_clock else 0.0
    rows = completed_at(run, cutoff, phase_clock)
    row = rows[-1] if rows else run["switch"]
    if seconds(run["final"]) - origin <= cutoff + 1e-7:
        row = run["final"]
    return {"lb": float(row["LB"]), "reported_at": seconds(row) - origin}


def search_stats(run, cutoff):
    rows = completed_at(run, cutoff)
    candidates = [run["master"][row["iteration"]] for row in rows]
    if any(not row["has_solution"] for row in candidates):
        raise ValueError("A completed evaluation has no master incumbent")
    designs = [row["design"] for row in candidates]
    distances = [distance(a, b) for a, b in zip(designs, designs[1:])]
    facility = [d[0] for d in distances]
    battery = [d[1] for d in distances]
    unique = len({design_key(d) for d in designs})
    facilities = int(re.search(r"-F(\d+)-", run["record"]["case_id"])[1])
    battery_choices = sum(len(s) for s in designs[0]["batteries"]) if designs else 0
    return {"completed_candidates": len(designs), "unique_configurations": unique,
            "repeat_fraction": (1 - unique / len(designs)) if designs else None,
            "adjacent_pairs": len(distances), "facility_changes": stat(facility),
            "battery_choice_changes": stat(battery),
            "total_choice_changes": stat([f + b for f, b in distances]),
            "one_hot_hamming": stat([f + 2 * b for f, b in distances]),
            "facility_change_fraction": stat([f / facilities for f in facility]),
            "battery_choice_change_fraction": stat([b / battery_choices for b in battery]),
            "normalization": {"facilities": facilities, "route_scenario_choices": battery_choices},
            "repair_probes": sum(int(row["repair_probes"]) for row in rows),
            "last_completed_seconds": seconds(rows[-1]) if rows else None}


def anchor_stats(run):
    """Compare each cut anchor with its MP design on that repair's scenario.

    Initial-UB completion uses a partly repaired combined incumbent, so its
    baseline would differ from the MP design and is excluded here.
    """
    changes = []
    for row in run["repairs"]:
        if row["trigger"] == "initial-ub":
            continue
        start = run["master"][row["iter"]]["design"]
        anchor = row["cut_anchor"]
        if not set(start["facilities"]) <= set(anchor["facilities"]):
            raise ValueError("Cut anchor removes an installed facility")
        a, b = start["batteries"][row["scenario"]], anchor["batteries"][row["scenario"]]
        if not all(y >= x for x, y in zip(a, b)):
            raise ValueError("Cut anchor reduces a battery")
        changes.append((len(set(anchor["facilities"]) - set(start["facilities"])),
                        sum(x != y for x, y in zip(a, b)),
                        sum((y - x) / 50 for x, y in zip(a, b))))
    coverage = anchor_coverage(run["master"], run["repairs"])
    if len(coverage) != len(changes):
        raise ValueError("Repair coverage and expansion counts differ")
    return {"anchors": len(changes), "trigger_counts": dict(Counter(r["trigger"] for r in run["repairs"])),
            "added_facilities": stat([v[0] for v in changes]),
            "raised_battery_choices": stat([v[1] for v in changes]),
            "added_50kwh_steps": stat([v[2] for v in changes]),
            "repairs_with_feasible_design": sum(r["feasible_design"] is not None for r in run["repairs"]),
            "resource_box_coverage_ratio": stat([v["coverage_ratio"] for v in coverage]),
            "resource_box_coverage": coverage}


def phase_one_stats(run, reference_ub):
    distance_to = lambda lb: 100 * (reference_ub - float(lb)) / reference_ub
    timers = {"Master": sum(float(r["master_ms"]) for r in run["p1"]) / 1000,
              "Route LP": sum(float(r["subproblem_ms"]) for r in run["p1"]) / 1000}
    timers["Other"] = seconds(run["switch"]) - sum(timers.values())
    if timers["Other"] < -0.01:
        raise ValueError("Phase 1 component times exceed elapsed time")
    return {"phase1_seconds": seconds(run["switch"]), "iterations": len(run["p1"]),
            "lb": float(run["switch"]["LB"]), "relaxation_ub": float(run["switch"]["UB"]),
            "distance_percent": distance_to(run["switch"]["LB"]),
            "time_to_5_percent": next((seconds(r) for r in run["p1"] if distance_to(r["LB"]) <= 5), None),
            "time_to_1_percent": next((seconds(r) for r in run["p1"] if distance_to(r["LB"]) <= 1), None),
            "phase1_timers": timers}


def scenario_inputs(repository):
    """Check nested scenario content as well as the changing probability weights."""
    result, previous = {}, None
    for n in (5, 10, 20, 40):
        root = repository / f"data/R28_N{n}_data"
        lines = [s.strip() for s in (root / "scenarioCfg.txt").read_text().splitlines()
                 if s.strip() and not s.startswith("#")]
        if int(lines[0]) != n:
            raise ValueError(f"Unexpected scenario count in {root}")
        scenarios = [(s.split(",")[0], float(s.split(",")[1])) for s in lines[1:]]
        if len(scenarios) != n or not math.isclose(sum(p for _, p in scenarios), 1.0, abs_tol=1e-10):
            raise ValueError(f"Invalid scenario probabilities: {root}")
        names = [name for name, _ in scenarios]
        same_shared = None
        common_files = []
        if previous:
            old_root, old_names = previous
            if names[:len(old_names)] != old_names:
                raise ValueError("Scenario sets are not nested prefixes")
            shared = [p.relative_to(old_root) for name in old_names for p in (old_root / name).rglob("*") if p.is_file()]
            same_shared = all((old_root / p).read_bytes() == (root / p).read_bytes() for p in shared)
            common_files = [p.name for p in old_root.iterdir() if p.is_file() and p.name != "scenarioCfg.txt"]
            if not same_shared or not all((old_root / p).read_bytes() == (root / p).read_bytes() for p in common_files):
                raise ValueError("Shared scenario or network content changed with N")
        result[str(n)] = {"scenario_names": names, "probabilities": [p for _, p in scenarios],
                          "uniform": all(math.isclose(p, 1 / n, abs_tol=1e-12) for _, p in scenarios),
                          "prefix_and_shared_scenario_bytes_match_previous": same_shared,
                          "common_network_files_match_previous": common_files}
        previous = root, names
    return result


def numeric_rows(path, cast=float):
    return [[cast(x) for x in line.split(",")] for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


def instance_description(repository):
    """Recover physical counts, coefficients and the implemented candidate ranking."""
    data = repository / "data"
    route_sets, candidates = {}, {}
    for count in (3, 12, 28):
        root = data / f"R{count}_N5_data"
        lines = (root / "base/scenarioMiscCfg.txt").read_text().splitlines()
        routes = lines[lines.index("# routeNames") + 1].split(",")
        edges = numeric_rows(root / "linkDistance.txt")
        if len({(int(u), int(v)) for u, v, _ in edges}) != len(edges):
            raise ValueError("Duplicate physical directed link")
        config = numeric_rows(root / "globalCfg.txt")
        route_sets[str(count)] = {
            "routes": routes, "physical_stations": len((root / "map.txt").read_text().splitlines()),
            "directed_links": len(edges), "existing_plugin_locations": int(config[7][0]),
            "base_buses": int(lines[lines.index("# numEB") + 1])}
        if len(routes) != count:
            raise ValueError("Route count and route-name list differ")
    config = numeric_rows(data / "R28_N5_data/globalCfg.txt")
    operation, times, consumption, _, rates, efficiency, cost = config[:7]
    for n in (5, 10, 20, 40):
        root = data / f"R28_N{n}_data"
        scenario_lines = [line for line in (root / "scenarioCfg.txt").read_text().splitlines()
                          if line.strip() and not line.startswith("#")][1:]
        scores = Counter()
        for line in scenario_lines:
            folder = root / line.split(",")[0]
            stops = numeric_rows(folder / "tripStations.txt", int)
            arrivals = numeric_rows(folder / "tripArrTimes.txt", int)
            departures = numeric_rows(folder / "tripDepTimes.txt", int)
            for st, ar, de in zip(stops, arrivals, departures, strict=True):
                if len(st) != len(ar) or len(st) != len(de):
                    raise ValueError("Trip data dimensions differ")
                for i, edge in enumerate(zip(st, st[1:])):
                    scores[edge] += ar[i + 1] - de[i]
        ranked = sorted(scores, key=lambda edge: (-scores[edge], edge))
        selected = ranked[:50]
        base = {tuple(edge) for edge in candidates["5"]["top_50"]} if n != 5 else set(selected)
        candidates[str(n)] = {"top_50": selected, "top_50_in_common_with_N5": len(set(selected) & base)}
    return {"route_sets": route_sets, "candidate_ranking": candidates,
            "parameters": {"day_price": operation[0], "night_price": operation[1],
                           "battery_price_per_kwh_day": operation[2],
                           "maximum_soc": operation[3], "minimum_soc": operation[4],
                           "minimum_battery_kwh": operation[5], "maximum_battery_kwh": operation[6],
                           "slot_minutes": times[2], "kwh_per_km": consumption[0],
                           "plugin_kwh_per_min": rates[0], "dwc_kwh_per_min": rates[2],
                           "plugin_efficiency": efficiency[0], "dwc_efficiency": efficiency[2],
                           "dwc_cost_per_km_day": cost[2],
                           "dwc_inverter_per_link_day": 60 * cost[3] * rates[2]}}


def lifted_vi_stats(package, record):
    lines = (package / record["raw_log"]).read_text(encoding="utf-8").splitlines()
    rows = []
    for number, line in enumerate(lines, 1):
        if line.startswith("Lifted ILS iter ") and "elapsed_ms=" in line:
            row = fields(line)
            row["raw_line"] = number
            rows.append(row)
    terminal = next(fields(line) for line in lines if line.startswith("Algorithm termination:"))
    audits = [fields(line) for line in lines if line.startswith("Lifted ILS VI audit:")]
    audit = ({k: float(v) if k == "max_deficit" else int(v) for k, v in audits[0].items()}
             if audits else None)
    final = {"lb": float(terminal["LB"]), "ub": float(terminal["UB"]),
             "reported_at": float(terminal["reported_seconds"])}
    checkpoints = {}
    for cutoff in (300, 600, 900, 1200, 1800, 3600, 5400, 7200):
        available = [r for r in rows if seconds(r) <= cutoff + 1e-7]
        bound = ({"lb": float(available[-1]["LB"]), "reported_at": seconds(available[-1]),
                  "raw_line": available[-1]["raw_line"], "source": "completed_iteration"}
                 if available else None)
        if final["reported_at"] <= cutoff + 1e-7:
            bound = {**final, "source": "final"}
        checkpoints[str(cutoff)] = bound
    return {"record": record, "audit": audit, "complete_iterations": len(rows),
            "last_complete_iteration": ({"lb": float(rows[-1]["LB"]),
                                         "reported_at": seconds(rows[-1]), "raw_line": rows[-1]["raw_line"]}
                                        if rows else None),
            "final": final, "checkpoints": checkpoints}


def json_safe(value):
    if isinstance(value, dict):
        return {k: json_safe(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(v) for v in value]
    if isinstance(value, float) and not math.isfinite(value):
        return str(value)
    return value


def write_vi_checkpoints(output, vi):
    with (output / "vi_checkpoint_bounds.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("case", "algorithm_time_s", "VI_LB", "VI_record_time_s",
                         "noVI_LB", "noVI_record_time_s"))
        for case, pair in vi.items():
            for cutoff in pair["1"]["checkpoints"]:
                on, off = (pair[str(s)]["checkpoints"][cutoff] for s in (1, 0))
                writer.writerow((case, cutoff, on["lb"] if on else None, on["reported_at"] if on else None,
                                 off["lb"] if off else None, off["reported_at"] if off else None))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--source-commit", required=True,
                        help="Git commit from which the package was exported")
    parser.add_argument("--previous-results", type=Path,
                        help="Optional previous results.json for an unchanged-results audit")
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    package, output = args.package, args.output_dir
    output.mkdir(parents=True, exist_ok=True)
    manifest = read_json(package / "manifest.json")
    verify_hashes(package, manifest["package_files_sha256"])
    repository = package.resolve().parents[1]
    verify_hashes(repository, manifest["repository_input_files_sha256"])
    results = read_json(package / "results.json")
    records = results["records"]
    if len(records) != results["record_slots"] or len({r["raw_log"] for r in records}) != results["unique_logs"]:
        raise ValueError("Result/log coverage count mismatch")
    for record in records:
        log = package / record["raw_log"]
        if hashlib.sha256(log.read_bytes()).hexdigest() != record["raw_log_sha256"]:
            raise ValueError(f"Result/log checksum mismatch: {record['record_id']}")
        if record["status"] == "MEMORY_LIMIT_EXTERNAL":
            continue
        terminal = [fields(line) for line in log.read_text(encoding="utf-8").splitlines()
                    if line.startswith("Algorithm termination:")]
        if len(terminal) != 1:
            raise ValueError(f"Missing or duplicate terminal record: {log}")
        if terminal[0]["status"] != record["status"]:
            raise ValueError(f"Status mismatch: {log}")
        for field in ("lb", "ub"):
            if float(terminal[0][field.upper()]) != float(record[field]):
                raise ValueError(f"Final {field} mismatch: {log}")
        if math.isfinite(float(record["ub"])):
            gap = 100 * (float(record["ub"]) - float(record["lb"])) / abs(float(record["ub"]))
            if not math.isclose(gap, float(record["gap_percent"]), abs_tol=1e-5):
                raise ValueError(f"Final gap mismatch: {log}")
            if record["status"] == "OPTIMAL" and gap > 1 + 1e-5:
                raise ValueError(f"Target status with gap above 1%: {log}")
    selected = {(r["case_id"], r["algorithm"]): r for r in records if r["group"] != "vi_validation"}
    pairs, runs = {}, {}
    for case in (*CAPACITY_FACILITY_CASES, *SCENARIO_CASES[1:]):
        pairs[case] = {a: selected[case, a] for a in TWO_PHASE}
        settings = []
        for a in TWO_PHASE:
            record = pairs[case][a]
            settings.append(command_settings(package, record, "--alg"))
            runs[case, a] = load_run(package, record)
        if settings[0] != settings[1]:
            raise ValueError(f"Unmatched Full/L2P pair: {case}")
    phase_one = []
    for case in ("R12-N5-F50-C4", *pairs):
        run = runs.get((case, TWO_PHASE[0])) or load_run(package, selected[case, TWO_PHASE[0]])
        references = [selected[case, a] for a in TWO_PHASE if math.isfinite(float(selected[case, a]["ub"]))]
        ref = min(references, key=lambda r: float(r["ub"]))
        ub = float(ref["ub"])
        by_algorithm = {a: phase_one_stats(runs.get((case, a)) or load_run(package, selected[case, a]), ub)
                        for a in TWO_PHASE}
        original_lb = max(float(selected[case, a]["lb"]) for a in TWO_PHASE)
        relaxation_lb = max(r["lb"] for r in by_algorithm.values())
        relaxation_ub = min(r["relaxation_ub"] for r in by_algorithm.values())
        phase_one.append({"case": case, "reference_algorithm": ref["algorithm"],
                          "reference_ub": ub, **phase_one_stats(run, ub),
                          "by_algorithm": by_algorithm,
                          "relaxation_gap_certificate_percent": {
                              "lower": 100 * max(0, 1 - relaxation_ub / original_lb),
                              "upper": 100 * (1 - relaxation_lb / ub),
                              "original_lb": original_lb, "original_ub": ub,
                              "relaxation_lb": relaxation_lb, "relaxation_ub": relaxation_ub}})
    phase_two, comparisons = {}, {}
    for case, pair in pairs.items():
        phase_two[case] = {}
        cutoff = min(seconds(runs[case, a]["final"]) for a in TWO_PHASE)
        phase_cutoff = min(seconds(runs[case, a]["final"]) - seconds(runs[case, a]["switch"]) for a in TWO_PHASE)
        checkpoints = {}
        for t in (1800.0, 3600.0, 7200.0):
            if t > cutoff + 1e-6:
                continue
            bounds = {a: bound_at(runs[case, a], t) for a in TWO_PHASE}
            bounds["full_relative_lb_percent"] = 100 * (bounds["full"]["lb"] / bounds[TWO_PHASE[0]]["lb"] - 1)
            checkpoints[str(int(t))] = bounds
        comparisons[case] = {"common_overall_seconds": cutoff,
                             "common_phase2_seconds": phase_cutoff,
                             "checkpoints": checkpoints,
                             "overall_bounds": {a: bound_at(runs[case, a], cutoff) for a in TWO_PHASE},
                             "phase2_bounds": {a: bound_at(runs[case, a], phase_cutoff, True) for a in TWO_PHASE},
                             "search": {a: search_stats(runs[case, a], cutoff) for a in TWO_PHASE}}
        for a in TWO_PHASE:
            r = runs[case, a]
            first = next((x for x in r["p2"] if math.isfinite(float(x["UB"]))), None)
            phase_two[case][a] = {
                "complete_iterations": len(r["p2"]), "master_returns": len(r["master"]),
                "master_returns_with_solution": sum(x["has_solution"] for x in r["master"].values()),
                "unique_returned_designs": len({design_key(x["design"]) for x in r["master"].values()
                                                if x["has_solution"]}),
                "returns_without_completed_evaluation": [x["iter"] for x in r["master"].values()
                    if x["iter"] not in {v["iteration"] for v in r["p2"]}],
                "solver_seconds": seconds(r["final"]),
                "controller_seconds": pair[a]["actual_wall_seconds"],
                "status": pair[a]["status"],
                "final_gap_percent": (float(pair[a]["gap_percent"])
                                      if pair[a]["gap_percent"] is not None else None),
                "phase2_seconds": seconds(r["final"]) - seconds(r["switch"]),
                "first_ub_overall_seconds": seconds(first) if first else None,
                "first_ub_phase2_seconds": seconds(first) - seconds(r["switch"]) if first else None,
                "timing": timing(r["switch"], r["p2"], r["final"]),
                "repair_probes": sum(int(x["repair_probes"]) for x in r["p2"]),
                "strengthened_cuts": sum(int(x["strengthened_cuts"]) for x in r["p2"]),
                "anchor_statistics": anchor_stats(r),
                "final": {"lb": float(r["final"]["LB"]), "ub": float(r["final"]["UB"])}}
    vi = {}
    for case in sorted({r["case_id"] for r in records if r["group"] == "vi_validation"}):
        pair = {str(int(r["vi"])): r for r in records
                if r["group"] == "vi_validation" and r["case_id"] == case}
        if set(pair) != {"0", "1"} or command_settings(package, pair["0"], "--valid-inequality") != command_settings(
                package, pair["1"], "--valid-inequality"):
            raise ValueError(f"Unmatched VI pair: {case}")
        vi[case] = {switch: lifted_vi_stats(package, r) for switch, r in pair.items()}
    # Independently reproduce the publisher's strict 7200-second R28 checkpoints.
    supplied_csv = package / "analysis/r28_vi_same_algorithm_time_LB.csv"
    vi_csv_verified = 0
    if supplied_csv.exists():
        with supplied_csv.open(encoding="utf-8-sig", newline="") as stream:
            for row in csv.DictReader(stream):
                cutoff = str(int(float(row["algorithm_time_s"])))
                for switch, prefix in (("1", "VI"), ("0", "noVI")):
                    observed = vi["R28-N5-F50-C4"][switch]["checkpoints"][cutoff]
                    for expected_key, observed_key in ((prefix + "_LB", "lb"), (prefix + "_record_time_s", "reported_at")):
                        expected = row[expected_key]
                        if (expected == "") != (observed is None) or (observed is not None and not math.isclose(
                                float(expected), observed[observed_key], rel_tol=0, abs_tol=1e-6)):
                            raise ValueError(f"VI checkpoint CSV mismatch: {cutoff}, {expected_key}")
                vi_csv_verified += 1
    previous_audit = compare_previous(records, args.previous_results) if args.previous_results else None
    summary = {"source": f"origin/codex/final-experiments-2026-11-05@{args.source_commit}",
               "package": "experiments/final_2026-11-07", "results_updated_utc": results["updated_utc"],
               "verified_package_files": len(manifest["package_files_sha256"]),
               "verified_repository_input_files": len(manifest["repository_input_files_sha256"]),
               "record_slots": len(records), "unique_logs": results["unique_logs"],
               "case_count": read_json(package / "cases.json")["case_count"],
               "versions": read_json(package / "versions.json"),
               "pending_replacement": results["pending_replacement"],
               "pending_solver_work": results["pending_solver_work"],
               "numerical_outcome_coverage_complete": results["numerical_outcome_coverage_complete"],
               "publisher_historical_archive_final_acceptance": results["final_acceptance"],
               "previous_results_audit": previous_audit, "scenario_inputs": scenario_inputs(repository),
               "instance_description": instance_description(repository),
               "vi_diagnostic_final": True, "vi": vi, "verified_vi_checkpoint_csv_rows": vi_csv_verified,
               "main_records": [r for r in records if r["group"] == "main_table"],
               "target_reached_counts": dict(Counter(r["algorithm"] for r in records
                                                       if r["group"] == "main_table" and r["status"] == "OPTIMAL")),
               "phase_one": phase_one, "phase_two": phase_two, "comparisons": comparisons}
    (output / "summary.json").write_text(json.dumps(json_safe(summary), ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    write_vi_checkpoints(output, vi)
    print(f"Verified {len(manifest['package_files_sha256'])} package files, {len(records)} result slots, "
          f"{len(manifest['repository_input_files_sha256'])} repository inputs, "
          f"{len(pairs)} Full/L2P pairs and {len(vi)} final VI pairs.")
    if previous_audit:
        print(f"Previous slots unchanged: {previous_audit['unchanged_result_slots']}/"
              f"{previous_audit['previous_result_slots']}; added: {len(previous_audit['added'])}; "
              f"changed: {len(previous_audit['changed'])}; removed: {len(previous_audit['removed'])}.")


if __name__ == "__main__":
    main()
