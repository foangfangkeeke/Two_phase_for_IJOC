import argparse
import csv
import math
import re
from pathlib import Path


ITERATION_RE = re.compile(
    r"^(?P<prefix>Natural ILS|Lifted ILS|Phase 2) iter (?P<iteration>\d+),"
    r".*?UB=(?P<ub>[-+0-9.eEinfINF]+), LB=(?P<lb>[-+0-9.eE]+), gap=(?P<gap>[-+0-9.eE]+%|1),"
    r".*?elapsed_ms=(?P<elapsed>[-+0-9.eE]+)$",
    re.MULTILINE,
)
EF_RESULT_RE = re.compile(
    r"EF status: (?P<status>\S+).*?UB: (?P<ub>[-+0-9.eE]+), "
    r"LB: (?P<lb>[-+0-9.eE]+), gap=(?P<gap>[-+0-9.eE]+)%.*?"
    r"EF solve time: (?P<elapsed>[-+0-9.eE]+) s",
    re.DOTALL,
)
PHASE_ONE_RE = re.compile(
    r"Phase 1 completed: UB=(?P<ub>[-+0-9.eE]+), "
    r"LB=(?P<lb>[-+0-9.eE]+), elapsed_ms=(?P<elapsed>[-+0-9.eE]+)"
)
FINAL_RE = re.compile(
    r"^(?:Full|Lifted Two-Phase) status=(?P<status>\S+), "
    r"UB=(?P<ub>[-+0-9.eEinfINF]+), LB=(?P<lb>[-+0-9.eE]+), "
    r"gap=(?P<gap>[-+0-9.eE]+%|1), elapsed_ms=(?P<elapsed>[-+0-9.eE]+)$",
    re.MULTILINE,
)
REPAIR_PROBES_RE = re.compile(r"repair_probes=(\d+)")
REPAIR_TIME_RE = re.compile(
    r"^Phase 2 iter .*?repair_ms=([-+0-9.eE]+)", re.MULTILINE
)
STRENGTHENED_RE = re.compile(r"strengthened_cuts=(\d+)")


def parse_bound(value: str) -> float:
    return math.inf if value.lower() == "inf" else float(value)


def format_number(value: float) -> str:
    return "inf" if not math.isfinite(value) else f"{value:.6f}"


def summarize(log_path: Path, algorithm: str) -> dict[str, str]:
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    converged = (
        " converged." in text
        or "EF status: Optimal" in text
    )

    ef_match = EF_RESULT_RE.search(text)
    if ef_match is not None:
        return {
            "status": ef_match.group("status"),
            "iterations": "",
            "ub": ef_match.group("ub"),
            "lb": ef_match.group("lb"),
            "gap_percent": ef_match.group("gap"),
            "solver_elapsed_seconds": ef_match.group("elapsed"),
            "repair_probes": "0",
            "repair_seconds": "0.000000",
            "strengthened_cuts": "0",
        }

    matches = list(ITERATION_RE.finditer(text))
    final_matches = list(FINAL_RE.finditer(text))
    if final_matches:
        final = final_matches[-1]
        ub = parse_bound(final.group("ub"))
        gap = "" if not math.isfinite(ub) else final.group("gap").removesuffix("%")
        return {
            "status": final.group("status"),
            "iterations": str(int(matches[-1].group("iteration")) + 1) if matches else "0",
            "ub": format_number(ub),
            "lb": f"{float(final.group('lb')):.6f}",
            "gap_percent": gap,
            "solver_elapsed_seconds": f"{float(final.group('elapsed')) / 1000.0:.6f}",
            "repair_probes": str(sum(map(int, REPAIR_PROBES_RE.findall(text)))),
            "repair_seconds": f"{sum(map(float, REPAIR_TIME_RE.findall(text))) / 1000.0:.6f}",
            "strengthened_cuts": str(sum(map(int, STRENGTHENED_RE.findall(text)))),
        }

    if matches:
        last = matches[-1]
        ub = parse_bound(last.group("ub"))
        gap_text = last.group("gap")
        gap = "" if not math.isfinite(ub) else gap_text.removesuffix("%")
        elapsed = float(last.group("elapsed")) / 1000.0
        return {
            "status": "Optimal" if converged else "TimeLimitOrInterrupted",
            "iterations": str(int(last.group("iteration")) + 1),
            "ub": format_number(ub),
            "lb": f"{float(last.group('lb')):.6f}",
            "gap_percent": gap,
            "solver_elapsed_seconds": f"{elapsed:.6f}",
            "repair_probes": str(sum(map(int, REPAIR_PROBES_RE.findall(text)))),
            "repair_seconds": f"{sum(map(float, REPAIR_TIME_RE.findall(text))) / 1000.0:.6f}",
            "strengthened_cuts": str(sum(map(int, STRENGTHENED_RE.findall(text)))),
        }

    phase_one = PHASE_ONE_RE.search(text)
    if phase_one is not None:
        return {
            "status": "Phase1Only",
            "iterations": "0",
            "ub": "inf",
            "lb": phase_one.group("lb"),
            "gap_percent": "",
            "solver_elapsed_seconds": f"{float(phase_one.group('elapsed')) / 1000.0:.6f}",
            "repair_probes": "0",
            "repair_seconds": "0.000000",
            "strengthened_cuts": "0",
        }

    return {
        "status": "NoProgressRecord",
        "iterations": "",
        "ub": "",
        "lb": "",
        "gap_percent": "",
        "solver_elapsed_seconds": "",
        "repair_probes": "0",
        "repair_seconds": "0.000000",
        "strengthened_cuts": "0",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description="Summarize a completed experiment group.")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    with args.manifest.open(encoding="utf-8-sig", newline="") as source:
        manifest = list(csv.DictReader(source, delimiter="\t"))

    fieldnames = list(manifest[0]) + [
        "status",
        "iterations",
        "ub",
        "lb",
        "gap_percent",
        "solver_elapsed_seconds",
        "repair_probes",
        "repair_seconds",
        "strengthened_cuts",
    ]
    with args.output.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=fieldnames)
        writer.writeheader()
        for row in manifest:
            row.update(summarize(Path(row["solver_log"]), row["algorithm"]))
            writer.writerow(row)

    print(args.output)


if __name__ == "__main__":
    main()

