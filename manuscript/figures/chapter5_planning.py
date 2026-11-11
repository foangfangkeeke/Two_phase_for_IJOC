"""Draft Section 5.3 figures from the published plan exports.

The source tables are pinned to commit 1f7cf15a on the final-experiments
branch.  This script reads them through ``git show`` and launches no solver.
"""

from __future__ import annotations

import argparse
import csv
import io
import subprocess
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


RESULT_REF = "1f7cf15a4a1a48591c54367a74ec4fe85c4d9d28"
EXPORT_ROOT = "experiments/section53_2026-11-09/exports"
BLUE = "#3572a5"
RED = "#b14a3b"
BATTERY_NO_DWC = "#1f4e79"
BATTERY_DWC = "#c45a2d"
PLUG_WINDOW = "#c8d4d2"
DWC_WINDOW = "#607f7c"
STATION_LABELS = {
    255: "Bt Batok",
    275: "Clementi",
    304: "Jurong East",
    313: "Jurong Town Hall",
    476: "Tengah",
}


def git_csv(repository: Path, name: str, ref: str) -> list[dict[str, str]]:
    completed = subprocess.run(
        ["git", "show", f"{ref}:{EXPORT_ROOT}/{name}"],
        cwd=repository,
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
    )
    return list(csv.DictReader(io.StringIO(completed.stdout)))


def plot_planning_adjustments(repository: Path, output: Path, ref: str) -> None:
    route_rows = git_csv(repository, "baseline_route_changes.csv", ref)
    opportunity_rows = git_csv(repository, "baseline_routes.csv", ref)
    station_rows = git_csv(repository, "baseline_station_changes.csv", ref)
    if len(route_rows) != 140 or len(opportunity_rows) != 280 or len(station_rows) != 5:
        raise ValueError("Unexpected baseline export size")

    by_route: dict[int, dict[str, float | str]] = defaultdict(
        lambda: {"weight": 0.0, "dwc": 0.0, "nodwc": 0.0, "name": ""}
    )
    for row in route_rows:
        route = by_route[int(row["route_index"])]
        weight = float(row["scenario_probability"]) * float(row["buses"])
        route["name"] = row["route_name"]
        route["weight"] += weight
        route["dwc"] += weight * float(row["battery_dwc_kwh"])
        route["nodwc"] += weight * float(row["battery_nodwc_kwh"])

    route_indices = sorted(by_route)
    route_names = [str(by_route[index]["name"]) for index in route_indices]
    battery_dwc = np.array([
        float(by_route[index]["dwc"]) / float(by_route[index]["weight"])
        for index in route_indices
    ])
    battery_nodwc = np.array([
        float(by_route[index]["nodwc"]) / float(by_route[index]["weight"])
        for index in route_indices
    ])
    if np.any(battery_dwc > battery_nodwc + 1e-9):
        raise ValueError("A route-level DWC battery exceeds its no-DWC counterpart")

    by_route_opportunity: dict[int, dict[str, float]] = defaultdict(
        lambda: {"buses": 0.0, "plug": 0.0, "dwc": 0.0}
    )
    for row in opportunity_rows:
        if row["run_id"] != "C4_DWC":
            continue
        route = by_route_opportunity[int(row["route_index"])]
        probability = float(row["scenario_probability"])
        route["buses"] += probability * float(row["buses"])
        route["plug"] += probability * float(row["plug_available_minutes"])
        route["dwc"] += probability * float(row["dwc_available_minutes"])
    plug_opportunities = np.array([
        by_route_opportunity[index]["plug"] / by_route_opportunity[index]["buses"]
        for index in route_indices
    ])
    dwc_opportunities = np.array([
        by_route_opportunity[index]["dwc"] / by_route_opportunity[index]["buses"]
        for index in route_indices
    ])

    fig, axes = plt.subplots(
        2, 1, figsize=(7.1, 4.9), gridspec_kw={"height_ratios": [2.15, 1.0]}
    )
    ax = axes[0]
    positions = np.arange(len(route_indices))
    opportunity_axis = ax.twinx()
    opportunity_axis.bar(positions, plug_opportunities, width=0.72,
                         color=PLUG_WINDOW, alpha=0.88,
                         label="Plug-in window", zorder=1)
    opportunity_axis.bar(positions, dwc_opportunities, width=0.72,
                         bottom=plug_opportunities,
                         color=DWC_WINDOW, alpha=0.96,
                         label="DWC window", zorder=2)
    opportunity_axis.set_ylabel("Charging window (min/bus-day)",
                                color="#526d6a")
    opportunity_axis.tick_params(axis="y", colors="#526d6a")
    opportunity_axis.grid(False)
    ax.set_zorder(opportunity_axis.get_zorder() + 1)
    ax.patch.set_visible(False)
    ax.plot(positions, battery_nodwc, color=BATTERY_NO_DWC, marker="o", ms=3.0,
            lw=1.35, label="Battery without DWC", zorder=4)
    ax.plot(positions, battery_dwc, color=BATTERY_DWC, marker="s", ms=2.8,
            lw=1.35, label="Battery with DWC", zorder=4)
    ax.fill_between(positions, battery_dwc, battery_nodwc,
                    color="#7fa6bf", alpha=0.20, zorder=3)
    ax.set_xticks(positions, route_names, rotation=55, ha="right")
    ax.set_xlim(-0.4, len(route_indices) - 0.6)
    ax.set_ylim(bottom=0)
    ax.set_xlabel("Route")
    ax.set_ylabel("Mean battery capacity (kWh)")
    ax.set_title("(a) Battery capacity by route")
    ax.grid(axis="y", alpha=0.22)
    line_handles, line_labels = ax.get_legend_handles_labels()
    bar_handles, bar_labels = opportunity_axis.get_legend_handles_labels()
    ax.legend(line_handles + bar_handles, line_labels + bar_labels,
              ncol=4, loc="upper center", fontsize=6.7, frameon=False)

    ax = axes[1]
    station_ids = [int(row["station"]) for row in station_rows]
    unknown_stations = set(station_ids) - set(STATION_LABELS)
    if unknown_stations:
        raise ValueError(f"Unknown existing plug-in stations: {sorted(unknown_stations)}")
    stations = [STATION_LABELS[station] for station in station_ids]
    differences = [float(row["difference_kwh"]) for row in station_rows]
    colors = [BLUE if value >= 0 else RED for value in differences]
    bars = ax.bar(stations, differences, color=colors, width=0.68)
    ax.axhline(0, color="#555555", lw=0.8)
    value_span = max(differences) - min(differences)
    label_offset = max(20.0, 0.018 * value_span)
    lower_padding = max(60.0, 0.06 * value_span)
    upper_padding = max(180.0, 0.18 * value_span)
    ax.set_ylim(
        min(0.0, min(differences)) - lower_padding,
        max(0.0, max(differences)) + upper_padding,
    )
    for bar, value in zip(bars, differences):
        label_y = value + label_offset if value >= 0 else label_offset
        ax.text(bar.get_x() + bar.get_width() / 2, label_y,
                f"{value:+.0f}", ha="center", va="bottom", fontsize=7)
    ax.set_xlabel("Existing plug-in station")
    ax.set_ylabel("Energy change (kWh/day)")
    ax.set_title("(b) Plug-in energy by station")
    ax.grid(axis="y", alpha=0.22)

    fig.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, bbox_inches="tight")
    plt.close(fig)


def plot_cost_sensitivity(repository: Path, output: Path, ref: str) -> None:
    battery = git_csv(repository, "battery_cost_sensitivity.csv", ref)
    construction = git_csv(repository, "dwc_cost_sensitivity.csv", ref)
    if len(battery) != 5 or len(construction) != 5:
        raise ValueError("Unexpected sensitivity export size")

    fig, axes = plt.subplots(1, 2, figsize=(7.1, 2.75), sharex=True)
    panels = [
        (axes[0], battery, "(a) Battery unit cost"),
        (axes[1], construction, "(b) DWC construction cost"),
    ]
    for ax, rows, title in panels:
        factors = [100 * float(row["factor"]) for row in rows]
        differences = [float(row["incumbent_cost_difference"]) for row in rows]
        links = [int(row["selected_dwc_links"]) for row in rows]
        ax.plot(factors, differences, color=BLUE, marker="o", lw=1.4, ms=4)
        ax.axhline(0, color="#555555", lw=0.8)
        for factor, difference, count in zip(factors, differences, links):
            ax.annotate(f"{count} links", (factor, difference), xytext=(0, 6),
                        textcoords="offset points", ha="center", fontsize=6.5)
        ax.set_xticks(factors)
        ax.set_xlabel("Cost factor (% of base value)")
        ax.set_title(title)
        ax.grid(alpha=0.22)
    axes[0].set_ylabel("Cost difference\n(No DWC - DWC, USD/day)")
    fig.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, bbox_inches="tight")
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output-dir", type=Path,
                        default=Path(__file__).resolve().parent)
    parser.add_argument("--ref", default=RESULT_REF)
    args = parser.parse_args()
    repository = args.repository.resolve()
    output_dir = args.output_dir.resolve()
    plot_planning_adjustments(
        repository, output_dir / "chapter5_planning_adjustments.pdf", args.ref)
    plot_cost_sensitivity(
        repository, output_dir / "chapter5_cost_sensitivity.pdf", args.ref)


if __name__ == "__main__":
    main()
