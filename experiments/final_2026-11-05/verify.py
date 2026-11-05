"""Verify frozen input bytes, paired controls, and the released solver."""

import argparse
import hashlib
import json
import subprocess
from collections import Counter, defaultdict
from pathlib import Path


PACKAGE = Path(__file__).resolve().parent
ROOT = PACKAGE.parents[1]


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def options(job):
    arguments = job["arguments"]
    require(len(arguments) % 2 == 0, "Options must be explicit pairs")
    result = dict(zip(arguments[::2], arguments[1::2]))
    require(len(result) * 2 == len(arguments), "Duplicate runtime option")
    return result


def verify_inputs():
    datasets = load(PACKAGE / "datasets.json")["datasets"]
    require(set(datasets) == {
        "R3_N5", "R12_N5", "R28_N5", "R28_N10", "R28_N20", "R28_N40"
    }, "Unexpected dataset names")
    for name, dataset in datasets.items():
        require(len(dataset["files"]) == 4 * dataset["scenario_count"] + 4,
                f"Incomplete input file inventory: {name}")
        for relative, expected in dataset["files"].items():
            require(sha256(ROOT / relative) == expected, f"Input changed: {relative}")
        require(dataset["scenario_count"] == len(dataset["scenario_folders"]),
                f"Incorrect scenario count: {name}")
        rows = [line.strip().split(",") for line in
                (ROOT / "data" / (name + "_data") / "scenarioCfg.txt").read_text(
                    encoding="utf-8-sig").splitlines()
                if line.strip() and not line.lstrip().startswith("#")]
        require(int(rows[0][0]) == len(rows) - 1 == dataset["scenario_count"],
                f"Scenario configuration does not match dataset name: {name}")
        require([row[0] for row in rows[1:]] == dataset["scenario_folders"],
                f"Scenario order differs: {name}")
        require(all(abs(float(row[1]) - 1 / dataset["scenario_count"]) < 1e-12
                    for row in rows[1:]), f"Scenario probabilities differ: {name}")

    cases = load(PACKAGE / "cases.json")["cases"]
    for case in cases.values():
        require(sha256(ROOT / "solverCfg.txt") == case["solver_config_sha256"],
                "Solver configuration changed")
        require(case["dataset_files_sha256"] == datasets[case["dataset"]]["files"],
                "Case input inventory differs from its dataset")
    configuration = {}
    for line in (ROOT / "solverCfg.txt").read_text(encoding="utf-8-sig").splitlines():
        value = line.split("#", 1)[0].strip()
        if value:
            key, setting = value.split("=", 1)
            configuration[key.strip()] = setting.strip()
    for key, expected in {"mipGap": .0001, "globalGap": .01,
                          "timeLimit": 7200, "threads": 12}.items():
        require(float(configuration[key]) == expected, f"Unexpected {key}")

    plan = load(PACKAGE / "runs.json")
    records, jobs = plan["records"], {j["run_id"]: j for j in plan["jobs"]}
    require(len(jobs) == plan["unique_runs"] == 58, "Run count changed")
    require(Counter(r["group"] for r in records) == {
        "main_table": 45, "vi_validation": 2, "r28_full_l2p_comparison": 18
    }, "Result scope changed")
    require(len({r["record_id"] for r in records}) == 65, "Duplicate result record")
    for job in jobs.values():
        opts = options(job)
        case = cases[job["case_id"]]
        require(opts["--data"] == job["dataset"] == case["dataset"], "Dataset mismatch")
        require(opts["--alg"] == job["algorithm"], "Algorithm mismatch")
        expected = {"--scenario-count": case["parameters"]["N"],
                    "--scenario-start": case["parameters"]["scenario_start"],
                    "--cap": case["parameters"]["C"],
                    "--dwc-top-n": case["parameters"]["F"],
                    "--battery-step": case["parameters"]["battery_step"],
                    "--energy-scale": case["parameters"]["energy_scale"],
                    "--facility-cost-scale": case["parameters"]["facility_cost_scale"]}
        require(all(float(opts[key]) == value for key, value in expected.items()),
                "Case parameters differ from command")
        require(int(opts["--scenario-count"]) == datasets[job["dataset"]]["scenario_count"],
                "A job selects only part of the named dataset")
        require(opts["--rsp"] == ("0" if job["algorithm"] == "ef" else "1"),
                "Unexpected RSP setting")
        require(opts["--valid-inequality"] in ("0", "1"), "Invalid VI setting")
        require(opts["--rsp-workers"] == "3" and opts["--rsp-threads"] == "4",
                "Thread allocation differs")
        require(opts["--wireless-option"] == "1" and opts["--preprocess-bounds"] == "0",
                "Problem settings differ")
        require(job["settings"]["global_gap"] == .01 and
                job["settings"]["time_limit_seconds"] == 7200,
                "Algorithm budget differs")
        require(job["settings"]["rsp_numerical"] == "skip_screening_no_retry" and
                job["settings"]["original_subproblem"] == "required",
                "Numerical policy differs")

    pairs = defaultdict(list)
    for record in records:
        job = jobs[record["run_id"]]
        require(record["record_id"] in job["records"], "Missing run alias")
        if record["group"] != "main_table":
            pairs[(record["group"], record["case_id"])].append(options(job))
    for (group, case), pair in pairs.items():
        require(len(pair) == 2, f"Unpaired experiment: {case}")
        a, b = pair
        different = {key for key in a.keys() | b.keys() if a.get(key) != b.get(key)}
        expected = {"--valid-inequality"} if group == "vi_validation" else {"--alg"}
        require(different == expected, f"Confounded pair: {group}/{case}: {different}")
    require("R28-N5-F50-C2" in cases, "Missing C2 case")
    return plan


def verify_history():
    history = load(PACKAGE / "history/records.json")
    counts = Counter(record["version"] for record in history["completed_records"])
    require(counts == {"baseline": 58, "diagnostic_repair": 1, "production_repair": 5},
            "Historical record inventory changed")
    for record in history["completed_records"]:
        require(sha256(ROOT / record["raw_log"]) == record["raw_log_sha256"],
                "Historical raw log changed")
        require(sha256(ROOT / record["input_snapshot"]) == record["snapshot_sha256"],
                "Historical input snapshot changed")
    for version in history["versions"].values():
        require(sha256(ROOT / version["source_snapshot"]) == version["source_snapshot_sha256"],
                "Historical source archive changed")
        require(sha256(ROOT / version["executable"]) == version["executable_sha256"],
                "Historical executable changed")


def verify_release(check_commit=True):
    release = load(PACKAGE / "release.json")
    for relative, expected in release["source_files_sha256"].items():
        require(sha256(ROOT / relative) == expected, f"Frozen source changed: {relative}")
        if check_commit:
            data = subprocess.check_output(
                ["git", "show", release["source_commit"] + ":" + relative], cwd=ROOT)
            require(hashlib.sha256(data).hexdigest() == expected,
                    f"Committed source differs: {relative}")
    require(sha256(ROOT / release["executable"]) == release["executable_sha256"],
            "Released executable changed")
    for name, expected in release["copt_dll_sha256"].items():
        require(sha256(ROOT / "out" / name) == expected, f"COPT runtime differs: {name}")
    return release


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true")
    parser.add_argument("--release", action="store_true")
    args = parser.parse_args()
    plan = verify_inputs()
    if args.history:
        verify_history()
    if args.release:
        verify_release()
    print(json.dumps({"verified": True, "records": plan["record_count"],
                      "unique_runs": plan["unique_runs"]}))


if __name__ == "__main__":
    main()
