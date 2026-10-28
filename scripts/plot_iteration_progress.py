import argparse
import math
import re
from dataclasses import dataclass
from pathlib import Path

import matplotlib.pyplot as plt


PHASE1_RE = re.compile(
    r"Phase 1 completed: UB=(?P<ub>[-+0-9.eE]+), "
    r"LB=(?P<lb>[-+0-9.eE]+), elapsed_ms=(?P<elapsed>[-+0-9.eE]+)"
)
PHASE2_RE = re.compile(
    r"Phase 2 iter (?P<iteration>\d+), .*?UB=(?P<ub>[-+0-9.eEinfINF]+), "
    r"LB=(?P<lb>[-+0-9.eE]+), .*?elapsed_ms=(?P<elapsed>[-+0-9.eE]+)"
)
FINAL_RE = re.compile(
    r"^(?:Full|Lifted Two-Phase) status=\S+, "
    r"UB=(?P<ub>[-+0-9.eEinfINF]+), LB=(?P<lb>[-+0-9.eE]+), "
    r"gap=(?:[-+0-9.eE]+%|1), elapsed_ms=(?P<elapsed>[-+0-9.eE]+)$",
    re.MULTILINE,
)


@dataclass
class Progress:
    time: list[float]
    lower_bound: list[float]
    upper_bound: list[float]


def parse_upper_bound(value: str) -> float:
    return math.inf if value.lower() == "inf" else float(value)


def parse_log(path: Path) -> Progress:
    text = path.read_text(encoding="utf-8", errors="ignore")
    phase1_match = PHASE1_RE.search(text)
    if phase1_match is None:
        raise ValueError(f"No Phase 1 completion row found in {path}")

    phase1_time = float(phase1_match.group("elapsed")) / 1000.0
    time = [0.0]
    lower_bound = [float(phase1_match.group("lb"))]
    upper_bound = [math.inf]
    for match in PHASE2_RE.finditer(text):
        elapsed = float(match.group("elapsed")) / 1000.0 - phase1_time
        time.append(max(0.0, elapsed))
        lower_bound.append(float(match.group("lb")))
        upper_bound.append(parse_upper_bound(match.group("ub")))

    final_matches = list(FINAL_RE.finditer(text))
    if final_matches:
        final = final_matches[-1]
        elapsed = max(
            0.0, float(final.group("elapsed")) / 1000.0 - phase1_time
        )
        final_lb = float(final.group("lb"))
        final_ub = parse_upper_bound(final.group("ub"))
        if len(time) > 1 and math.isclose(elapsed, time[-1]):
            lower_bound[-1] = final_lb
            upper_bound[-1] = final_ub
        else:
            time.append(elapsed)
            lower_bound.append(final_lb)
            upper_bound.append(final_ub)

    if len(time) == 1:
        raise ValueError(f"No Phase 2 iteration rows found in {path}")
    return Progress(time, lower_bound, upper_bound)


def finite_series(time: list[float], values: list[float]):
    points = [(x, y) for x, y in zip(time, values) if math.isfinite(y)]
    return [point[0] for point in points], [point[1] for point in points]


def plot_comparison(
    baseline_path: Path,
    comparison_path: Path,
    output_path: Path,
    title: str,
    baseline_label: str,
    comparison_label: str,
) -> None:
    baseline = parse_log(baseline_path)
    comparison = parse_log(comparison_path)

    figure, (bounds_axis, lb_axis) = plt.subplots(
        2,
        1,
        figsize=(9.0, 6.8),
        dpi=180,
        sharex=True,
        gridspec_kw={"height_ratios": [1.35, 1.0]},
    )

    bounds_axis.step(
        baseline.time,
        baseline.lower_bound,
        where="post",
        color="#2563eb",
        linewidth=2.0,
        label=f"{baseline_label} LB",
    )
    bounds_axis.step(
        comparison.time,
        comparison.lower_bound,
        where="post",
        color="#0f766e",
        linewidth=2.0,
        label=f"{comparison_label} LB",
    )
    comparison_ub_time, comparison_ub = finite_series(
        comparison.time, comparison.upper_bound
    )
    bounds_axis.step(
        comparison_ub_time,
        comparison_ub,
        where="post",
        color="#dc2626",
        linewidth=2.2,
        label=f"{comparison_label} UB",
    )
    baseline_ub_time, baseline_ub = finite_series(
        baseline.time, baseline.upper_bound
    )
    if baseline_ub:
        bounds_axis.step(
            baseline_ub_time,
            baseline_ub,
            where="post",
            color="#d97706",
            linewidth=2.0,
            label=f"{baseline_label} UB",
        )
    else:
        bounds_axis.text(
            0.985,
            0.94,
            f"{baseline_label}: no feasible UB",
            transform=bounds_axis.transAxes,
            ha="right",
            va="top",
            color="#92400e",
            fontsize=9,
        )

    for progress, color, label in (
        (baseline, "#2563eb", f"{baseline_label} LB"),
        (comparison, "#0f766e", f"{comparison_label} LB"),
    ):
        lb_axis.step(
            progress.time,
            progress.lower_bound,
            where="post",
            color=color,
            linewidth=2.0,
            label=label,
        )

    bounds_axis.set_title(title)
    bounds_axis.set_ylabel("Objective value")
    lb_axis.set_ylabel("Lower bound (zoomed)")
    lb_axis.set_xlabel("Phase 2 wall-clock time (s)")
    for axis in (bounds_axis, lb_axis):
        axis.grid(color="#d4d4d4", linewidth=0.6, alpha=0.7)
        axis.legend(loc="best", frameon=False)
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)

    maximum_time = max(baseline.time[-1], comparison.time[-1])
    bounds_axis.set_xlim(0.0, maximum_time)
    figure.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, bbox_inches="tight")
    plt.close(figure)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compare Phase 2 UB/LB progress from two wireless logs."
    )
    parser.add_argument("baseline", type=Path)
    parser.add_argument("comparison", type=Path)
    parser.add_argument("--output", "-o", type=Path, required=True)
    parser.add_argument(
        "--title", default="Phase 2 bound progress"
    )
    parser.add_argument("--baseline-label", default="Baseline")
    parser.add_argument("--comparison-label", default="Comparison")
    arguments = parser.parse_args()
    plot_comparison(
        arguments.baseline,
        arguments.comparison,
        arguments.output,
        arguments.title,
        arguments.baseline_label,
        arguments.comparison_label,
    )
    print(arguments.output)


if __name__ == "__main__":
    main()
