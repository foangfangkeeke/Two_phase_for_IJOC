"""Run the frozen experiment manifest once, preserving every outcome."""

import argparse
import datetime
import importlib.util
import json
import msvcrt
import os
import re
import subprocess
import threading
import time
import traceback
from pathlib import Path

import psutil

from verify import PACKAGE, ROOT, load, require, sha256, verify_inputs, verify_release
from report import render


STATE = PACKAGE / "status.json"
GIB = 1024 ** 3


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def save(path, data):
    temporary = path.with_name(path.name + ".tmp." + str(os.getpid()))
    temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    os.replace(temporary, path)


def update(**values):
    state = load(STATE)
    state.update(values)
    state["updated_utc"] = utc()
    save(STATE, state)


def identity(process):
    return {"pid": process.pid, "create_time": process.create_time(),
            "executable": process.exe(), "command": process.cmdline()}


def no_other_solver():
    live = [p.pid for p in psutil.process_iter(["name"])
            if (p.info["name"] or "").lower() == "wireless.exe"]
    require(not live, f"A wireless solver is already running: {live}")


def parser_module():
    path = ROOT / "scripts/experiments/summarize_group.py"
    spec = importlib.util.spec_from_file_location("frozen_summary", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def priority(job):
    main = any(record.startswith("main_table/") for record in job["records"])
    if main:
        rank = {"R3_N5": 0, "R12_N5": 1, "R28_N5": 3}[job["dataset"]]
    elif any(record.startswith("vi_validation/") for record in job["records"]):
        rank = 2
    else:
        rank = 4 if job["dataset"] == "R28_N5" else 5
    algorithm_order = {"ef": 0, "natural-ils": 1, "lifted-ils": 2,
                       "lifted-two-phase": 3, "full": 4}
    return rank, job["case_id"], algorithm_order[job["algorithm"]]


def run_job(job, release, summarize):
    verify_inputs()
    verify_release(check_commit=False)
    no_other_solver()
    directory = PACKAGE / "runs" / job["run_id"]
    directory.mkdir(parents=True)
    available = psutil.virtual_memory().available
    memory_limit = min(20 * GIB, max(0, available - 4 * GIB))
    environment = os.environ.copy()
    environment["PATH"] = str(ROOT / "out") + os.pathsep + environment["PATH"]
    command = [str(ROOT / release["executable"]), *job["arguments"]]
    state = load(STATE)
    state["jobs"][job["run_id"]] = "LAUNCHING"
    save(STATE, state)
    with (directory / "stdout.log").open("xb") as stdout, \
            (directory / "stderr.log").open("xb") as stderr, \
            (directory / "resources.jsonl").open("x", encoding="utf-8") as resources:
        started = utc()
        before = time.monotonic()
        child = subprocess.Popen(command, cwd=ROOT, env=environment,
                                 stdout=stdout, stderr=stderr,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        ended = threading.Event()
        completion = {}

        def wait_for_exit():
            completion["exit_code"] = child.wait()
            completion["monotonic"] = time.monotonic()
            completion["utc"] = utc()
            ended.set()

        waiter = threading.Thread(target=wait_for_exit)
        waiter.start()
        process = psutil.Process(child.pid)
        solver_identity = identity(process)
        launch = {"started_utc": started, "run_id": job["run_id"],
                  "source_commit": release["source_commit"],
                  "executable_sha256": release["executable_sha256"],
                  "solver": solver_identity, "arguments": job["arguments"],
                  "memory_guard": ({"limit_bytes": memory_limit,
                                    "initial_available_bytes": available,
                                    "reserve_bytes": 4 * GIB}
                                   if job["algorithm"] == "ef" else None)}
        save(directory / "launch.json", launch)
        state = load(STATE)
        state["jobs"][job["run_id"]] = "RUNNING"
        state.update(current=launch, updated_utc=utc())
        save(STATE, state)
        render(load(PACKAGE / "runs.json"), state)
        guarded = False
        peak_private = 0
        last_sample = -10.0
        while not ended.is_set():
            elapsed = time.monotonic() - before
            try:
                memory = process.memory_info()
            except psutil.NoSuchProcess:
                break
            private = memory.private
            free = psutil.virtual_memory().available
            peak_private = max(peak_private, private)
            if elapsed - last_sample >= 10:
                sample = {"utc": utc(), "elapsed_wall_seconds": elapsed,
                          "private_bytes": private, "available_bytes": free}
                resources.write(json.dumps(sample) + "\n")
                resources.flush()
                update(last_resource_sample=sample)
                last_sample = elapsed
            if (job["algorithm"] == "ef" and
                    (private > memory_limit or free < 4 * GIB) and child.poll() is None):
                guarded = True
                save(directory / "external_memory_limit.json", {
                    "utc": utc(), "private_bytes": private, "available_bytes": free,
                    "limit_bytes": memory_limit, "reserve_bytes": 4 * GIB,
                    "solver": solver_identity})
                child.terminate()
                break
            ended.wait(1)
        waiter.join()
        code = completion["exit_code"]
    wall = completion["monotonic"] - before
    stdout_text = (directory / "stdout.log").read_text(encoding="utf-8-sig")
    match = re.search(r"^Log file: (.+)$", stdout_text, re.MULTILINE)
    require(match is not None, "Solver did not record its native log path")
    native_log = (ROOT / match.group(1).strip()).resolve()
    require(native_log.is_relative_to((ROOT / "logs").resolve()),
            "Native log path escapes the log directory")
    raw = native_log.read_bytes()
    (directory / "solver.log").write_bytes(raw)
    summary = summarize(directory / "solver.log", job["algorithm"])
    native_final = summary["status"] if summary["final_status_recorded"] == "1" else None
    status = "MEMORY_LIMIT_EXTERNAL" if guarded else (
        native_final if native_final is not None else "UNKNOWN_EXIT_UNRECORDED")
    result = {"run_id": job["run_id"], "records": job["records"],
              "started_utc": started, "ended_utc": completion["utc"],
              "actual_wall_seconds": wall, "exit_code": code,
              "status": status, "algorithm_final_status": native_final,
              "summary": summary, "peak_private_bytes": peak_private,
              "source_commit": release["source_commit"],
              "executable_sha256": release["executable_sha256"],
              "raw_log_sha256": sha256(directory / "solver.log"),
              "solver": solver_identity}
    save(directory / "result.json", result)
    verify_inputs()
    verify_release(check_commit=False)
    return result


def initialize():
    plan = verify_inputs()
    release = verify_release()
    state = {"state": "READY", "created_utc": utc(), "updated_utc": utc(),
             "source_commit": release["source_commit"],
             "executable_sha256": release["executable_sha256"],
             "jobs": {job["run_id"]: "PENDING" for job in plan["jobs"]},
             "completed_runs": [], "completed_result_records": 0}
    with STATE.open("x", encoding="utf-8") as stream:
        json.dump(state, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    render(plan, state)


def execute(resume):
    plan = verify_inputs()
    release = verify_release()
    no_other_solver()
    state = load(STATE)
    require(state["state"] == "READY" or (resume and state["state"] == "STOPPED_ERROR"),
            "Controller is not ready; interrupted attempts require explicit reconciliation")
    require(all(value in ("PENDING", "COMPLETE") for value in state["jobs"].values()),
            "An interrupted launch/run must be preserved and reconciled first")
    with (PACKAGE / "controller.lock").open("a+b") as lock:
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        controller = identity(psutil.Process())
        update(state="RUNNING", controller=controller)
        summary = parser_module().summarize
        try:
            for job in sorted(plan["jobs"], key=priority):
                if load(STATE)["jobs"][job["run_id"]] == "COMPLETE":
                    continue
                result = run_job(job, release, summary)
                state = load(STATE)
                state["jobs"][job["run_id"]] = "COMPLETE"
                state["completed_runs"].append(job["run_id"])
                state["completed_result_records"] += len(job["records"])
                state.update(last_result=result, current=None, updated_utc=utc())
                save(STATE, state)
                render(plan, state)
                require(result["status"] != "UNKNOWN_EXIT_UNRECORDED",
                        "Unknown exit retained; review before continuing")
            update(state="COMPLETE_RUNS_PENDING_INDEPENDENT_REVIEW", ended_utc=utc())
        except BaseException:
            update(state="STOPPED_ERROR", controller_error=traceback.format_exc(),
                   controller_stopped_utc=utc())
            raise
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["init", "run", "status"])
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.action == "init":
        initialize()
    elif args.action == "run":
        execute(args.resume)
    else:
        state = load(STATE)
        current = state.get("current")
        if current is not None:
            expected = current["solver"]
            state["solver_identity_live"] = any(
                p.pid == expected["pid"] and abs(p.info["create_time"] - expected["create_time"]) < .001
                for p in psutil.process_iter(["create_time"]))
        print(json.dumps(state, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
