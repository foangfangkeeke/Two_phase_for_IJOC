import argparse
import csv
import math
import re
from pathlib import Path


NUMBER = r"[-+0-9.eEinfINF]+"
ITERATION_RE = re.compile(
    r"^(?P<prefix>Natural ILS|Lifted ILS|Phase 2) iter (?P<iteration>\d+),"
    rf".*?UB=(?P<ub>{NUMBER}), LB=(?P<lb>{NUMBER}),"
    r".*?elapsed_ms=(?P<elapsed>[-+0-9.eE]+)$",
    re.MULTILINE,
)
EF_RESULT_RE = re.compile(
    rf"EF status: (?P<status>\S+).*?UB: (?P<ub>{NUMBER}), "
    rf"LB: (?P<lb>{NUMBER}), gap=(?P<gap>{NUMBER})%.*?"
    r"EF solve time: (?P<elapsed>[-+0-9.eE]+) s",
    re.DOTALL,
)
PHASE_ONE_RE = re.compile(
    r"Phase 1 completed: UB=(?P<ub>[-+0-9.eE]+), "
    r"LB=(?P<lb>[-+0-9.eE]+), elapsed_ms=(?P<elapsed>[-+0-9.eE]+)"
)
FINAL_RE = re.compile(
    r"^(?:Full|Lifted Two-Phase|Natural ILS final:|Lifted ILS final:) "
    rf"status=(?P<status>\S+), UB=(?P<ub>{NUMBER}), LB=(?P<lb>{NUMBER}), "
    rf"gap=(?P<gap>{NUMBER})%?, elapsed_ms=(?P<elapsed>[-+0-9.eE]+)$",
    re.MULTILINE,
)
TERMINATION_RE = re.compile(
    r"^Algorithm termination: algorithm=(?P<algorithm>[^,]+), "
    r"status=(?P<status>[^,]+), has_solution=(?P<has_solution>[01])"
    rf"(?:, UB=(?P<ub>{NUMBER}), LB=(?P<lb>{NUMBER}), "
    rf"gap_percent=(?P<gap>{NUMBER}))?"
    r", reported_seconds=(?P<elapsed>[-+0-9.eE]+)$",
    re.MULTILINE,
)
REPAIR_PROBES_RE = re.compile(r"repair_probes=(\d+)")
REPAIR_TIME_RE = re.compile(
    r"^Phase 2 iter .*?repair_ms=([-+0-9.eE]+)", re.MULTILINE
)
STRENGTHENED_RE = re.compile(r"strengthened_cuts=(\d+)")
VI_AUDIT_RE = re.compile(
    r"^Lifted ILS VI audit: visited_points=(?P<visited>\d+), "
    r"unique_points=(?P<unique>\d+), excluded_visits=(?P<excluded>\d+), "
    r"unique_excluded_points=(?P<unique_excluded>\d+), "
    r"violated_constraints=(?P<violations>\d+), "
    r"max_deficit=(?P<max_deficit>[-+0-9.eE]+)$",
    re.MULTILINE,
)


def parse_bound(value: str) -> float:
    return math.inf if value.lower() == "inf" else float(value)


def format_number(value: float) -> str:
    if math.isinf(value):
        return "inf" if value > 0 else "-inf"
    return f"{value:.6f}"


def summarize(log_path: Path, algorithm: str) -> dict[str, str]:
    text = log_path.read_text(encoding="utf-8-sig")
    matches = list(ITERATION_RE.finditer(text))
    final_matches = list(FINAL_RE.finditer(text))
    terminations = list(TERMINATION_RE.finditer(text))
    ef_match = EF_RESULT_RE.search(text)
    result = {
        "status": "UNKNOWN_EXIT_UNRECORDED",
        "iterations": str(int(matches[-1].group("iteration")) + 1) if matches else "0",
        "ub": "",
        "lb": "",
        "gap_percent": "",
        "solver_elapsed_seconds": "",
        "repair_probes": str(sum(map(int, REPAIR_PROBES_RE.findall(text)))),
        "repair_seconds": f"{sum(map(float, REPAIR_TIME_RE.findall(text))) / 1000.0:.6f}",
        "strengthened_cuts": str(sum(map(int, STRENGTHENED_RE.findall(text)))),
        "bound_source": "UNRECORDED",
        "final_status_recorded": "0",
    }

    selected = None
    if final_matches:
        selected = final_matches[-1]
        result.update(status=selected.group("status"), bound_source="FINAL",
                      final_status_recorded="1")
    elif ef_match is not None:
        selected = ef_match
        result.update(status=selected.group("status"), bound_source="FINAL",
                      final_status_recorded="1")
    elif matches:
        selected = matches[-1]
        result["bound_source"] = "LAST_COMPLETE_ITERATION"
        if " converged." in text:
            result["status"] = "OPTIMAL"
    if selected is not None:
        ub, lb = float(selected.group("ub")), float(selected.group("lb"))
        result.update(ub=format_number(ub), lb=format_number(lb))
        if math.isfinite(ub) and math.isfinite(lb):
            result["gap_percent"] = f"{(ub - lb) / max(1.0, abs(ub)) * 100.0:.6f}"
        divisor = 1.0 if selected is ef_match else 1000.0
        result["solver_elapsed_seconds"] = f"{float(selected.group('elapsed')) / divisor:.6f}"
    else:
        phase_one = PHASE_ONE_RE.search(text)
        if phase_one is not None:
            result.update(ub="inf", lb=phase_one.group("lb"),
                          bound_source="PHASE_ONE_LOWER_BOUND",
                          solver_elapsed_seconds=f"{float(phase_one.group('elapsed')) / 1000.0:.6f}")

    if terminations:
        final = terminations[-1]
        if final.group("algorithm") != algorithm:
            raise ValueError("Terminal algorithm differs from the requested algorithm")
        result.update(status=final.group("status"), final_status_recorded="1")
        if final.group("ub") is not None:
            result.update(ub=format_number(float(final.group("ub"))),
                          lb=format_number(float(final.group("lb"))),
                          bound_source="FINAL")
            result["gap_percent"] = (
                final.group("gap") if math.isfinite(float(final.group("gap"))) else ""
            )
        seconds = float(final.group("elapsed"))
        if seconds > 0.0 or final.group("ub") is not None:
            result["solver_elapsed_seconds"] = f"{seconds:.6f}"
    return result


def summarize_vi_audit(log_path: Path) -> dict[str, str]:
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    matches = list(VI_AUDIT_RE.finditer(text))
    if not matches:
        return {
            "vi_visited_points": "",
            "vi_unique_points": "",
            "vi_excluded_visits": "",
            "vi_unique_excluded_points": "",
            "vi_violated_constraints": "",
            "vi_max_deficit": "",
        }
    audit = matches[-1]
    return {
        "vi_visited_points": audit.group("visited"),
        "vi_unique_points": audit.group("unique"),
        "vi_excluded_visits": audit.group("excluded"),
        "vi_unique_excluded_points": audit.group("unique_excluded"),
        "vi_violated_constraints": audit.group("violations"),
        "vi_max_deficit": audit.group("max_deficit"),
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
        "bound_source",
        "final_status_recorded",
        "vi_visited_points",
        "vi_unique_points",
        "vi_excluded_visits",
        "vi_unique_excluded_points",
        "vi_violated_constraints",
        "vi_max_deficit",
    ]
    with args.output.open("w", encoding="utf-8", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=fieldnames)
        writer.writeheader()
        for row in manifest:
            log_path = Path(row["solver_log"])
            row.update(summarize(log_path, row["algorithm"]))
            row.update(summarize_vi_audit(log_path))
            writer.writerow(row)

    print(args.output)


if __name__ == "__main__":
    main()

