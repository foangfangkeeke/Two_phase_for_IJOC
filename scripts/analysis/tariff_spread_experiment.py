"""Prepare, run, and inspect the R28-N5-F50-C4 day/night tariff experiment.

Usage: python scripts/analysis/tariff_spread_experiment.py prepare
       python scripts/analysis/tariff_spread_experiment.py run <package directory>
       python scripts/analysis/tariff_spread_experiment.py status <package directory>

Preparation never launches a solver. The run command is serial and deliberately
cannot resume or overwrite an interrupted attempt.
"""

import csv
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from decimal import Decimal
from pathlib import Path

import psutil


ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "data/R28_N5_data"
CASE = json.loads((ROOT / "experiments/final_2026-11-07/cases.json").read_text(
    encoding="utf-8"))["cases"]["R28-N5-F50-C4"]
EVIDENCE = json.loads((ROOT / "experiments/section53_2026-11-09/manifest.json").read_text(
    encoding="utf-8"))
POINTER = ROOT / "results/tariff_spread_current.json"
FACTORS = (("s0", Decimal("0")), ("s0p5", Decimal("0.5")),
           ("s1", Decimal("1")), ("s1p5", Decimal("1.5")),
           ("s2", Decimal("2")))
MEAN = Decimal("0.15775")
HALF_SPREAD = Decimal("0.03445")
GIB = 1024 ** 3
RUNTIME = {"facility_cost_scale": "1.000000", "energy_scale": "1.000000",
           "battery_step": "50", "scenario_count": "5", "scenario_start": "0",
           "cap": "4", "dwc_top_n": "50"}
SOLVER = {"mip_gap": "0.000100", "global_gap": "0.010000",
          "time_limit": "7200.000000", "threads": "12"}


def utc():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n",
                         encoding="utf-8")
    os.replace(temporary, path)


def fields(line):
    return dict(re.findall(r"(?:^|[, ])([A-Za-z_]+)=([^, ]+)", line))


def terminal(path, data_name, algorithm, wireless):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    runtime = [line for line in lines if line.startswith("Runtime options: ")]
    solver = [line for line in lines if line.startswith("Solver options: ")]
    instance = [line for line in lines if line.startswith("Instance: ")]
    if len(runtime) != 1 or len(solver) != 1 or len(instance) != 1:
        raise ValueError(f"Missing or duplicate log header: {path}")
    actual = fields(runtime[0])
    if any(actual.get(k) != v for k, v in RUNTIME.items()) or \
            actual.get("wireless_option") != str(wireless):
        raise ValueError(f"Unexpected runtime options: {path}")
    if any(fields(solver[0]).get(k) != v for k, v in SOLVER.items()):
        raise ValueError(f"Unexpected solver options: {path}")
    if fields(instance[0]).get("data") != data_name:
        raise ValueError(f"Unexpected instance: {path}")
    ends = [fields(line) for line in lines
            if line.startswith("Algorithm termination: ")]
    if len(ends) > 1:
        raise ValueError(f"Duplicate terminal status: {path}")
    if not ends:
        return {"status": "UNRECORDED", "has_solution": False,
                "lb": None, "ub": None, "gap_percent": None}
    end = ends[0]
    if end["algorithm"] != algorithm:
        raise ValueError(f"Unexpected algorithm: {path}")
    numeric = lambda value: float(value) if math.isfinite(float(value)) else None
    return {"status": end["status"], "has_solution": end["has_solution"] == "1",
            "lb": numeric(end["LB"]), "ub": numeric(end["UB"]),
            "gap_percent": numeric(end["gap_percent"])}


def verify_base():
    for relative, expected in CASE["dataset_files_sha256"].items():
        if sha(ROOT / relative) != expected:
            raise ValueError(f"Baseline input hash mismatch: {relative}")
    if sha(ROOT / "solverCfg.txt") != CASE["solver_config_sha256"]:
        raise ValueError("solverCfg.txt hash mismatch")
    for item in (EVIDENCE["full_executable"], EVIDENCE["ef_audit_executable"]):
        if sha(ROOT / item["path"]) != item["sha256"]:
            raise ValueError(f"Executable hash mismatch: {item['path']}")
    if sha(ROOT / EVIDENCE["ef_audit_executable"]["source_snapshot"]) != \
            EVIDENCE["ef_audit_executable"]["source_snapshot_sha256"]:
        raise ValueError("EF/audit source archive hash mismatch")
    return sha(ROOT / EVIDENCE["full_executable"]["source_archive"])


def selected_baseline():
    records = {row["id"]: row for row in EVIDENCE["records"]}
    result = {}
    for arm, record_id, algorithm, wireless in (
            ("full", "C4_DWC", "full", 1),
            ("ef", "C4_noDWC_EF", "ef", 0)):
        record = records[record_id]
        log = ROOT / record["optimization_log"]
        audit = ROOT / record["audit_log"]
        plan = ROOT / record["plan"]
        for path, expected in ((log, record["optimization_log_sha256"]),
                               (audit, record["audit_log_sha256"]),
                               (plan, record["plan_sha256"])):
            if sha(path) != expected:
                raise ValueError(f"Baseline evidence hash mismatch: {path}")
        info = terminal(log, "R28_N5", algorithm, wireless)
        if info["status"] != "OPTIMAL" or not info["has_solution"]:
            raise ValueError(f"Baseline optimization incomplete: {log}")
        result[arm] = {"log": str(log), "audit": str(audit),
                       "plan": str(plan),
                       "selected_links": sum(line.startswith("facility ") for line in
                                             plan.read_text(encoding="utf-8").splitlines()),
                       **info}
    return result


def verify_variant(name, day, night, expected_hash=None):
    destination = ROOT / "data" / f"{name}_data"
    source_files = sorted(path for path in BASE.rglob("*") if path.is_file())
    if len(source_files) != len(CASE["dataset_files_sha256"]):
        raise ValueError("Unexpected base file count")
    changes = []
    for source in source_files:
        relative = source.relative_to(BASE)
        target = destination / relative
        if sha(source) != sha(target):
            changes.append(relative.as_posix())
    if changes != ["globalCfg.txt"]:
        raise ValueError(f"Unexpected variant changes: {name}: {changes}")
    raw = (destination / "globalCfg.txt").read_bytes()
    base_lines = (BASE / "globalCfg.txt").read_bytes().splitlines(keepends=True)
    variant_lines = raw.splitlines(keepends=True)
    if len(base_lines) != len(variant_lines) or \
            any(a != b for i, (a, b) in enumerate(zip(base_lines, variant_lines))
                if i != 2):
        raise ValueError(f"Unexpected variant bytes: {name}")
    actual = variant_lines[2].decode("ascii").strip().split(",")
    if Decimal(actual[0]) != day or Decimal(actual[1]) != night or \
            actual[2:] != base_lines[2].decode("ascii").strip().split(",")[2:]:
        raise ValueError(f"Unexpected tariff values: {name}")
    if expected_hash is not None and sha(destination / "globalCfg.txt") != expected_hash:
        raise ValueError(f"Variant hash changed since preparation: {name}")
    return sha(destination / "globalCfg.txt")


def prepare():
    full_source_hash = verify_base()
    baseline = selected_baseline()
    variants = {}
    base_raw = (BASE / "globalCfg.txt").read_bytes()
    if base_raw.count(b"\r\n") != base_raw.count(b"\n") or not base_raw.endswith(b"\r\n"):
        raise ValueError("Noncanonical base globalCfg.txt")
    original_row = base_raw.splitlines(keepends=True)[2]
    if original_row.decode("ascii").strip().split(",")[:2] != ["0.1922", "0.1233"]:
        raise ValueError("Unexpected base tariff")
    for token, factor in FACTORS:
        day, night = MEAN + HALF_SPREAD * factor, MEAN - HALF_SPREAD * factor
        if token == "s1":
            variants[token] = {"s": str(factor), "data": "R28_N5",
                               "day": str(day), "night": str(night),
                               "global_sha256": sha(BASE / "globalCfg.txt")}
            continue
        name = f"R28_N5_tariff_{token}"
        destination = ROOT / "data" / f"{name}_data"
        shutil.copytree(BASE, destination)
        row = original_row.decode("ascii").strip().split(",")
        row[:2] = [format(day, "f"), format(night, "f")]
        replacement = (",".join(row) + "\r\n").encode("ascii")
        (destination / "globalCfg.txt").write_bytes(base_raw.replace(original_row, replacement, 1))
        digest = verify_variant(name, day, night)
        manifest = {"data": name, "factor": str(factor), "day": str(day),
                    "night": str(night), "base_global_sha256": sha(BASE / "globalCfg.txt"),
                    "global_sha256": digest, "only_changed": "globalCfg.txt",
                    "other_files_unchanged": 23}
        (destination / "tariff_manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        variants[token] = {"s": str(factor), "data": name,
                           "day": str(day), "night": str(night),
                           "global_sha256": digest}
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d_%H-%M-%S_UTC")
    package = ROOT / "results" / f"tariff_spread_{stamp}"
    package.mkdir(parents=True, exist_ok=False)
    plan = {"created_utc": utc(), "case": "R28-N5-F50-C4",
            "variants": variants, "baseline": baseline,
            "full_executable": EVIDENCE["full_executable"],
            "full_source_sha256": full_source_hash,
            "ef_audit_executable": EVIDENCE["ef_audit_executable"],
            "solver_config_sha256": CASE["solver_config_sha256"],
            "tasks": [f"{token}_{arm}" for token, _ in FACTORS if token != "s1"
                      for arm in ("full", "ef")]}
    write_json(package / "plan.json", plan)
    write_json(package / "state.json", {"status": "PREPARED", "created_utc": utc(),
                                        "completed": [], "current": None})
    write_json(POINTER, {"package": str(package), "created_utc": utc()})
    print(str(package))


def args(algorithm, data, wireless, plan=None):
    result = ["--pb", "wireless_reform", "--alg", algorithm,
              "--data", data, "--scenario-count", "5", "--scenario-start", "0",
              "--cap", "4", "--battery-step", "50", "--dwc-top-n", "50",
              "--energy-scale", "1.0", "--facility-cost-scale", "1.0",
              "--wireless-option", str(wireless)]
    if plan is not None:
        result += ["--analysis-plan", str(plan)]
    return result


def execute(package, state, name, binary, arguments, algorithm, data, wireless,
            memory_guard=False):
    folder = package / "runs" / name
    folder.mkdir(parents=True, exist_ok=False)
    available = psutil.virtual_memory().available
    limit = min(20 * GIB, available - 4 * GIB) if memory_guard else None
    if memory_guard and limit <= 0:
        raise RuntimeError("Insufficient memory for guarded EF")
    state["current"] = {"name": name, "phase": "STARTING", "started_utc": utc()}
    write_json(package / "state.json", state)
    started = time.monotonic()
    with (folder / "stdout.txt").open("xb") as stdout, \
            (folder / "stderr.txt").open("xb") as stderr, \
            (folder / "resources.jsonl").open("x", encoding="utf-8") as resources:
        child = subprocess.Popen([str(binary), *arguments], cwd=ROOT,
                                 stdout=stdout, stderr=stderr,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        process = psutil.Process(child.pid)
        launch = {"name": name, "started_utc": utc(), "pid": child.pid,
                  "create_time": process.create_time(), "binary": str(binary),
                  "binary_sha256": sha(binary), "arguments": arguments,
                  "data_global_sha256": sha(ROOT / "data" / f"{data}_data/globalCfg.txt"),
                  "solver_config_sha256": sha(ROOT / "solverCfg.txt"),
                  "initial_available_bytes": available,
                  "private_limit_bytes": limit, "reserve_bytes": 4 * GIB if memory_guard else None}
        write_json(folder / "launch.json", launch)
        state["current"] = {"name": name, "phase": "RUNNING", "pid": child.pid,
                            "create_time": process.create_time(), "started_utc": launch["started_utc"]}
        write_json(package / "state.json", state)
        peak, guarded, last_sample = 0, False, -10
        while child.poll() is None:
            try:
                private = process.memory_info().private
            except psutil.NoSuchProcess:
                break
            free = psutil.virtual_memory().available
            elapsed = time.monotonic() - started
            peak = max(peak, private)
            if elapsed - last_sample >= 10:
                resources.write(json.dumps({"utc": utc(), "elapsed_wall_seconds": elapsed,
                                            "private_bytes": private,
                                            "available_bytes": free}) + "\n")
                resources.flush()
                last_sample = elapsed
            if memory_guard and (private > limit or free < 4 * GIB):
                guarded = True
                write_json(folder / "external_memory_limit.json", {
                    "utc": utc(), "private_bytes": private, "available_bytes": free,
                    "private_limit_bytes": limit})
                child.terminate()
                break
            time.sleep(1)
        exit_code = child.wait()
    output = (folder / "stdout.txt").read_text(encoding="utf-8", errors="replace")
    pointers = [line[10:].strip() for line in output.splitlines()
                if line.startswith("Log file: ")]
    if len(pointers) > 1:
        raise ValueError(f"Multiple solver log pointers: {name}")
    raw = None
    info = {"status": "UNRECORDED", "has_solution": False,
            "lb": None, "ub": None, "gap_percent": None}
    if pointers:
        native = ROOT / pointers[0]
        raw = folder / "raw.log"
        shutil.copyfile(native, raw)
        if not guarded:
            if algorithm == "plan-audit":
                lines = raw.read_text(encoding="utf-8", errors="replace").splitlines()
                info["status"] = "COMPLETE_OPTIMAL_RECOURSE" if \
                    "Plan audit status: COMPLETE_OPTIMAL_RECOURSE" in lines else "INCOMPLETE"
            else:
                info = terminal(raw, data, algorithm, wireless)
    if guarded:
        info["status"] = "MEMORY_LIMIT_EXTERNAL"
    result = {"name": name, "algorithm": algorithm, "data": data,
              "wireless": wireless, "raw_log": str(raw) if raw else None,
              "native_log": pointers[0] if pointers else None,
              "raw_sha256": sha(raw) if raw else None,
              "exit_code": exit_code, "actual_wall_seconds": time.monotonic() - started,
              "external_memory_guard": guarded, "peak_private_bytes": peak, **info}
    write_json(folder / "result.json", result)
    state["completed"].append(result)
    state["current"] = None
    write_json(package / "state.json", state)
    return result


def make_plan(log, destination):
    subprocess.run([sys.executable, str(ROOT / "scripts/analysis/prepare_fixed_plan.py"),
                    str(log), str(destination)], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return destination


def audit_metrics(path, source_log):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    source = [json.loads(x.split(": ", 1)[1]) for x in lines
              if x.startswith("Plan audit source: ")]
    totals = [json.loads(x.split(": ", 1)[1]) for x in lines
              if x.startswith("Plan audit total: ")]
    scenarios = [json.loads(x.split(": ", 1)[1]) for x in lines
                 if x.startswith("Plan audit scenario: ")]
    routes = [json.loads(x.split(": ", 1)[1]) for x in lines
              if x.startswith("Plan audit route: ")]
    if len(source) != 1 or source[0]["sha256"] != sha(source_log):
        raise ValueError(f"Mismatched plan audit source: {path}")
    if lines.count("Plan audit status: COMPLETE_OPTIMAL_RECOURSE") != 1:
        return {"audit_status": "INCOMPLETE", "day_grid_kwh": None,
                "night_grid_kwh": None, "weighted_battery_kwh": None,
                "audit_total": None, "audit_minus_original_ub": None}
    if len(totals) != 1 or len(scenarios) != 5 or len(routes) != 140:
        raise ValueError(f"Incomplete plan audit despite complete status: {path}")
    probability = {x["scenario"]: x["probability"] for x in scenarios}
    if abs(sum(probability.values()) - 1) > 1e-8:
        raise ValueError(f"Invalid scenario probabilities: {path}")
    grouped = {scenario: [x for x in routes if x["scenario"] == scenario]
               for scenario in probability}
    if any(len(values) != 28 for values in grouped.values()):
        raise ValueError(f"Incomplete scenario routes: {path}")
    day = sum(probability[s] * sum(x["plug_grid_kwh"] + x["dwc_grid_kwh"]
                                   for x in grouped[s]) for s in probability)
    night = sum(probability[s] * sum(x["night_grid_kwh"]
                                     for x in grouped[s]) for s in probability)
    battery = sum(probability[s] *
                  sum(x["buses"] * x["battery_kwh"] for x in grouped[s]) /
                  sum(x["buses"] for x in grouped[s]) for s in probability)
    return {"audit_status": "COMPLETE_OPTIMAL_RECOURSE",
            "day_grid_kwh": day, "night_grid_kwh": night,
            "weighted_battery_kwh": battery, "audit_total": totals[0]["total"],
            "audit_minus_original_ub": totals[0]["difference"]}


def report(package, plan, state):
    result = {x["name"]: x for x in state["completed"]}
    pair_rows, mechanism_rows = [], []
    for token, factor in FACTORS:
        variant = plan["variants"][token]
        arms = {}
        for arm in ("full", "ef"):
            item = plan["baseline"][arm] if token == "s1" else result[f"{token}_{arm}"]
            arms[arm] = item
            if token in ("s0", "s1", "s2"):
                audit = item.get("audit")
                metrics = audit_metrics(Path(audit), Path(item["raw_log"] if token != "s1" else item["log"])) \
                    if audit else {"audit_status": "MISSING", "day_grid_kwh": None,
                                   "night_grid_kwh": None,
                                   "weighted_battery_kwh": None,
                                   "audit_total": None,
                                   "audit_minus_original_ub": None}
                mechanism_rows.append({"s": str(factor), "arm": arm,
                                       "audit_log": audit, **metrics})
        dwc, nodwc = arms["full"], arms["ef"]
        valid = all(x is not None for x in
                    (dwc["lb"], dwc["ub"], nodwc["lb"], nodwc["ub"]))
        lower = nodwc["lb"] - dwc["ub"] if valid else None
        upper = nodwc["ub"] - dwc["lb"] if valid else None
        pair_rows.append({"s": str(factor), "day_price": variant["day"],
                          "night_price": variant["night"],
                          "dwc_status": dwc["status"], "dwc_lb": dwc["lb"],
                          "dwc_ub": dwc["ub"], "dwc_gap_percent": dwc["gap_percent"],
                          "nodwc_status": nodwc["status"], "nodwc_lb": nodwc["lb"],
                          "nodwc_ub": nodwc["ub"],
                          "nodwc_gap_percent": nodwc["gap_percent"],
                          "saving_lower": lower, "saving_upper": upper,
                          "certified_positive": lower > 0 if valid else None,
                          "dwc_links": dwc.get("selected_links"),
                          "dwc_log": dwc["log"] if token == "s1" else dwc["raw_log"],
                          "nodwc_log": nodwc["log"] if token == "s1" else nodwc["raw_log"]})
    for name, rows in (("paired_results.csv", pair_rows),
                       ("mechanism.csv", mechanism_rows)):
        with (package / name).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    first, last = pair_rows[0], pair_rows[-1]
    disjoint = first["saving_upper"] < last["saving_lower"] or \
        last["saving_upper"] < first["saving_lower"] if \
        all(x["saving_lower"] is not None for x in (first, last)) else None
    write_json(package / "interval_check.json", {
        "s0_s2_intervals_disjoint": disjoint,
        "s2_certified_positive": last["certified_positive"],
        "formula": "[LB_noDWC - UB_DWC, UB_noDWC - LB_DWC]",
        "bounds": "Original optimization logs; audit differences shown separately"})


def run(package):
    plan = json.loads((package / "plan.json").read_text(encoding="utf-8"))
    state = json.loads((package / "state.json").read_text(encoding="utf-8"))
    if state["status"] != "PREPARED" or state["completed"] or state["current"]:
        raise ValueError("Controller is not in its one-time prepared state")
    verify_base()
    selected_baseline()
    for token, factor in FACTORS:
        if token != "s1":
            item = plan["variants"][token]
            verify_variant(item["data"], Decimal(item["day"]),
                           Decimal(item["night"]), item["global_sha256"])
    if [p for p in psutil.process_iter(["name"]) if p.info["name"] == "wireless.exe"]:
        raise RuntimeError("Another wireless.exe is already running")
    lock = package / "controller.lock"
    with lock.open("x", encoding="utf-8") as stream:
        stream.write(json.dumps({"pid": os.getpid(), "created_utc": utc()}) + "\n")
    controller_source = package / "controller_source.py"
    shutil.copyfile(Path(__file__), controller_source)
    state["status"] = "RUNNING"
    state["controller"] = {"pid": os.getpid(),
                           "create_time": psutil.Process().create_time(),
                           "source_sha256": sha(controller_source)}
    write_json(package / "state.json", state)
    full_binary = ROOT / plan["full_executable"]["path"]
    ef_binary = ROOT / plan["ef_audit_executable"]["path"]
    for token, factor in FACTORS:
        if token == "s1":
            continue
        data = plan["variants"][token]["data"]
        for arm, binary, wireless in (("full", full_binary, 1),
                                      ("ef", ef_binary, 0)):
            name = f"{token}_{arm}"
            item = execute(package, state, name, binary,
                           args(arm, data, wireless), arm, data, wireless,
                           memory_guard=arm == "ef")
            if item["has_solution"] and item["ub"] is not None:
                fixed = make_plan(Path(item["raw_log"]),
                                  package / "runs" / name / "final.plan")
                item["plan"] = str(fixed)
                item["selected_links"] = sum(line.startswith("facility ") for line in
                                             fixed.read_text(encoding="utf-8").splitlines())
                if token in ("s0", "s2"):
                    audit = execute(package, state, name + "_audit", ef_binary,
                                    args("plan-audit", data, wireless, fixed),
                                    "plan-audit", data, wireless)
                    item["audit"] = audit["raw_log"]
            write_json(package / "runs" / name / "result.json", item)
            for index, completed in enumerate(state["completed"]):
                if completed["name"] == name:
                    state["completed"][index] = item
                    break
            write_json(package / "state.json", state)
    report(package, plan, state)
    state["status"] = "FINISHED"
    state["finished_utc"] = utc()
    write_json(package / "state.json", state)


def status(package):
    state = json.loads((package / "state.json").read_text(encoding="utf-8"))
    def alive(pid, creation):
        try:
            return abs(psutil.Process(pid).create_time() - creation) < 0.01
        except psutil.NoSuchProcess:
            return False

    controller = state.get("controller")
    controller_alive = bool(controller and alive(controller["pid"],
                                               controller["create_time"]))
    current = state["current"]
    solver_alive = bool(current and current.get("pid") and
                        alive(current["pid"], current["create_time"]))
    print(json.dumps({"status": state["status"], "controller_alive": controller_alive,
                      "solver_alive": solver_alive, "current": current,
                      "completed_optimizations": sum(x["algorithm"] != "plan-audit"
                                                     for x in state["completed"]),
                      "completed_audits": sum(x["algorithm"] == "plan-audit"
                                               for x in state["completed"]),
                      "last_completed": state["completed"][-1]["name"]
                      if state["completed"] else None}, ensure_ascii=False))


if __name__ == "__main__":
    if sys.argv[1] == "prepare" and len(sys.argv) == 2:
        prepare()
    elif sys.argv[1] == "run" and len(sys.argv) == 3:
        run(Path(sys.argv[2]).resolve())
    elif sys.argv[1] == "status" and len(sys.argv) == 3:
        status(Path(sys.argv[2]).resolve())
    else:
        raise ValueError("Usage: tariff_spread_experiment.py prepare|run|status")
