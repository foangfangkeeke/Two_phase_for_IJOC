"""Draw Chapter 5 progress figures from the published target-machine logs.

The input logs are the matched records in
experiments/final_2026-11-07 in this repository.
Pass their local paths explicitly;
the script never substitutes local-machine timings for those records.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


BLUE = "#225ea8"
GREEN = "#238b75"
RED = "#b33b34"
PURPLE = "#756bb1"
GRAY = "#777777"
COLORS = {
    "Master": "#5d7594",
    "Route LP / RSP": "#3582b4",
    "Exact / UB checks": "#258a80",
    "Repair": "#d46a49",
    "Other": "#c6cbd0",
}
FIELD = re.compile(r"([a-zA-Z_]+)=([^,\s]+)")
BATTERY_MENU = tuple(range(50, 601, 50))
ITER_MARKER_SIZE = 2.2
DENSE_ITER_MARKER_SIZE = 1.5
TRACE_LINE_WIDTH = 1.2


def fields(line: str) -> dict[str, str]:
    return dict(FIELD.findall(line))


def iterations(lines: list[str], phase: int) -> list[dict]:
    """Only completed iterations have elapsed_ms; skip diagnostic fragments."""
    result = []
    for line in lines:
        if line.startswith(f"Phase {phase} iter ") and "elapsed_ms=" in line:
            row = fields(line)
            row["iteration"] = int(re.match(r"Phase \d iter (\d+)", line)[1])
            result.append(row)
    return result


def read_log(path: Path) -> tuple[dict, list[dict], list[dict], dict]:
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    phase1 = iterations(lines, 1)
    phase2 = iterations(lines, 2)
    switches = [fields(line) for line in lines if line.startswith("Phase 1 completed:")]
    finals = [fields(line) for line in lines if re.match(r"^(Full|Lifted Two-Phase) status=", line)]
    if not phase1 or not phase2 or len(switches) != 1 or len(finals) != 1:
        raise ValueError(f"Incomplete phase records in {path}")
    switch, final = switches[0], finals[0]
    if float(final["elapsed_ms"]) < float(phase2[-1]["elapsed_ms"]):
        raise ValueError(f"Final time precedes last completed iteration: {path}")
    return switch, phase1, phase2, final


def master_returns(path: Path, phase: int = 2) -> list[dict]:
    """Read master-solve returns, which occur before scenario evaluation."""
    result = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if not line.startswith("Search master: "):
            continue
        row = json.loads(line.split(": ", 1)[1])
        if row["phase"] == phase:
            result.append(row)
    return result


def seconds(record: dict) -> float:
    return float(record["elapsed_ms"]) / 1000.0


def reconstructed_master_times(rows: list[dict], masters: list[dict],
                               origin: float, final: dict,
                               downstream_fields: tuple[str, ...]) -> list[float]:
    """Recover master-return times from iteration ends and downstream timers."""
    if len(masters) not in {len(rows), len(rows) + 1}:
        raise ValueError(
            f"Expected one master return per evaluation plus at most one terminal "
            f"return; got {len(masters)} masters and {len(rows)} evaluations"
        )
    times = []
    for row in rows:
        downstream = sum(float(row.get(field, 0.0)) for field in downstream_fields) / 1000.0
        times.append(seconds(row) - origin - downstream)
    if len(masters) > len(rows):
        times.append(seconds(final) - origin)
    return times


def plot_phase_two_bounds(ax, switch, rows, final, method, color, marker,
                          upper_color=None, time_scale=1.0,
                          marker_size=ITER_MARKER_SIZE, *, masters):
    """Plot LB returns and UB updates at their respective recorded events."""
    origin = seconds(switch)
    final_time = seconds(final) - origin
    master_times = [time / time_scale for time in reconstructed_master_times(
        rows, masters, origin, final,
        ("rsp_ms", "subproblem_ms", "repair_ms"),
    )]

    times = [0.0] + master_times
    lbs = [float(switch["LB"])] + [float(master["bound"]) for master in masters]
    marked = list(range(1, len(times)))
    if final_time / time_scale > times[-1] + 1e-6:
        times.append(final_time / time_scale)
        lbs.append(float(final["LB"]))
    trace_zorder = 4 if method == "TPILS" else 3
    ax.step(
        times, lbs, where="post", color=color, lw=TRACE_LINE_WIDTH,
        ls="--" if method == "TPILS" else "-", label=f"{method} LB",
        marker=marker, markevery=marked,
        ms=marker_size, mfc="none", mew=0.45, zorder=trace_zorder,
    )

    ub_events = []
    best_ub = math.inf
    for row in rows:
        value = float(row["UB"])
        if math.isfinite(value) and value < best_ub - 1e-6:
            ub_events.append(((seconds(row) - origin) / time_scale, value))
            best_ub = value
    if ub_events:
        ub_times = [row[0] for row in ub_events]
        ubs = [row[1] for row in ub_events]
        marked = list(range(len(ub_events)))
        if final_time / time_scale > ub_times[-1] + 1e-6:
            ub_times.append(final_time / time_scale)
            ubs.append(float(final["UB"]))
        ax.step(
            ub_times, ubs, where="post", color=upper_color or color,
            lw=TRACE_LINE_WIDTH, label=f"{method} UB", marker=marker,
            markevery=marked, ms=marker_size, mfc="none", mew=0.45,
            zorder=trace_zorder,
        )
    else:
        ubs = []
    return times, lbs, ubs


def bound_legend(ax, **kwargs):
    ax.legend(frameon=False, **kwargs)


def timing(switch: dict, phase2: list[dict], final: dict) -> dict[str, float]:
    total = seconds(final) - seconds(switch)
    parts = {
        "Master": sum(float(row["master_ms"]) for row in phase2) / 1000.0,
        "Route LP / RSP": sum(float(row["rsp_ms"]) for row in phase2) / 1000.0,
        "Exact / UB checks": sum(float(row["subproblem_ms"]) for row in phase2) / 1000.0,
        "Repair": sum(float(row["repair_ms"]) for row in phase2) / 1000.0,
    }
    parts["Other"] = total - sum(parts.values())
    if parts["Other"] < -0.01:
        raise ValueError("Component timers exceed Phase 2 wall time")
    parts["Other"] = max(0.0, parts["Other"])
    return parts


def anchor_coverage(master: dict, repairs: list[dict], battery_menu=BATTERY_MENU) -> list[dict]:
    """Count each monotone cut's downward resource box before other MP constraints.

    The ratio compares two anchors for the same scenario, so the unrestricted
    battery choices of other scenarios cancel. It is not marginal coverage
    after existing cuts or valid inequalities, and boxes are not added.
    """
    rank = {capacity: i + 1 for i, capacity in enumerate(battery_menu)}
    result = []
    for repair in repairs:
        if repair["trigger"] == "initial-ub" or repair["cut_anchor"] is None:
            continue
        start = master[repair["iter"]]["design"]
        anchor = repair["cut_anchor"]
        scenario = repair["scenario"]
        old_f, new_f = set(start["facilities"]), set(anchor["facilities"])
        old_b, new_b = start["batteries"][scenario], anchor["batteries"][scenario]
        if not old_f <= new_f or len(old_b) != len(new_b) or any(a > b for a, b in zip(old_b, new_b)):
            raise ValueError("Repair anchor does not dominate its master resource configuration")
        old_count = 2 ** len(old_f) * math.prod(rank[b] for b in old_b)
        new_count = 2 ** len(new_f) * math.prod(rank[b] for b in new_b)
        result.append({"iteration": repair["iter"] + 1, "scenario": scenario + 1,
                       "added_facilities": len(new_f - old_f),
                       "raised_battery_choices": sum(a != b for a, b in zip(old_b, new_b)),
                       "original_box_count": str(old_count), "expanded_box_count": str(new_count),
                       "coverage_ratio": new_count / old_count})
    return result


def save(figure, output: Path, png_preview: bool = False) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output.with_suffix(".pdf"), bbox_inches="tight")
    if png_preview:
        figure.savefig(output.with_suffix(".png"), dpi=240, bbox_inches="tight")
    plt.close(figure)


def plot_two_phase(log_path: Path, output: Path) -> None:
    switch, phase1, phase2, final = read_log(log_path)
    phase1_masters = master_returns(log_path, 1)
    phase2_masters = master_returns(log_path, 2)
    if final["status"] != "OPTIMAL" or len(phase2) != 1:
        raise ValueError("Expected the selected completed one-iteration TPILS case")

    reference_ub = float(final["UB"])
    phase1_times = reconstructed_master_times(
        phase1, phase1_masters, 0.0, switch, ("subproblem_ms",),
    )
    phase1_lbs = [float(row["bound"]) for row in phase1_masters]
    retrospective = [100.0 * (reference_ub - value) / reference_ub
                     for value in phase1_lbs]
    first_one = next(i for i, value in enumerate(retrospective) if value <= 1.0)
    switch_time, final_time = seconds(switch), seconds(final)
    final_distance = 100.0 * (reference_ub - float(final["LB"])) / reference_ub

    phase2_times = [switch_time] + reconstructed_master_times(
        phase2, phase2_masters, 0.0, final,
        ("rsp_ms", "subproblem_ms", "repair_ms"),
    )
    phase2_lbs = [float(switch["LB"])] + [float(row["bound"]) for row in phase2_masters]
    if final_time > phase2_times[-1] + 1e-6:
        phase2_times.append(final_time)
        phase2_lbs.append(float(final["LB"]))

    relaxed_ubs = [(seconds(row), float(row["UB"])) for row in phase1
                   if math.isfinite(float(row["UB"]))]
    exact_ubs = [(seconds(row), float(row["UB"])) for row in phase2
                 if math.isfinite(float(row["UB"]))]

    if not relaxed_ubs or not exact_ubs:
        raise ValueError("Expected finite upper bounds at both phase endpoints")

    fig, (ax, allocation) = plt.subplots(
        2, 1, figsize=(7.0, 4.8), gridspec_kw={"height_ratios": [2.5, 1.0]}
    )

    ax.axvspan(0, switch_time, color=BLUE, alpha=0.035, lw=0)
    ax.axvspan(switch_time, final_time, color=GREEN, alpha=0.045, lw=0)
    phase1_plot_times = phase1_times + [switch_time]
    phase1_plot_lbs = phase1_lbs + [float(switch["LB"])]
    relaxed_ub_times = [point[0] for point in relaxed_ubs] + [switch_time]
    relaxed_ub_values = [point[1] for point in relaxed_ubs] + [float(switch["UB"])]
    exact_ub_times = [point[0] for point in exact_ubs]
    exact_ub_values = [point[1] for point in exact_ubs]

    ax.step(phase1_plot_times, phase1_plot_lbs, where="post", color=BLUE,
            lw=1.35, marker="o", ms=1.6, mfc="white", mec=BLUE, mew=0.35,
            label="Phase 1 relaxation LB", zorder=3)
    ax.plot(relaxed_ub_times, relaxed_ub_values, color=RED, lw=1.1,
            marker="s", ms=4.0, mfc="white", mec=RED, mew=0.8,
            label="Phase 1 relaxation UB", zorder=5)
    ax.step(phase2_times, phase2_lbs, where="post", color=PURPLE, lw=1.35,
            marker="D", ms=3.3, mfc="white", mec=PURPLE, mew=0.7,
            label="Phase 2 original LB", zorder=4)
    ax.plot(exact_ub_times, exact_ub_values, color=GREEN, lw=1.1,
            marker="D", ms=4.2, mfc=GREEN, mec=GREEN,
            label="Phase 2 verified UB", zorder=5)
    ax.axvline(switch_time, color=GRAY, ls="--", lw=1.0)
    ax.text(switch_time / 2, 0.96, "Phase 1", transform=ax.get_xaxis_transform(),
            ha="center", va="top", fontsize=8, color=BLUE)
    ax.text((switch_time + final_time) / 2, 0.96, "Phase 2",
            transform=ax.get_xaxis_transform(), ha="center", va="top",
            fontsize=8, color=GREEN)
    ax.set(xlim=(0, final_time + 6),
           ylim=(min(phase1_lbs) * 0.93, reference_ub * 1.018),
           xlabel="Elapsed wall-clock time (s)", ylabel="Objective value")
    ax.ticklabel_format(style="plain", axis="y", useOffset=False)
    ax.grid(alpha=0.22)
    ax.legend(frameon=False, fontsize=6.8, loc="lower right", ncol=2)

    zoom = ax.inset_axes([0.37, 0.29, 0.59, 0.50])
    zoom.axvspan(phase1_times[first_one] - 3, switch_time,
                 color=BLUE, alpha=0.035, lw=0)
    zoom.axvspan(switch_time, final_time, color=GREEN, alpha=0.045, lw=0)
    zoom.step(phase1_plot_times, phase1_plot_lbs, where="post", color=BLUE,
              lw=1.1, marker="o", ms=1.3, mfc="white", mec=BLUE, mew=0.3)
    zoom.plot(relaxed_ub_times, relaxed_ub_values, color=RED, lw=0.9,
              marker="s", ms=3.5, mfc="white", mec=RED, mew=0.7, zorder=4)
    zoom.step(phase2_times, phase2_lbs, where="post", color=PURPLE, lw=1.1,
              marker="D", ms=2.8, mfc="white", mec=PURPLE, mew=0.6)
    zoom.plot(exact_ub_times, exact_ub_values, color=GREEN, lw=0.9,
              marker="D", ms=3.8, mfc=GREEN, mec=GREEN, zorder=4)
    zoom.scatter([phase1_times[first_one]], [phase1_lbs[first_one]],
                 color=BLUE, s=15, zorder=5)
    zoom.axvline(switch_time, color=GRAY, ls="--", lw=0.8)
    zoom.annotate(f"Within 1% at {phase1_times[first_one]:.1f} s",
                  (phase1_times[first_one], phase1_lbs[first_one]),
                  xytext=(10, -18), textcoords="offset points", fontsize=6.5,
                  arrowprops={"arrowstyle": "-", "color": BLUE, "lw": 0.6})
    zoom.annotate(f"Phase switch\n{switch_time:.1f} s",
                  (switch_time, float(switch["LB"])), xytext=(-7, -29),
                  textcoords="offset points", ha="right", fontsize=6.5,
                  color=GRAY)
    zoom.annotate(f"Relaxation UB\n{relaxed_ubs[0][0]:.1f} s",
                  (relaxed_ubs[0][0], relaxed_ubs[0][1]), xytext=(-4, 14),
                  textcoords="offset points", ha="right", fontsize=6.2,
                  color=RED)
    zoom.annotate(f"Verified UB\n{exact_ubs[0][0]:.1f} s",
                  (exact_ubs[0][0], exact_ubs[0][1]), xytext=(-5, 7),
                  textcoords="offset points", ha="right", va="bottom",
                  fontsize=6.2, color=GREEN)
    gap_x = final_time - 8.0
    zoom.annotate("", xy=(gap_x, reference_ub),
                  xytext=(gap_x, float(final["LB"])),
                  arrowprops={"arrowstyle": "<->", "color": GREEN, "lw": 0.7})
    zoom.text(gap_x - 2.0, (reference_ub + float(final["LB"])) / 2,
              f"Final gap\n{final_distance:.3f}%", ha="right", va="center",
              fontsize=6.5, color=GREEN)
    zoom.set(xlim=(max(0, phase1_times[first_one] - 4), final_time + 4),
             ylim=(phase1_lbs[first_one] - 30, reference_ub + 30))
    zoom.tick_params(labelsize=6)
    zoom.ticklabel_format(style="plain", axis="y", useOffset=False)
    zoom.grid(alpha=0.18)
    zoom.set_title("Final-value region", fontsize=6.8, loc="left", pad=2)

    phase1_parts = {
        "Master": sum(float(row["master_ms"]) for row in phase1) / 1000.0,
        "Route LP / RSP": sum(float(row["subproblem_ms"]) for row in phase1) / 1000.0,
    }
    phase1_parts["Other"] = switch_time - sum(phase1_parts.values())
    phase2_parts = timing(switch, phase2, final)
    for y, label, parts, total in (
        (1, "Phase 1", phase1_parts, switch_time),
        (0, "Phase 2", phase2_parts, final_time - switch_time),
    ):
        left = 0.0
        for name in COLORS:
            value = parts.get(name, 0.0)
            if value > 0:
                allocation.barh(y, value, left=left, color=COLORS[name], height=0.55)
                left += value
        allocation.text(total + 2.0, y, f"{total:.1f} s", va="center", fontsize=8)
    allocation.set(yticks=[0, 1], yticklabels=["Phase 2", "Phase 1"],
                   xlim=(0, switch_time * 1.18), xlabel="Measured wall-clock time (s)")
    allocation.spines[["top", "right"]].set_visible(False)
    active = [name for name in COLORS
              if phase1_parts.get(name, 0) + phase2_parts.get(name, 0) > 0]
    handles = [plt.Rectangle((0, 0), 1, 1, color=COLORS[name]) for name in active]
    allocation.legend(handles, active, frameon=False, fontsize=7,
                      ncol=3, loc="upper center", bbox_to_anchor=(0.5, -0.5))
    fig.tight_layout(h_pad=1.1)
    save(fig, output)
    print(f"TPILS C4: first LB within 1% at {phase1_times[first_one]:.3f}s; "
          f"Phase 1 {switch_time:.3f}s; Phase 2 {final_time-switch_time:.3f}s; "
          f"final gap {final_distance:.4f}%")


def plot_two_phase_l2p(log_path: Path, output: Path) -> None:
    """Plot both phase-specific bound sequences for the selected TPILS run."""
    switch, phase1, phase2, final = read_log(log_path)
    phase1_masters = master_returns(log_path, 1)
    phase2_masters = master_returns(log_path, 2)
    if final["status"] != "OPTIMAL" or len(phase1) < 2 or len(phase2) < 2:
        raise ValueError("Expected a completed multi-iteration TPILS run")

    phase1_times = reconstructed_master_times(
        phase1, phase1_masters, 0.0, switch, ("subproblem_ms",),
    )
    switch_time = seconds(switch)
    phase2_times = reconstructed_master_times(
        phase2, phase2_masters, 0.0, final,
        ("rsp_ms", "subproblem_ms", "repair_ms"),
    )
    final_time = seconds(final)
    phase1_lb = [float(row["bound"]) for row in phase1_masters]
    phase1_ub = [(seconds(row), float(row["UB"])) for row in phase1
                 if math.isfinite(float(row["UB"]))]
    phase2_lb_times = [switch_time] + phase2_times
    phase2_lb = [float(switch["LB"])] + [float(row["bound"]) for row in phase2_masters]
    phase2_ub = [(seconds(row), float(row["UB"])) for row in phase2
                 if math.isfinite(float(row["UB"]))]
    if len(phase1_ub) < 2 or len({value for _, value in phase1_ub}) < 2:
        raise ValueError("Expected multiple finite Phase 1 upper bounds")
    if not phase2_ub:
        raise ValueError("Expected a finite Phase 2 upper bound")

    fig, (bounds, timing_ax) = plt.subplots(
        2, 1, figsize=(7.2, 4.9), gridspec_kw={"height_ratios": [2.5, 1.0]}
    )
    bounds.axvspan(0, switch_time, color=BLUE, alpha=0.035, lw=0)
    bounds.axvspan(switch_time, final_time, color=GREEN, alpha=0.045, lw=0)
    bounds.axvline(switch_time, color=GRAY, ls="--", lw=0.9)

    bounds.step(phase1_times + [switch_time], phase1_lb + [float(switch["LB"])],
                where="post", color=BLUE, lw=1.35, marker="o", ms=3.0,
                mfc="white", mec=BLUE, mew=0.65, label="Phase 1 relaxation LB")
    bounds.step([time for time, _ in phase1_ub] + [switch_time],
                [value for _, value in phase1_ub] + [float(switch["UB"])],
                where="post", color=RED, lw=1.25, marker="s", ms=3.8,
                mfc="white", mec=RED, mew=0.75, label="Phase 1 relaxation UB")
    bounds.step(phase2_lb_times, phase2_lb, where="post", color=PURPLE, lw=1.35,
                marker="D", ms=3.1, mfc="white", mec=PURPLE, mew=0.65,
                label="Phase 2 original LB")
    bounds.step([time for time, _ in phase2_ub], [value for _, value in phase2_ub],
                where="post", color=GREEN, lw=1.25, marker="D", ms=3.8,
                mfc=GREEN, mec=GREEN, label="Phase 2 verified UB")
    bounds.text(switch_time / 2, 0.96, "Phase 1",
                transform=bounds.get_xaxis_transform(), ha="center", va="top",
                fontsize=8, color=BLUE)
    bounds.text((switch_time + final_time) / 2, 0.96, "Phase 2",
                transform=bounds.get_xaxis_transform(), ha="center", va="top",
                fontsize=8, color=GREEN)
    bounds.set(xlim=(0, final_time + 6), xlabel="Elapsed algorithm time (s)",
               ylabel="Objective value")
    bounds.set_ylim(min(phase1_lb) * 0.92,
                    max(value for _, value in phase2_ub) * 1.018)
    bounds.ticklabel_format(style="plain", axis="y", useOffset=False)
    bounds.grid(alpha=0.22)
    bounds.legend(frameon=False, fontsize=6.8, loc="lower right", ncol=2)
    bounds.set_title("(a) TPILS bound trajectories on R28-S5-F10-C4",
                     loc="left", fontsize=9)

    final_gap = 100.0 * (float(final["UB"]) - float(final["LB"])) / float(final["UB"])
    bounds.annotate(f"Final gap {final_gap:.3f}%",
                    (phase2_ub[-1][0], float(final["UB"])), xytext=(-8, -24),
                    textcoords="offset points", ha="right", va="top",
                    fontsize=6.8, color=GREEN,
                    arrowprops={"arrowstyle": "-", "color": GREEN, "lw": 0.6})

    phase1_detail = bounds.inset_axes([0.13, 0.20, 0.43, 0.46])
    phase1_detail.step(phase1_times + [switch_time],
                       phase1_lb + [float(switch["LB"])], where="post",
                       color=BLUE, lw=1.0, marker="o", ms=2.5,
                       mfc="white", mec=BLUE, mew=0.5)
    phase1_detail.step([time for time, _ in phase1_ub] + [switch_time],
                       [value for _, value in phase1_ub] + [float(switch["UB"])],
                       where="post", color=RED, lw=1.0, marker="s", ms=3.2,
                       mfc="white", mec=RED, mew=0.6)
    phase1_detail.axvline(switch_time, color=GRAY, ls="--", lw=0.7)
    phase1_detail.set(xlim=(phase1_times[0] - 0.2, switch_time + 0.15),
                      ylim=(phase1_lb[1] - 300,
                            max(value for _, value in phase1_ub) + 200))
    phase1_detail.tick_params(labelsize=6)
    phase1_detail.ticklabel_format(style="plain", axis="y", useOffset=False)
    phase1_detail.grid(alpha=0.18)
    phase1_detail.set_title("Phase 1 detail", fontsize=6.8, loc="left", pad=2)

    phase1_parts = {
        "Master": sum(float(row["master_ms"]) for row in phase1) / 1000.0,
        "Route LP / RSP": sum(float(row["subproblem_ms"]) for row in phase1) / 1000.0,
    }
    phase1_parts["Other"] = switch_time - sum(phase1_parts.values())
    phase2_parts = timing(switch, phase2, final)
    for y, label, parts, total in (
        (1, "Phase 1", phase1_parts, switch_time),
        (0, "Phase 2", phase2_parts, final_time - switch_time),
    ):
        left = 0.0
        for name in COLORS:
            value = parts.get(name, 0.0)
            if value > 0:
                timing_ax.barh(y, value, left=left, color=COLORS[name], height=0.55)
                left += value
        timing_ax.text(total + 4.0, y, f"{total:.1f} s", va="center", fontsize=7.5)

    timing_ax.set(yticks=[0, 1], yticklabels=["Phase 2", "Phase 1"],
                  xlim=(0, (final_time - switch_time) * 1.15),
                  xlabel="Measured algorithm time (s)")
    timing_ax.set_title("(b) Component times on R28-S5-F10-C4",
                        loc="left", fontsize=9)
    timing_ax.spines[["top", "right"]].set_visible(False)
    active = [name for name in COLORS
              if phase1_parts.get(name, 0.0) + phase2_parts.get(name, 0.0) > 0]
    handles = [plt.Rectangle((0, 0), 1, 1, color=COLORS[name]) for name in active]
    timing_ax.legend(handles, active, frameon=False, fontsize=6.5,
                     ncol=3, loc="upper center", bbox_to_anchor=(0.5, -0.35))

    fig.tight_layout(h_pad=0.9)
    save(fig, output)
    print(f"TPILS F10: Phase 1 {len(phase1)} iterations and "
          f"{len(phase1_ub)} finite UB records; Phase 2 {len(phase2)} iterations and "
          f"{len(phase2_ub)} finite UB records; final gap {final_gap:.4f}%")


def plot_phase_one_l2p(log_path: Path, output: Path) -> None:
    """Plot Phase 1 bounds against the UB later verified in Phase 2."""
    switch, phase1, _, final = read_log(log_path)
    phase1_masters = master_returns(log_path, 1)
    if final["status"] != "OPTIMAL" or len(phase1) < 2:
        raise ValueError("Expected a completed multi-iteration TPILS run")

    times = reconstructed_master_times(
        phase1, phase1_masters, 0.0, switch, ("subproblem_ms",),
    )
    lbs = [float(row["bound"]) for row in phase1_masters]
    finite_ubs = [(seconds(row), float(row["UB"])) for row in phase1
                  if math.isfinite(float(row["UB"]))]
    if len(finite_ubs) < 2 or len({value for _, value in finite_ubs}) < 2:
        raise ValueError("Expected multiple finite Phase 1 upper bounds")

    switch_time = seconds(switch)
    switch_lb = float(switch["LB"])
    switch_ub = float(switch["UB"])
    reference_ub = float(final["UB"])
    distance = 100.0 * (reference_ub - switch_lb) / reference_ub

    fig, ax = plt.subplots(figsize=(7.0, 3.5))
    ax.step(times + [switch_time], lbs + [switch_lb], where="post",
            color=BLUE, lw=1.45, marker="o", ms=3.2, mfc="white",
            mec=BLUE, mew=0.65, label="Phase 1 relaxation LB")
    ax.step([time for time, _ in finite_ubs] + [switch_time],
            [value for _, value in finite_ubs] + [switch_ub], where="post",
            color=RED, lw=1.3, marker="s", ms=4.0, mfc="white",
            mec=RED, mew=0.75, label="Phase 1 relaxation UB")
    ax.axhline(reference_ub, color=GREEN, ls="--", lw=1.1,
               label="Final verified original-problem UB")
    ax.axvline(switch_time, color=GRAY, ls=":", lw=0.9)
    ax.annotate(f"Final verified UB = {reference_ub:,.2f}",
                (times[0] + 0.15, reference_ub), xytext=(4, 5),
                textcoords="offset points", fontsize=7, color=GREEN,
                va="bottom")
    ax.annotate(f"Phase 1 ends at {switch_time:.1f} s\n"
                f"Distance to verified UB: {distance:.3f}%",
                (switch_time, switch_lb), xytext=(-8, -28),
                textcoords="offset points", ha="right", va="top",
                fontsize=7, color=GRAY,
                arrowprops={"arrowstyle": "-", "color": GRAY, "lw": 0.6})
    ax.set(xlim=(0, switch_time + 0.35),
           ylim=(min(lbs) * 0.94, max(reference_ub, max(value for _, value in finite_ubs)) * 1.015),
           xlabel="Elapsed algorithm time (s)", ylabel="Objective value")
    ax.ticklabel_format(style="plain", axis="y", useOffset=False)
    ax.grid(alpha=0.22)
    ax.legend(frameon=False, fontsize=7.2, loc="lower right")
    ax.set_title("Phase 1 bound formation on R28-S5-F10-C4 (TPILS)",
                 loc="left", fontsize=9)
    fig.tight_layout()
    save(fig, output)
    print(f"TPILS F10 Phase 1: {len(phase1)} iterations, {len(finite_ubs)} finite UB "
          f"records, {distance:.4f}% from the final verified UB")


def plot_phase_one_distance_comparison(
        cases: list[tuple[str, Path, Path]], output: Path,
        log_scale: bool = True) -> None:
    """Compare complete Phase 1 LB trajectories across candidate-set sizes."""
    colors = {"F10": BLUE, "F20": RED, "F50": GREEN}
    records = []
    for label, l2p_path, reference_path in cases:
        switch, phase1, _, _ = read_log(l2p_path)
        phase1_masters = master_returns(l2p_path, 1)
        _, _, _, reference_final = read_log(reference_path)
        reference_ub = float(reference_final["UB"])
        if not math.isfinite(reference_ub):
            raise ValueError(f"Reference UB is not finite for {label}")
        times = reconstructed_master_times(
            phase1, phase1_masters, 0.0, switch, ("subproblem_ms",),
        )
        distances = [100.0 * (reference_ub - float(row["bound"])) / reference_ub
                     for row in phase1_masters]
        switch_time = seconds(switch)
        switch_distance = 100.0 * (reference_ub - float(switch["LB"])) / reference_ub
        threshold_times = {}
        all_points = list(zip(times + [switch_time],
                              distances + [switch_distance]))
        for threshold in (1.0,):
            threshold_times[threshold] = next(
                ((time, distance) for time, distance in all_points
                 if distance <= threshold), None)
        records.append({"label": label, "times": times, "distances": distances,
                        "switch_time": switch_time,
                        "switch_distance": switch_distance,
                        "reference_ub": reference_ub,
                        "threshold_times": threshold_times})

    fig, complete = plt.subplots(figsize=(7.2, 3.75))
    early = complete.inset_axes([0.43, 0.43, 0.53, 0.50])
    for record in records:
        label = record["label"]
        color = colors[label]
        x = record["times"] + [record["switch_time"]]
        y = record["distances"] + [record["switch_distance"]]
        for axis in (complete, early):
            axis.step(x, y, where="post", color=color, lw=1.35, label=label)
        early_points = [(time, distance) for time, distance
                        in zip(record["times"], record["distances"])
                        if time <= 15.0]
        if early_points:
            early.scatter([point[0] for point in early_points],
                          [point[1] for point in early_points],
                          marker="o", color=color, s=8, linewidth=0,
                          zorder=4)
        complete.scatter([record["switch_time"]], [record["switch_distance"]],
                         marker="s", color=color, s=17, zorder=4)
        if record["switch_time"] <= 15.0:
            early.scatter([record["switch_time"]], [record["switch_distance"]],
                          marker="s", color=color, s=17, zorder=4)

        threshold = record["threshold_times"][1.0]
        if threshold is not None:
            complete.scatter([threshold[0]], [threshold[1]], marker="o",
                             facecolor="white", edgecolor=color, s=26,
                             linewidth=0.9, zorder=5)
            if threshold[0] <= 15.0:
                early.scatter([threshold[0]], [threshold[1]], marker="o",
                              facecolor="white", edgecolor=color, s=26,
                              linewidth=0.9, zorder=5)

    annotation_offsets = {"F10": (-28, -13), "F50": (8, 7)}
    for record in records:
        threshold = record["threshold_times"][1.0]
        if threshold is None or threshold[0] > 15.0:
            continue
        early.annotate(f"{threshold[0]:.1f} s", threshold,
                       xytext=annotation_offsets[record["label"]],
                       textcoords="offset points", fontsize=6.2,
                       color=colors[record["label"]])

    for axis in (complete, early):
        axis.axhline(1.0, color="#777777", ls=":", lw=0.85)
        if log_scale:
            axis.set_yscale("log")
            axis.set_ylim(0.2, 100)
        else:
            largest_distance = max(
                max(record["distances"] + [record["switch_distance"]])
                for record in records
            )
            axis.set_ylim(0, largest_distance * 1.08)
        axis.grid(alpha=0.22, which="both")
    complete.set_xlim(0, max(record["switch_time"] for record in records) + 5)
    complete.set_xlabel("Elapsed algorithm time (s)")
    early.set_xlim(0, 15)
    early.set_xticks([0, 5, 10, 15])
    early.tick_params(axis="both", labelsize=6.2)
    complete.set_ylabel("Retrospective gap (%)")
    complete.set_title("Phase 1 lower-bound formation on R28-S5-C4",
                       loc="left", fontsize=9)
    early.set_title("First 15 seconds", loc="left", fontsize=7, pad=2)
    early.text(14.7, 1.06, "1%", ha="right", va="bottom", fontsize=6.5,
               color="#666666")
    handles, labels = complete.get_legend_handles_labels()
    complete.legend(handles, labels, frameon=False, fontsize=7.2, ncol=3,
                    loc="lower right")
    fig.tight_layout()
    save(fig, output)
    summary = "; ".join(
        f"{record['label']} ends at {record['switch_time']:.1f}s and "
        f"{record['switch_distance']:.3f}%" for record in records
    )
    print(summary)


RECORDED_COMPONENTS = (
    ("Master", "master_ms"),
    ("Route LP / RSP", "rsp_ms"),
    ("Exact / UB checks", "subproblem_ms"),
    ("Repair", "repair_ms"),
)


def plot_evaluation_components(ax, rows, positions, width) -> list[float]:
    """Draw instrumented component times for every completed evaluation."""
    bottom = [0.0] * len(rows)
    for name, field in RECORDED_COMPONENTS:
        values = [float(row[field]) / 1000.0 for row in rows]
        ax.bar(positions, values, width=width, bottom=bottom,
               color=COLORS[name], edgecolor="none", linewidth=0)
        bottom = [base + value for base, value in zip(bottom, values)]
    return bottom


def component_records(rows: list[dict], masters: list[dict]) -> list[dict]:
    """Add a master-only terminal record when the gap test stops Phase 2."""
    if len(masters) not in {len(rows), len(rows) + 1}:
        raise ValueError("Unexpected number of Phase 2 master returns")
    result = [dict(row) for row in rows]
    for master in masters[len(rows):]:
        result.append({
            "master_ms": 1000.0 * float(master["solve_seconds"]),
            "rsp_ms": 0.0,
            "subproblem_ms": 0.0,
            "repair_ms": 0.0,
        })
    return result


def first_finite_ub(rows: list[dict], switch: dict) -> tuple[int, float, float]:
    for evaluation, row in enumerate(rows, 1):
        ub = float(row["UB"])
        if math.isfinite(ub):
            return evaluation, seconds(row) - seconds(switch), ub
    raise ValueError("Expected at least one finite Phase 2 upper bound")


def completed_master_changes(path: Path) -> list[int]:
    """Count changed facility and route-scenario battery decisions."""
    _, _, rows, _ = read_log(path)
    masters = {row["iter"]: row for row in master_returns(path)}
    designs = [masters[row["iteration"]]["design"] for row in rows]
    changes = []
    for previous, current in zip(designs, designs[1:]):
        facility_changes = len(
            set(previous["facilities"]) ^ set(current["facilities"])
        )
        previous_batteries = previous["batteries"]
        current_batteries = current["batteries"]
        if [len(row) for row in previous_batteries] != [
                len(row) for row in current_batteries]:
            raise ValueError("Different battery dimensions across master solutions")
        battery_changes = sum(
            old != new
            for old_scenario, new_scenario in zip(
                previous_batteries, current_batteries
            )
            for old, new in zip(old_scenario, new_scenario)
        )
        changes.append(facility_changes + battery_changes)
    return changes


def plot_repair_modes(l2p_f10_path: Path, full_f10_path: Path,
                      l2p_c3_path: Path, full_c3_path: Path,
                      output: Path) -> None:
    """Contrast the low-overhead and capacity-stress repair regimes."""
    l10_switch, _, l10_rows, l10_final = read_log(l2p_f10_path)
    f10_switch, _, f10_rows, f10_final = read_log(full_f10_path)
    lc3_switch, _, lc3_rows, lc3_final = read_log(l2p_c3_path)
    fc3_switch, _, fc3_rows, fc3_final = read_log(full_c3_path)
    l10_masters = master_returns(l2p_f10_path)
    f10_masters = master_returns(full_f10_path)
    lc3_masters = master_returns(l2p_c3_path)
    fc3_masters = master_returns(full_c3_path)

    if l10_final["status"] != "OPTIMAL" or f10_final["status"] != "OPTIMAL":
        raise ValueError("Expected two completed F10 runs")
    if fc3_final["status"] != "OPTIMAL" or lc3_final["status"] != "TIME_LIMIT":
        raise ValueError("Expected a completed R-TPILS run and a time-limited TPILS run for C3")
    if any(math.isfinite(float(row["UB"])) for row in lc3_rows):
        raise ValueError("Expected no finite TPILS upper bound on the selected C3 run")

    fig = plt.figure(figsize=(7.35, 5.10))
    grid = fig.add_gridspec(
        2, 2, height_ratios=[1.32, 1.0], hspace=0.50, wspace=0.31,
    )
    f10_bounds = fig.add_subplot(grid[0, 0])
    c3_bounds = fig.add_subplot(grid[0, 1])
    f10_detail = fig.add_subplot(grid[1, 0])
    c3_detail = fig.add_subplot(grid[1, 1])

    l10_time, _, _ = plot_phase_two_bounds(
        f10_bounds, l10_switch, l10_rows, l10_final, "TPILS", BLUE, "D",
        marker_size=3.4, masters=l10_masters,
    )
    f10_time, _, _ = plot_phase_two_bounds(
        f10_bounds, f10_switch, f10_rows, f10_final, "R-TPILS", GREEN, "o", RED,
        marker_size=3.8, masters=f10_masters,
    )
    l10_eval, l10_ub_time, l10_ub = first_finite_ub(l10_rows, l10_switch)
    f10_eval, _, _ = first_finite_ub(f10_rows, f10_switch)
    l10_first_gap = 100.0 * (
        l10_ub - float(l10_rows[l10_eval - 1]["LB"])
    ) / l10_ub
    if l10_first_gap > 1.0 + 1e-9:
        raise ValueError("Expected the first TPILS upper bound to meet the target gap")
    f10_end = max(l10_time[-1], f10_time[-1])
    f10_bounds.set(
        xlim=(-0.02 * f10_end, 1.04 * f10_end),
        xlabel="Phase 2 wall-clock time (s)", ylabel="Objective value",
    )
    f10_bounds.set_title("(a) R28-S5-F10-C4: bound trajectories", loc="left", fontsize=8.5)
    f10_bounds.grid(alpha=0.22)
    f10_bounds.annotate(
        "First UB of TPILS",
        xy=(l10_ub_time, l10_ub), xytext=(-8, 24),
        textcoords="offset points", ha="right", va="bottom",
        fontsize=6.2, color=BLUE,
        arrowprops={"arrowstyle": "-", "color": BLUE, "lw": 0.6},
    )
    bound_legend(f10_bounds, fontsize=6.4, ncol=2, loc="upper left")

    f10_components = component_records(f10_rows, f10_masters)
    f10_x = list(range(1, len(f10_components) + 1))
    f10_totals = plot_evaluation_components(
        f10_detail, f10_components, f10_x, 0.62,
    )
    f10_detail.set(
        xlim=(0.45, len(f10_x) + 0.55), xticks=f10_x,
        xlabel="R-TPILS Phase 2 iteration", ylabel="Recorded component time (s)",
        ylim=(0, max(f10_totals) * 1.12),
    )
    if len(f10_components) > len(f10_rows):
        f10_detail.annotate(
            f"{f10_totals[-1]:.2f} s\n(master only)",
            xy=(f10_x[-1], f10_totals[-1]), xytext=(0, 5),
            textcoords="offset points", ha="center", va="bottom",
            fontsize=6.2, color=COLORS["Master"],
        )
    f10_detail.set_title("(b) R28-S5-F10-C4: R-TPILS component times", loc="left", fontsize=8.5)
    f10_detail.grid(alpha=0.18, axis="y")
    f10_detail.spines[["top", "right"]].set_visible(False)

    lc3_time, _, _ = plot_phase_two_bounds(
        c3_bounds, lc3_switch, lc3_rows, lc3_final, "TPILS", BLUE, "D",
        marker_size=DENSE_ITER_MARKER_SIZE, masters=lc3_masters,
    )
    fc3_time, fc3_lbs, fc3_ubs = plot_phase_two_bounds(
        c3_bounds, fc3_switch, fc3_rows, fc3_final, "R-TPILS", GREEN, "o", RED,
        marker_size=3.2, masters=fc3_masters,
    )
    fc3_eval, _, _ = first_finite_ub(fc3_rows, fc3_switch)
    final_gap = 100.0 * (fc3_ubs[-1] - fc3_lbs[-1]) / fc3_ubs[-1]
    c3_bounds.set(
        xlim=(-0.015 * lc3_time[-1], lc3_time[-1] * 1.015),
        xlabel="Phase 2 wall-clock time (s)",
        ylabel="Objective value",
    )
    c3_bounds.set_xticks([0, 2000, 4000, 6000])
    c3_bounds.set_title("(c) R28-S5-F50-C3: bound trajectories", loc="left", fontsize=8.5)
    c3_bounds.grid(alpha=0.22)

    l2p_changes = completed_master_changes(l2p_c3_path)
    full_changes = completed_master_changes(full_c3_path)
    l2p_median = statistics.median(l2p_changes)
    full_median = statistics.median(full_changes)
    change_ax = c3_bounds.inset_axes([0.42, 0.53, 0.55, 0.38])
    boxes = change_ax.boxplot(
        [l2p_changes, full_changes], positions=[1, 2], vert=False,
        widths=0.44, whis=(0, 100), showfliers=False, patch_artist=True,
        boxprops={"linewidth": 0.65}, whiskerprops={"linewidth": 0.6},
        capprops={"linewidth": 0.6}, medianprops={"color": "#333333", "linewidth": 0.85},
    )
    for box, color in zip(boxes["boxes"], [BLUE, GREEN]):
        box.set(facecolor=color, edgecolor=color, alpha=0.25)
    for whisker, color in zip(boxes["whiskers"], [BLUE, BLUE, GREEN, GREEN]):
        whisker.set_color(color)
    for cap, color in zip(boxes["caps"], [BLUE, BLUE, GREEN, GREEN]):
        cap.set_color(color)
    change_ax.set(
        xlim=(0, 42), xticks=[0, 20, 40], yticks=[1, 2],
        yticklabels=["TPILS", "R-TPILS"],
        xlabel="Changed decisions",
    )
    change_ax.set_title(
        "Adjacent master-solution changes",
        loc="left", fontsize=5.8, pad=1.5,
    )
    change_ax.tick_params(axis="both", labelsize=5.3, pad=1.5)
    change_ax.xaxis.label.set_size(5.5)
    change_ax.xaxis.labelpad = 1.5
    change_ax.grid(alpha=0.18, axis="x")
    change_ax.text(
        l2p_median, 1.25, f"median {l2p_median:.0f}", color=BLUE,
        fontsize=5.0, ha="center", va="bottom",
    )
    change_ax.text(
        full_median, 2.25, f"median {full_median:.0f}", color=GREEN,
        fontsize=5.0, ha="center", va="bottom",
    )

    c3_components = component_records(fc3_rows, fc3_masters)
    c3_x = list(range(1, len(c3_components) + 1))
    c3_totals = plot_evaluation_components(
        c3_detail, c3_components, c3_x, 0.72,
    )
    c3_detail.set(
        xlim=(0.4, len(c3_x) + 0.6), xticks=c3_x,
        xlabel="R-TPILS Phase 2 iteration", ylabel="Recorded component time (s)",
        ylim=(0, max(c3_totals) * 1.10),
    )
    if len(c3_components) > len(fc3_rows):
        c3_detail.annotate(
            f"{c3_totals[-1]:.2f} s\n(master only)",
            xy=(c3_x[-1], c3_totals[-1]), xytext=(-3, 5),
            textcoords="offset points", ha="right", va="bottom",
            fontsize=5.7, color=COLORS["Master"],
        )
    c3_detail.tick_params(axis="x", labelsize=6.0)
    c3_detail.set_title("(d) R28-S5-F50-C3: R-TPILS component times", loc="left", fontsize=8.5)
    c3_detail.grid(alpha=0.18, axis="y")
    c3_detail.spines[["top", "right"]].set_visible(False)

    names = [name for name, _ in RECORDED_COMPONENTS]
    handles = [plt.Rectangle((0, 0), 1, 1, color=COLORS[name]) for name in names]
    labels = ["Master", "RSP", "Exact / UB checks", "Repair"]
    fig.legend(handles, labels, frameon=False, fontsize=7.0, ncol=4,
               loc="lower center", bbox_to_anchor=(0.5, -0.005))
    fig.subplots_adjust(bottom=0.125, top=0.98)
    save(fig, output)

    f10_repair = timing(f10_switch, f10_rows, f10_final)["Repair"]
    c3_repair = timing(fc3_switch, fc3_rows, fc3_final)["Repair"]
    print(
        f"Repair modes: F10 first UB R-TPILS/TPILS evaluations {f10_eval}/{l10_eval}; "
        f"F10 R-TPILS master returns/repair rounds {len(f10_masters)}/"
        f"{sum(float(row['repair_ms']) > 0 for row in f10_rows)}; "
        f"F10 repair {f10_repair:.3f}s; C3 repair {c3_repair:.3f}s; "
        f"C3 R-TPILS final gap {final_gap:.3f}%"
    )


def plot_repair_phase_two(l2p_path: Path, full_path: Path, output: Path) -> None:
    switch, _, rows, final = read_log(full_path)
    l_switch, _, l_rows, l_final = read_log(l2p_path)
    lines = full_path.read_text(encoding="utf-8").splitlines()
    full_masters = master_returns(full_path)
    l2p_masters = master_returns(l2p_path)
    master = {row["iter"]: row for row in full_masters}
    repairs = [json.loads(line.split(": ", 1)[1]) for line in lines if line.startswith("Search repair: ")]
    coverage = anchor_coverage(master, repairs)
    if final["status"] != "OPTIMAL" or len(rows) != 18 or len(master) != 19 or len(coverage) != 45:
        raise ValueError("Expected the selected R-TPILS C3 trajectory")
    if l_final["status"] != "TIME_LIMIT" or len(l_rows) != 257 or any(math.isfinite(float(r["UB"])) for r in l_rows):
        raise ValueError("Expected the selected C3 TPILS trajectory without a finite UB")
    index = [0] + [r["iteration"] + 1 for r in rows] + [max(master) + 1]
    lbs = [float(switch["LB"])] + [float(r["LB"]) for r in rows] + [float(final["LB"])]
    ubs = [math.inf] + [float(r["UB"]) for r in rows] + [float(final["UB"])]
    first = next(i for i, ub in enumerate(ubs) if math.isfinite(ub))
    best = next(i for i, ub in enumerate(ubs) if ub == float(final["UB"]))
    first_seconds = seconds(rows[first - 1]) - seconds(switch)
    ratios = [r["coverage_ratio"] for r in coverage]
    median = statistics.median(ratios)

    fig, axes = plt.subplots(3, 1, figsize=(7.0, 7.6),
                             gridspec_kw={"height_ratios": [2.1, 1.5, 1.2]})
    ax, boxes, allocation = axes
    l_time, _, _ = plot_phase_two_bounds(ax, l_switch, l_rows, l_final, "TPILS", BLUE, "D",
                                        marker_size=DENSE_ITER_MARKER_SIZE,
                                        masters=l2p_masters)
    f_time, _, _ = plot_phase_two_bounds(ax, switch, rows, final, "R-TPILS", GREEN, "o", RED,
                                        marker_size=3.8, masters=full_masters)
    best_seconds = seconds(rows[best - 1]) - seconds(switch)
    ax.annotate(f"First R-TPILS UB ({first_seconds:.1f} s)", (first_seconds, ubs[first]),
                xytext=(1200, 18950), fontsize=8,
                arrowprops={"arrowstyle": "-", "color": RED, "lw": 0.7})
    ax.annotate(f"Best R-TPILS UB from evaluation {index[best]}", (best_seconds, ubs[best]),
                xytext=(1980, 18330), fontsize=8,
                arrowprops={"arrowstyle": "-", "color": RED, "lw": 0.7})
    gap = 100 * (ubs[-1] - lbs[-1]) / ubs[-1]
    ax.annotate(f"R-TPILS final gap {gap:.3f}%", (f_time[-1], (ubs[-1] + lbs[-1]) / 2),
                xytext=(3360, 18050), fontsize=8,
                arrowprops={"arrowstyle": "-", "color": GRAY, "lw": 0.7})
    ax.text(0.98, 0.50, "TPILS: 257 completed evaluations; no finite UB", transform=ax.transAxes,
            ha="right", fontsize=7.5, color=BLUE)
    ax.set(xlim=(0, l_time[-1] + 90), ylabel="Objective value", ylim=(17340, 19080),
           xlabel="Phase 2 wall-clock time (s)")
    ax.set_title("(a) R-TPILS and TPILS Phase 2 progress on R28-S5-F50-C3", loc="left", fontsize=9)
    bound_legend(ax, fontsize=7.5, loc="upper right")
    ax.grid(alpha=0.25)

    boxes.scatter([r["iteration"] + 0.07 * (r["scenario"] - 3) for r in coverage], ratios,
                  color=GREEN, s=18, alpha=0.7, label="Certified repair anchor", zorder=3)
    boxes.axhline(median, color=GRAY, lw=0.8, ls="--", label=f"Median {median:,.1f}")
    boxes.axhline(1, color=GRAY, lw=0.6, ls=":")
    boxes.set(yscale="log", ylim=(0.7, max(ratios) * 6), ylabel="Resource-box\ncoverage ratio")
    boxes.set(xlim=(0, index[-1] + 0.7), xticks=[0, 4, 8, 12, 16, 19],
              xlabel="R-TPILS Phase 2 master return")
    boxes.set_yticks([1, 1e3, 1e6, 1e9])
    boxes.set_title("(b) Coverage relative to a monotone cut at the originating candidate",
                    loc="left", fontsize=9)
    boxes.legend(frameon=False, fontsize=7, loc="upper right")
    boxes.grid(alpha=0.25, axis="y")

    previous = seconds(switch)
    for row in rows:
        end = seconds(row)
        values = [float(row[k]) / 1000 for k in
                  ("master_ms", "rsp_ms", "subproblem_ms", "repair_ms")]
        residual = end - previous - sum(values)
        if residual < -0.01:
            raise ValueError("Iteration component timers exceed its elapsed duration")
        left = 0.0
        for name, value in zip(COLORS, values + [max(0, residual)]):
            allocation.bar(row["iteration"] + 1, value, bottom=left,
                           color=COLORS[name], width=0.7)
            left += value
        previous = end
    terminal_master = full_masters[-1]
    allocation.bar(
        terminal_master["iter"] + 1, float(terminal_master["solve_seconds"]),
        color=COLORS["Master"], width=0.7,
    )
    allocation.annotate(
        f"{float(terminal_master['solve_seconds']):.2f} s\n(master only)",
        xy=(terminal_master["iter"] + 1, float(terminal_master["solve_seconds"])),
        xytext=(-3, 5), textcoords="offset points", ha="right", va="bottom",
        fontsize=6.2, color=COLORS["Master"],
    )
    allocation.set(xlim=(0, index[-1] + 0.7), xticks=[0, 4, 8, 12, 16, 19],
                   xlabel="R-TPILS Phase 2 master return", ylabel="Evaluation time\n(s)")
    durations = [(seconds(row) - seconds(rows[i - 1] if i else switch))
                 for i, row in enumerate(rows)]
    allocation.set_ylim(0, max(durations) * 1.12)
    allocation.set_title("(c) Component times; iteration 19 is master only", loc="left", fontsize=9)
    handles = [plt.Rectangle((0, 0), 1, 1, color=COLORS[name]) for name in COLORS]
    allocation.legend(handles, ["Master", "RSP", "Exact / UB checks", "Repair", "Other"],
                      frameon=False, fontsize=7, ncol=3, loc="upper center", bbox_to_anchor=(0.5, -0.5))
    fig.tight_layout(h_pad=1.1)
    save(fig, output)
    print(f"R-TPILS C3: {len(rows)} completed evaluations, {len(coverage)} anchors; "
          f"coverage ratio min/median/max {min(ratios):.3g}/{median:.3g}/{max(ratios):.3g}; "
          f"Phase 2 {seconds(final)-seconds(switch):.3f} s")
    print(f"TPILS C3: {len(l_rows)} completed evaluations; Phase 2 {l_time[-1]:.3f} s; "
          "no finite UB. LB markers denote master returns; UB markers denote updates.")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--l2p-c4", type=Path, required=True)
    parser.add_argument("--l2p-f10", type=Path, required=True)
    parser.add_argument("--full-f10", type=Path, required=True)
    parser.add_argument("--l2p-f20", type=Path, required=True)
    parser.add_argument("--full-f20", type=Path, required=True)
    parser.add_argument("--l2p-c3", type=Path, required=True)
    parser.add_argument("--full-c3", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    plot_phase_one_distance_comparison(
        [
            ("F10", args.l2p_f10, args.l2p_f10),
            ("F20", args.l2p_f20, args.full_f20),
            ("F50", args.l2p_c4, args.l2p_c4),
        ],
        args.output_dir / "chapter5_l2p_progress",
    )
    plot_repair_phase_two(args.l2p_c3, args.full_c3, args.output_dir / "chapter5_repair_phase2")
    plot_repair_modes(
        args.l2p_f10, args.full_f10, args.l2p_c3, args.full_c3,
        args.output_dir / "chapter5_repair_modes",
    )


if __name__ == "__main__":
    main()
