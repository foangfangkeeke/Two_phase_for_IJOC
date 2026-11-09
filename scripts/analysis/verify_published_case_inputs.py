"""Verify current input bytes against the published 2026-11-07 case manifest.

Usage: python scripts/analysis/verify_published_case_inputs.py R28-N5-F50-C4
"""

import hashlib
import json
import sys
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    case = json.loads((root / "experiments/final_2026-11-07/cases.json").read_text(
        encoding="utf-8"))["cases"][sys.argv[1]]
    for relative, expected in case["dataset_files_sha256"].items():
        actual = hashlib.sha256((root / relative).read_bytes()).hexdigest()
        if actual != expected:
            raise ValueError(f"Input hash mismatch: {relative}")
    actual_solver = hashlib.sha256((root / "solverCfg.txt").read_bytes()).hexdigest()
    if actual_solver != case["solver_config_sha256"]:
        raise ValueError("solverCfg.txt hash mismatch")
    print(f"OK {sys.argv[1]}: {len(case['dataset_files_sha256'])} input files and solverCfg.txt")


if __name__ == "__main__":
    main()
