"""Create one R28_N5 Section 5.3 cost variant without editing the base data.

Usage: python scripts/analysis/make_cost_case.py dwc 0.75
       python scripts/analysis/make_cost_case.py battery 1.25
"""

import hashlib
import json
import shutil
import sys
from decimal import Decimal
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def change_row(lines: list[str], label: str, indices: tuple[int, ...],
               factor: Decimal) -> None:
    headings = [index for index, line in enumerate(lines) if line.startswith(label)]
    if len(headings) != 1:
        raise ValueError(f"Expected one heading: {label}")
    index = headings[0] + 1
    if index >= len(lines) or lines[index].startswith("#") or not lines[index]:
        raise ValueError(f"Missing data row after {label}")
    fields = lines[index].split(",")
    for column in indices:
        fields[column] = format(Decimal(fields[column]) * factor, "f")
    lines[index] = ",".join(fields)


def main() -> None:
    kind = sys.argv[1]
    factor = Decimal(sys.argv[2])
    if kind not in ("dwc", "battery") or factor <= 0 or factor == 1:
        raise ValueError("Use dwc/battery and a positive factor other than 1")
    source = ROOT / "data/R28_N5_data"
    case = json.loads((ROOT / "experiments/final_2026-11-07/cases.json").read_text(
        encoding="utf-8"))["cases"]["R28-N5-F50-C4"]
    for relative, expected in case["dataset_files_sha256"].items():
        if digest(ROOT / relative) != expected:
            raise ValueError(f"Base input hash mismatch: {relative}")
    token = format(factor, "f").replace(".", "p")
    data_name = f"R28_N5_{kind}{token}"
    destination = ROOT / "data" / f"{data_name}_data"
    shutil.copytree(source, destination)
    global_cfg = destination / "globalCfg.txt"
    before = digest(global_cfg)
    raw = global_cfg.read_bytes()
    if raw.count(b"\r\n") != raw.count(b"\n") or not raw.endswith(b"\r\n"):
        raise ValueError("Expected canonical CRLF globalCfg.txt")
    lines = raw.decode("utf-8").splitlines()
    if kind == "dwc":
        change_row(lines, "# priceOfUnitCharger", (2, 3), factor)
    else:
        change_row(lines, "# priceOfPowerDay", (2,), factor)
    global_cfg.write_bytes(("\r\n".join(lines) + "\r\n").encode("utf-8"))
    files = sorted(path for path in source.rglob("*") if path.is_file())
    changed = [str(path.relative_to(source)) for path in files
               if digest(path) != digest(destination / path.relative_to(source))]
    if changed != ["globalCfg.txt"]:
        raise ValueError(f"Unexpected input changes: {changed}")
    manifest = {
        "data_name": data_name, "kind": kind, "factor": str(factor),
        "base_dataset": "R28_N5", "changed_files": changed,
        "base_global_sha256": before, "variant_global_sha256": digest(global_cfg),
        "other_file_count_unchanged": len(files) - 1,
    }
    (destination / "cost_case_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
