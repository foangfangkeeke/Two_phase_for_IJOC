"""Independently audit completed frozen runs; never import a runner or parser."""

import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess


PACKAGE = Path(__file__).resolve().parents[1]
ROOT = PACKAGE.parents[1]
NUMBER = r"[-+0-9.eEinfINF]+"
TERMINAL = re.compile(
    r"^Algorithm termination: algorithm=([^,]+), status=([^,]+), has_solution=([01]), "
    rf"UB=({NUMBER}), LB=({NUMBER}), gap_percent=({NUMBER}), reported_seconds=({NUMBER})$")
ROUND = re.compile(
    r"^(?:Natural ILS|Lifted ILS|Phase 2) iter (\d+),.*?"
    rf"UB=({NUMBER}), LB=({NUMBER}),.*?elapsed_ms=({NUMBER})$")


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(condition, message):
    if not condition:
        raise ValueError(message)


def equal(actual, expected):
    return math.isclose(float(actual), float(expected), rel_tol=1e-9, abs_tol=1e-5)


def fields(line):
    return dict(item.split("=", 1) for item in line.split(": ", 1)[1].split(", "))


def main():
    release = read(PACKAGE / "release.json")
    plan = read(PACKAGE / "runs.json")
    state = read(PACKAGE / "status.json")
    datasets = read(PACKAGE / "datasets.json")["datasets"]
    check(digest(ROOT / release["executable"]) == release["executable_sha256"],
          "Frozen executable differs")
    for name, expected in release["source_files_sha256"].items():
        check(digest(ROOT / name) == expected, f"Source bytes changed: {name}")
        blob = subprocess.check_output(["git", "show", release["source_commit"] + ":" + name], cwd=ROOT)
        check(hashlib.sha256(blob).hexdigest() == expected, f"Source commit differs: {name}")
    for name, expected in release["copt_dll_sha256"].items():
        check(digest(ROOT / "out" / name) == expected, f"COPT DLL differs: {name}")
    for dataset in datasets.values():
        for name, expected in dataset["files"].items():
            check(digest(ROOT / name) == expected, f"Actual input changed: {name}")

    jobs = {job["run_id"]: job for job in plan["jobs"]}
    audited = []
    for run_id in state["completed_runs"]:
        job = jobs[run_id]
        directory = PACKAGE / "runs" / run_id
        result, launch = read(directory / "result.json"), read(directory / "launch.json")
        options = dict(zip(job["arguments"][::2], job["arguments"][1::2]))
        check(launch["arguments"] == job["arguments"] == launch["solver"]["command"][1:],
              f"Command differs: {run_id}")
        for record in (launch, result):
            check(record["source_commit"] == release["source_commit"], f"Source version differs: {run_id}")
            check(record["executable_sha256"] == release["executable_sha256"], f"Executable version differs: {run_id}")
        check(Path(launch["solver"]["executable"]).resolve() == (ROOT / release["executable"]).resolve(),
              f"Actual executable path differs: {run_id}")
        check(digest(directory / "solver.log") == result["raw_log_sha256"], f"Raw log differs: {run_id}")
        lines = (directory / "solver.log").read_text(encoding="utf-8-sig").splitlines()
        runtime_lines = [line for line in lines if line.startswith("Runtime options: ")]
        check(len(runtime_lines) == 1, f"Missing/duplicate runtime header: {run_id}")
        runtime = fields(runtime_lines[0])
        expected_runtime = {"facility_cost_scale": "--facility-cost-scale",
                            "energy_scale": "--energy-scale", "battery_step": "--battery-step",
                            "scenario_count": "--scenario-count", "scenario_start": "--scenario-start",
                            "cap": "--cap", "dwc_top_n": "--dwc-top-n", "wireless_option": "--wireless-option",
                            "rsp": "--rsp", "rsp_workers": "--rsp-workers", "rsp_threads": "--rsp-threads",
                            "preprocess_bounds": "--preprocess-bounds", "valid_inequality": "--valid-inequality"}
        check(all(equal(runtime[key], options[option]) for key, option in expected_runtime.items()),
              f"Raw runtime options differ: {run_id}")
        solver_lines = [line for line in lines if line.startswith("Solver options: ")]
        check(len(solver_lines) == 1, f"Missing/duplicate solver header: {run_id}")
        solver = fields(solver_lines[0])
        check(all(equal(solver[key], value) for key, value in
                  {"mip_gap": .0001, "global_gap": .01, "time_limit": 7200, "threads": 12}.items()),
              f"Raw solver settings differ: {run_id}")
        expected_instance = ("Instance: data=" + job["dataset"] + ", scenario_folders=" +
                             ";".join(datasets[job["dataset"]]["scenario_folders"]) + ", battery_range=")
        check(sum(line.startswith(expected_instance) for line in lines) == 1,
              f"Actual selected scenarios differ: {run_id}")
        check(lines[0] == "Execution policy: version=final-rsp-skip-2026-11-05, "
              "rsp_numerical=skip_screening, original_subproblem=required",
              f"Raw policy version differs: {run_id}")

        rounds = [(index + 1, ROUND.fullmatch(line)) for index, line in enumerate(lines)]
        rounds = [(line, match) for line, match in rounds if match is not None]
        check([int(match[1]) for _, match in rounds] == list(range(len(rounds))),
              f"Incomplete/duplicate iteration sequence: {run_id}")
        prior_lb, prior_ub = -math.inf, math.inf
        for _, match in rounds:
            ub, lb = float(match[2]), float(match[3])
            check(lb + 1e-5 >= prior_lb and ub <= prior_ub + 1e-5,
                  f"LB decrease or incumbent increase: {run_id}, iter {match[1]}")
            prior_lb, prior_ub = lb, ub
        terminals = [(index + 1, TERMINAL.fullmatch(line)) for index, line in enumerate(lines)]
        terminals = [(line, match) for line, match in terminals if match is not None]
        check(len(terminals) <= 1, f"Duplicate terminal record: {run_id}")
        evidence = {"run_id": run_id, "records": job["records"], "complete_iterations": len(rounds),
                    "raw_log_sha256": result["raw_log_sha256"],
                    "actual_wall_seconds": result["actual_wall_seconds"], "exit_code": result["exit_code"],
                    "status": result["status"], "last_complete_line": rounds[-1][0] if rounds else None}
        if result["status"] == "MEMORY_LIMIT_EXTERNAL":
            guard = read(directory / "external_memory_limit.json")
            check(job["algorithm"] == "ef" and (guard["private_bytes"] > guard["limit_bytes"] or
                  guard["available_bytes"] < guard["reserve_bytes"]), f"No external memory evidence: {run_id}")
            if not terminals:
                check(result["summary"]["ub"] == result["summary"]["lb"] == "",
                      f"Invented external-memory bounds: {run_id}")
        else:
            check(len(terminals) == 1, f"Final algorithm record missing: {run_id}")
        if terminals:
            line, terminal = terminals[0]
            algorithm, status, has_solution, ub, lb, gap, seconds = terminal.groups()
            check(algorithm == job["algorithm"] and status == result["algorithm_final_status"],
                  f"Final status differs: {run_id}")
            check(equal(ub, result["summary"]["ub"]) and equal(lb, result["summary"]["lb"]),
                  f"Raw/final summary bounds differ: {run_id}")
            check(float(lb) + 1e-5 >= prior_lb and float(ub) <= prior_ub + 1e-5,
                  f"Final bounds worsen: {run_id}")
            check(bool(int(has_solution)) == math.isfinite(float(ub)), f"Incumbent marker differs: {run_id}")
            if math.isfinite(float(ub)) and math.isfinite(float(lb)):
                calculated_gap = (float(ub) - float(lb)) / max(1.0, abs(float(ub))) * 100
                check(equal(gap, calculated_gap), f"Original gap differs: {run_id}")
                check(float(lb) <= float(ub) + 1e-3, f"LB above original UB: {run_id}")
            check(equal(seconds, result["summary"]["solver_elapsed_seconds"]), f"Algorithm time differs: {run_id}")
            check(result["actual_wall_seconds"] + .1 >= float(seconds), f"Wall time below algorithm time: {run_id}")
            evidence.update(terminal_line=line, ub=ub, lb=lb, gap_percent=gap,
                            final_bound_source="EXPLICIT_ORIGINAL_FINAL")
        elapsed = (datetime.datetime.fromisoformat(result["ended_utc"]) -
                   datetime.datetime.fromisoformat(result["started_utc"])).total_seconds()
        check(abs(elapsed - result["actual_wall_seconds"]) < .1, f"Wall-clock record differs: {run_id}")
        evidence["last_complete_raw"] = lines[rounds[-1][0] - 1] if rounds else None
        evidence["terminal_raw"] = lines[terminals[-1][0] - 1] if terminals else None
        audited.append(evidence)

    now = datetime.datetime.now(datetime.timezone.utc)
    directory = PACKAGE / "provenance" / ("raw_review_" + now.strftime("%Y-%m-%d_%H-%M-%S_%f_UTC"))
    directory.mkdir()
    review = {"reviewed_utc": now.isoformat(), "independent_of_controller_and_parser": True,
              "solver_calls": 0, "review_script_sha256": digest(Path(__file__)),
              "source_commit": release["source_commit"], "exe_sha256": release["executable_sha256"],
              "reviewed_runs": len(audited), "reviewed_result_records": sum(len(item["records"]) for item in audited),
              "pending_runs": len(jobs) - len(audited), "mismatches": 0,
              "all_frozen_runs_reviewed": len(audited) == len(jobs), "records": audited}
    (directory / "review.json").write_text(json.dumps(review, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    pointer = PACKAGE / "raw_review_current.json"
    temporary = pointer.with_suffix(".tmp")
    temporary.write_text(json.dumps({key: value for key, value in review.items() if key != "records"}
                                   | {"review": str(directory / "review.json")}, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, pointer)
    print(json.dumps({key: value for key, value in review.items() if key != "records"}, indent=2))


if __name__ == "__main__":
    main()
