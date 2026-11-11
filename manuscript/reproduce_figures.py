"""Verify retained experiment evidence and reproduce the Chapter 5 figures."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.metadata
import json
import math
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
ALGORITHM_REF = "21969663eb908bb8390fdca29a4bad26af675737"
PLANNING_REF = "1f7cf15a4a1a48591c54367a74ec4fe85c4d9d28"
PACKAGE = ROOT / "experiments/final_2026-11-07"
PDF_FIGURES = (
    "chapter5_vi_progress.pdf", "chapter5_l2p_progress.pdf",
    "chapter5_repair_modes.pdf", "chapter5_planning_adjustments.pdf",
    "chapter5_cost_sensitivity.pdf", "chapter5_repair_phase2.pdf",
)
PAPER_FIGURES = (*PDF_FIGURES[:5], "chapter5_station_visits.png",
                 "chapter5_network_map.png")


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def check_close(actual: float, expected: float, label: str,
                tolerance: float = 1e-6) -> None:
    if not math.isclose(actual, expected, rel_tol=0, abs_tol=tolerance):
        raise ValueError(f"{label}: {actual} != {expected}")


def verify_tariff_evidence() -> dict:
    package = ROOT / "experiments/tariff_spread_2026-11-10"
    manifest = json.loads((package / "published_manifest.json").read_text(encoding="utf-8"))
    for relative, entry in manifest["files"].items():
        path = (package / relative).resolve()
        if not path.is_relative_to(package) or path.stat().st_size != entry["bytes"]:
            raise ValueError(f"Invalid tariff evidence path or size: {relative}")
        if digest(path) != entry["sha256"]:
            raise ValueError(f"Tariff evidence checksum mismatch: {relative}")
    for key in ("full_executable", "ef_audit_executable"):
        entry = manifest[key]
        if digest(ROOT / entry["path"]) != entry["sha256"]:
            raise ValueError(f"Archived tariff executable mismatch: {key}")

    pairs = read_csv(package / "paired_results_portable.csv")
    if [float(row["s"]) for row in pairs] != [0, 0.5, 1, 1.5, 2]:
        raise ValueError("Tariff price-level coverage mismatch")
    for row in pairs:
        level = float(row["s"])
        check_close(float(row["day_price"]), 0.15775 + 0.03445 * level, "Day price")
        check_close(float(row["night_price"]), 0.15775 - 0.03445 * level, "Night price")
        for arm in ("dwc", "nodwc"):
            log = ROOT / row[f"{arm}_log"]
            finals = [line for line in log.read_text(encoding="utf-8").splitlines()
                      if line.startswith("Algorithm termination:")]
            if len(finals) != 1:
                raise ValueError(f"Missing or ambiguous optimization bounds: {log}")
            fields = dict(re.findall(r"([A-Za-z_]\w*)=([^,\s]+)", finals[0]))
            if fields["status"] != row[f"{arm}_status"]:
                raise ValueError(f"Tariff terminal status mismatch: {log}")
            for key in ("lb", "ub"):
                check_close(float(fields[key.upper()]), float(row[f"{arm}_{key}"]),
                            f"Tariff {level} {arm} {key}")
            lb, ub = float(row[f"{arm}_lb"]), float(row[f"{arm}_ub"])
            check_close(float(row[f"{arm}_gap_percent"]), 100 * (ub - lb) / ub,
                        f"Tariff {level} {arm} gap", 1e-5)
        lower = float(row["nodwc_lb"]) - float(row["dwc_ub"])
        upper = float(row["nodwc_ub"]) - float(row["dwc_lb"])
        check_close(float(row["saving_lower"]), lower, "Saving lower bound")
        check_close(float(row["saving_upper"]), upper, "Saving upper bound")
        if row["certified_positive"] != str(lower > 0):
            raise ValueError(f"Tariff positivity mismatch at {level}")

    mechanisms = read_csv(package / "mechanism_portable.csv")
    expected = {(level, arm) for level in (0, 1, 2) for arm in ("full", "ef")}
    if len(mechanisms) != 6 or {(float(r["s"]), r["arm"]) for r in mechanisms} != expected:
        raise ValueError("Tariff mechanism coverage mismatch")
    for row in mechanisms:
        if row["audit_status"] != "COMPLETE_OPTIMAL_RECOURSE":
            raise ValueError("Incomplete tariff fixed-plan audit")
        if not (ROOT / row["audit_log"]).is_file():
            raise FileNotFoundError(row["audit_log"])
        for field in ("day_grid_kwh", "night_grid_kwh", "weighted_battery_kwh"):
            if not math.isfinite(float(row[field])) or float(row[field]) <= 0:
                raise ValueError(f"Invalid tariff operating quantity: {field}")
        pair = next(p for p in pairs if float(p["s"]) == float(row["s"]))
        arm = "dwc" if row["arm"] == "full" else "nodwc"
        check_close(float(row["audit_total"]) - float(pair[f"{arm}_ub"]),
                    float(row["audit_minus_original_ub"]), "Fixed-plan audit difference", 2e-6)
    positive = float(pairs[-1]["saving_lower"]) > 0
    disjoint = float(pairs[0]["saving_lower"]) > float(pairs[-1]["saving_upper"])
    if not positive or not disjoint:
        raise ValueError("Published tariff interval conclusions do not hold")
    return {"verified_package_files": len(manifest["files"]), "price_levels": len(pairs),
            "mechanism_records": len(mechanisms), "s2_certified_positive": positive,
            "s0_s2_intervals_disjoint": disjoint}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "manuscript/figures")
    parser.add_argument("--report-dir", type=Path, default=ROOT / "outputs/figure_reproduction")
    args = parser.parse_args()
    output, report = args.output_dir.resolve(), args.report_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report.mkdir(parents=True, exist_ok=True)
    completion = report / "report.json"
    if completion.exists():
        completion.unlink()
    commands = []

    def run(name: str, script: str, *arguments: str | Path) -> None:
        command = [sys.executable, str(ROOT / script), *(str(a) for a in arguments)]
        environment = dict(os.environ, PYTHONIOENCODING="utf-8", MPLBACKEND="Agg")
        result = subprocess.run(command, cwd=ROOT, env=environment,
                                capture_output=True, text=True, encoding="utf-8")
        log = report / f"{name}.log"
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name} failed (exit {result.returncode}); see {log}")
        commands.append({"step": name, "command": command, "log": str(log), "exit_code": 0})
        print(f"OK: {name}", flush=True)

    run("algorithm_audit", "manuscript/chapter5_analysis/analyze.py",
        "--package", PACKAGE, "--source-commit", ALGORITHM_REF,
        "--output-dir", report / "analysis")
    regenerated = report / "planning_exports"
    run("planning_exports", "scripts/analysis/export_section53.py", "--output-dir", regenerated)
    export_manifest = json.loads((regenerated / "export_manifest.json").read_text(encoding="utf-8"))
    for name, entry in export_manifest["tables"].items():
        frozen = subprocess.check_output(
            ["git", "show", f"{PLANNING_REF}:experiments/section53_2026-11-09/exports/{name}"], cwd=ROOT)
        if (regenerated / name).read_bytes() != frozen:
            raise ValueError(f"Regenerated planning CSV differs from the paper snapshot: {name}")
        if digest(regenerated / name) != entry["sha256"]:
            raise ValueError(f"Regenerated CSV checksum mismatch: {name}")
    tariff = verify_tariff_evidence()
    print("OK: tariff_evidence", flush=True)
    logs = PACKAGE / "logs"
    progress_arguments = []
    for option, case, algorithm in (
        ("--l2p-c4", "R28-N5-F50-C4", "lifted-two-phase"),
        ("--l2p-f10", "R28-N5-F10-C4", "lifted-two-phase"),
        ("--full-f10", "R28-N5-F10-C4", "full"),
        ("--l2p-f20", "R28-N5-F20-C4", "lifted-two-phase"),
        ("--full-f20", "R28-N5-F20-C4", "full"),
        ("--l2p-c3", "R28-N5-F50-C3", "lifted-two-phase"),
        ("--full-c3", "R28-N5-F50-C3", "full"),
    ):
        progress_arguments.extend((option, logs / f"{case}_{algorithm}.log"))
    run("progress", "manuscript/figures/chapter5_progress.py",
        *progress_arguments, "--output-dir", output)
    run("vi", "manuscript/figures/chapter5_vi_progress.py",
        "--log-dir", logs, "--output-dir", output)
    run("planning", "manuscript/figures/chapter5_planning.py",
        "--repository", ROOT, "--ref", PLANNING_REF, "--output-dir", output)
    run("station_visits", "manuscript/chapter5_analysis/plot_charger_visits_r28_n40.py",
        "--data-root", ROOT / "data/R28_N40_data", "--output-dir", report / "visits",
        "--paper-output", output / "chapter5_station_visits.png")
    run("route_workload", "manuscript/chapter5_analysis/plot_route_workload_r28_n40.py",
        "--data-root", ROOT / "data/R28_N40_data", "--output-dir", report / "workload")

    map_source = ROOT / "manuscript/figures/chapter5_network_map.png"
    map_output = output / map_source.name
    if map_source != map_output:
        shutil.copyfile(map_source, map_output)
    artifacts = []
    for name in (*PDF_FIGURES, "chapter5_station_visits.png", "chapter5_network_map.png"):
        path = output / name
        raw = path.read_bytes()
        if path.suffix == ".pdf" and (not raw.startswith(b"%PDF-") or b"%%EOF" not in raw[-1024:]):
            raise ValueError(f"Invalid generated PDF: {path}")
        if path.suffix == ".png":
            with Image.open(path) as image:
                image.verify()
        artifacts.append({"name": name, "path": str(path), "bytes": len(raw),
                          "sha256": digest(path), "paper_figure": name in PAPER_FIGURES,
                          "source_kind": "static_screenshot" if path == map_output else "computed"})
    summary = json.loads((report / "analysis/summary.json").read_text(encoding="utf-8"))
    payload = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "repository_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "runner_sha256": digest(Path(__file__)), "algorithm_source_commit": ALGORITHM_REF,
        "planning_source_commit": PLANNING_REF, "python": sys.version,
        "packages": {p: importlib.metadata.version(p) for p in ("matplotlib", "numpy", "plotly", "Pillow")},
        "algorithm_result_slots": summary["record_slots"], "unique_logs": summary["unique_logs"],
        "verified_input_files": summary["verified_repository_input_files"],
        "target_reached_counts": summary["target_reached_counts"],
        "regenerated_planning_tables": len(export_manifest["tables"]),
        "tariff_evidence": tariff, "commands": commands, "artifacts": artifacts,
    }
    completion.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Verified {len(PAPER_FIGURES)} paper figures (6 computed, 1 static screenshot).")
    print(f"Report: {completion}")


if __name__ == "__main__":
    main()
