"""Run one R28-N5-F50-C{3,4} no-DWC EF baseline with a recorded memory guard.

Usage: python scripts/analysis/run_guarded_no_dwc_ef.py 4 [R28_N5_battery0p75]
"""

import hashlib
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import psutil


ROOT = Path(__file__).resolve().parents[2]
GIB = 1024 ** 3


def now() -> str:
    return datetime.now(timezone.utc).isoformat()


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    cap = int(sys.argv[1])
    data_name = sys.argv[2] if len(sys.argv) == 3 else "R28_N5"
    if cap not in (3, 4):
        raise ValueError("Only C3 and C4 are in the Section 5.3 baseline plan")
    case = json.loads((ROOT / "experiments/final_2026-11-07/cases.json").read_text(
        encoding="utf-8"))["cases"][f"R28-N5-F50-C{cap}"]
    for relative, expected in case["dataset_files_sha256"].items():
        if digest(ROOT / relative) != expected:
            raise ValueError(f"Input hash mismatch: {relative}")
    if digest(ROOT / "solverCfg.txt") != case["solver_config_sha256"]:
        raise ValueError("solverCfg.txt hash mismatch")
    if data_name != "R28_N5":
        variant = ROOT / "data" / f"{data_name}_data"
        manifest = json.loads((variant / "cost_case_manifest.json").read_text(
            encoding="utf-8"))
        if manifest["data_name"] != data_name or manifest["kind"] != "battery":
            raise ValueError("No-DWC EF variant must be a battery-cost case")
        base = ROOT / "data/R28_N5_data"
        for source in base.rglob("*"):
            if not source.is_file():
                continue
            target = variant / source.relative_to(base)
            if source.relative_to(base) == Path("globalCfg.txt"):
                if digest(target) != manifest["variant_global_sha256"]:
                    raise ValueError("Variant globalCfg.txt hash mismatch")
            elif digest(source) != digest(target):
                raise ValueError(f"Unexpected variant input change: {target}")
    binary = ROOT / "out/wireless.exe"
    arguments = [
        "--pb", "wireless_reform", "--alg", "ef", "--data", data_name,
        "--scenario-count", "5", "--scenario-start", "0", "--cap", str(cap),
        "--battery-step", "50", "--dwc-top-n", "50", "--energy-scale", "1.0",
        "--facility-cost-scale", "1.0", "--wireless-option", "0",
    ]
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d_%H-%M-%S_UTC")
    run_name = f"no_dwc_ef_{data_name}_C{cap}_{stamp}"
    output = ROOT / "results/section53" / run_name
    output.mkdir(parents=True, exist_ok=False)
    available = psutil.virtual_memory().available
    limit = min(20 * GIB, available - 4 * GIB)
    if limit <= 0:
        raise RuntimeError("Insufficient available memory for guarded EF")
    started = now()
    start = time.monotonic()
    with (output / "stdout.log").open("xb") as stdout, \
            (output / "stderr.log").open("xb") as stderr, \
            (output / "resources.jsonl").open("x", encoding="utf-8") as resources:
        child = subprocess.Popen([str(binary), *arguments], cwd=ROOT,
                                 stdout=stdout, stderr=stderr,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        process = psutil.Process(child.pid)
        save(output / "launch.json", {
            "started_utc": started, "pid": child.pid,
            "create_time": process.create_time(),
            "binary": str(binary), "binary_sha256": digest(binary),
            "arguments": arguments, "case": case["case_id"],
            "data_name": data_name,
            "input_hashes_verified": len(case["dataset_files_sha256"]),
            "solver_config_sha256": case["solver_config_sha256"],
            "initial_available_bytes": available, "private_limit_bytes": limit,
            "reserve_bytes": 4 * GIB,
        })
        peak_private = 0
        guarded = False
        last_sample = -10.0
        while child.poll() is None:
            try:
                private = process.memory_info().private
            except psutil.NoSuchProcess:
                break
            free = psutil.virtual_memory().available
            elapsed = time.monotonic() - start
            peak_private = max(peak_private, private)
            if elapsed - last_sample >= 10.0:
                resources.write(json.dumps({"utc": now(),
                    "elapsed_wall_seconds": elapsed, "private_bytes": private,
                    "available_bytes": free}) + "\n")
                resources.flush()
                last_sample = elapsed
            if private > limit or free < 4 * GIB:
                guarded = True
                save(output / "external_memory_limit.json", {
                    "utc": now(), "private_bytes": private,
                    "available_bytes": free, "limit_bytes": limit})
                child.terminate()
                break
            time.sleep(1.0)
        exit_code = child.wait()
    save(output / "exit.json", {
        "started_utc": started, "ended_utc": now(),
        "actual_wall_seconds": time.monotonic() - start,
        "exit_code": exit_code,
        "external_memory_guard_triggered": guarded,
        "peak_private_bytes": peak_private,
        "status": "MEMORY_LIMIT_EXTERNAL" if guarded else "INSPECT_NATIVE_LOG",
    })
    print(f"run={output} exit={exit_code} guarded={guarded}")


if __name__ == "__main__":
    main()
