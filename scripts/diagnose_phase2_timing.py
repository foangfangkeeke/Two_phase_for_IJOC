import argparse
import math
import re
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path


PHASE_ONE_RE = re.compile(
    r"^Phase 1 completed:.*?elapsed_ms=(?P<elapsed>[-+0-9.eE]+)$",
    re.MULTILINE,
)
PHASE_TWO_PROGRESS_RE = re.compile(
    r"^Phase 2 iter (?P<iteration>\d+),.*?\bLB=[-+0-9.eE]+.*?"
    r"\belapsed_ms=[-+0-9.eE]+$",
    re.MULTILINE,
)
FINAL_RE = re.compile(
    r"^(?P<algorithm>Full|Lifted Two-Phase) status=(?P<status>\S+), "
    r"UB=(?P<ub>[-+0-9.eEinfINF]+), LB=(?P<lb>[-+0-9.eE]+), "
    r"gap=(?P<gap>[-+0-9.eE]+%|1), elapsed_ms=(?P<elapsed>[-+0-9.eE]+)$",
    re.MULTILINE,
)
REPAIR_EVENT_RE = re.compile(r"^Repair iter=.*$", re.MULTILINE)
RUNTIME_RE = re.compile(r"^Runtime options:.*$", re.MULTILINE)
INSTANCE_RE = re.compile(r"^Instance:.*$", re.MULTILINE)


@dataclass(frozen=True)
class IterationTiming:
    iteration: int
    ub: float
    lb: float
    gap: str
    elapsed_ms: float
    master_ms: float
    rsp_ms: float
    exact_ms: float
    repair_ms: float
    repair_probes: int
    strengthened_cuts: int


@dataclass(frozen=True)
class RepairEvent:
    iteration: int
    scenario: int
    trigger: str
    probes: int
    battery_probes: int
    facility_probes: int
    repair_ms: float
    accepted_resources: int
    certificate_checks: int
    certified_facilities: int


def required_match(pattern: re.Pattern[str], text: str, description: str) -> re.Match[str]:
    match = pattern.search(text)
    if match is None:
        raise ValueError(f"Missing {description}")
    return match


def required_field(line: str, name: str) -> str:
    match = re.search(rf"(?:^|, ){re.escape(name)}=([^,]+)", line)
    if match is None:
        raise ValueError(f"Missing {name} in log line: {line}")
    return match.group(1)


def optional_int_field(line: str, name: str) -> int:
    match = re.search(rf"(?:^|, ){re.escape(name)}=(\d+)", line)
    return 0 if match is None else int(match.group(1))


def parse_bound(value: str) -> float:
    return math.inf if value.lower() == "inf" else float(value)


def parse_iterations(text: str) -> list[IterationTiming]:
    iterations = []
    for match in PHASE_TWO_PROGRESS_RE.finditer(text):
        line = match.group(0)
        iterations.append(
            IterationTiming(
                iteration=int(match.group("iteration")),
                ub=parse_bound(required_field(line, "UB")),
                lb=float(required_field(line, "LB")),
                gap=required_field(line, "gap"),
                elapsed_ms=float(required_field(line, "elapsed_ms")),
                master_ms=float(required_field(line, "master_ms")),
                rsp_ms=float(required_field(line, "rsp_ms")),
                exact_ms=float(required_field(line, "subproblem_ms")),
                repair_ms=float(required_field(line, "repair_ms")),
                repair_probes=int(required_field(line, "repair_probes")),
                strengthened_cuts=int(required_field(line, "strengthened_cuts")),
            )
        )
    if not iterations:
        raise ValueError("Missing Phase 2 progress records")
    return iterations


def parse_repair_events(text: str) -> list[RepairEvent]:
    events = []
    for match in REPAIR_EVENT_RE.finditer(text):
        line = match.group(0)
        events.append(
            RepairEvent(
                iteration=int(required_field(line, "Repair iter")),
                scenario=int(required_field(line, "scenario")),
                trigger=required_field(line, "trigger"),
                probes=int(required_field(line, "probes")),
                battery_probes=int(required_field(line, "battery_probes")),
                facility_probes=int(required_field(line, "facility_probes")),
                repair_ms=float(required_field(line, "repair_ms")),
                accepted_resources=int(required_field(line, "accepted_resources")),
                certificate_checks=optional_int_field(line, "rsp_cert_checks"),
                certified_facilities=optional_int_field(
                    line, "rsp_certified_facilities"
                ),
            )
        )
    return events


def format_bound(value: float) -> str:
    return "inf" if not math.isfinite(value) else f"{value:.6f}"


def print_table(headers: list[str], rows: list[list[str]]) -> None:
    widths = [len(header) for header in headers]
    for row in rows:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(value))
    print("  ".join(header.ljust(widths[index]) for index, header in enumerate(headers)))
    print("  ".join("-" * width for width in widths))
    for row in rows:
        print("  ".join(value.ljust(widths[index]) for index, value in enumerate(row)))


def summarize_log(log_path: Path, tail_iterations: int) -> None:
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    phase_one = required_match(PHASE_ONE_RE, text, "Phase 1 completion record")
    runtime = required_match(RUNTIME_RE, text, "runtime options")
    instance = required_match(INSTANCE_RE, text, "instance description")
    iterations = parse_iterations(text)
    events = parse_repair_events(text)
    final_matches = list(FINAL_RE.finditer(text))

    phase_one_ms = float(phase_one.group("elapsed"))
    if final_matches:
        final = final_matches[-1]
        status = final.group("status")
        end_elapsed_ms = float(final.group("elapsed"))
        end_ub = parse_bound(final.group("ub"))
        end_lb = float(final.group("lb"))
        end_gap = final.group("gap")
    else:
        last = iterations[-1]
        status = "PROGRESS_ONLY"
        end_elapsed_ms = last.elapsed_ms
        end_ub = last.ub
        end_lb = last.lb
        end_gap = last.gap

    phase_two_ms = end_elapsed_ms - phase_one_ms
    if phase_two_ms <= 0.0:
        raise ValueError("Phase 2 elapsed time is not positive")

    master_ms = sum(item.master_ms for item in iterations)
    rsp_ms = sum(item.rsp_ms for item in iterations)
    exact_ms = sum(item.exact_ms for item in iterations)
    repair_ms = sum(item.repair_ms for item in iterations)
    accounted_ms = master_ms + rsp_ms + exact_ms + repair_ms
    unaccounted_ms = phase_two_ms - accounted_ms

    print(f"\n=== {log_path} ===")
    print(runtime.group(0))
    print(instance.group(0))
    print(
        f"Endpoint: status={status}, iterations={len(iterations)}, "
        f"UB={format_bound(end_ub)}, LB={end_lb:.6f}, gap={end_gap}, "
        f"phase2={phase_two_ms / 1000.0:.3f}s"
    )

    timing_rows = []
    for name, milliseconds in (
        ("Master", master_ms),
        ("RSP", rsp_ms),
        ("Exact/repair-UB", exact_ms),
        ("Repair probes", repair_ms),
        ("Unaccounted/tail", unaccounted_ms),
    ):
        timing_rows.append(
            [
                name,
                f"{milliseconds / 1000.0:.3f}",
                f"{100.0 * milliseconds / phase_two_ms:.1f}%",
            ]
        )
    print("\nPhase 2 wall-clock accounting")
    print_table(["Component", "Seconds", "Share"], timing_rows)

    if events:
        events_by_iteration: dict[int, list[RepairEvent]] = defaultdict(list)
        for event in events:
            events_by_iteration[event.iteration].append(event)
        scenario_sum_ms = sum(event.repair_ms for event in events)
        ideal_parallel_ms = sum(
            max(event.repair_ms for event in batch)
            for batch in events_by_iteration.values()
        )
        batch_sizes = Counter(len(batch) for batch in events_by_iteration.values())
        total_probes = sum(event.probes for event in events)
        battery_probes = sum(event.battery_probes for event in events)
        facility_probes = sum(event.facility_probes for event in events)
        certificate_events = sum(event.certificate_checks > 0 for event in events)
        certificate_prefix_events = sum(
            event.certified_facilities > 0 for event in events
        )
        print("\nRepair summary")
        print(
            f"  batches={len(events_by_iteration)}, events={len(events)}, "
            f"batch_sizes={','.join(f'{size}x:{count}' for size, count in sorted(batch_sizes.items()))}"
        )
        print(
            f"  probes={total_probes}, battery={battery_probes}, "
            f"facility={facility_probes}, strengthened="
            f"{sum(item.strengthened_cuts for item in iterations)}"
        )
        print(
            f"  scenario_time_sum={scenario_sum_ms / 1000.0:.3f}s, "
            f"batch_wall={repair_ms / 1000.0:.3f}s, "
            f"effective_parallelism={scenario_sum_ms / repair_ms:.2f}"
        )
        print(
            f"  same-speed all-scenario lower bound={ideal_parallel_ms / 1000.0:.3f}s, "
            f"maximum scheduling-only saving="
            f"{max(0.0, repair_ms - ideal_parallel_ms) / 1000.0:.3f}s "
            f"({100.0 * max(0.0, repair_ms - ideal_parallel_ms) / repair_ms:.1f}% of repair)"
        )
        print(
            f"  RSP-certificate checked events={certificate_events}/{len(events)}, "
            f"nonzero certified-prefix events={certificate_prefix_events}/{len(events)}"
        )

        scenario_rows = []
        for scenario in sorted({event.scenario for event in events}):
            selected = [event for event in events if event.scenario == scenario]
            selected_probes = sum(event.probes for event in selected)
            selected_ms = sum(event.repair_ms for event in selected)
            scenario_rows.append(
                [
                    str(scenario),
                    str(len(selected)),
                    str(selected_probes),
                    str(sum(event.battery_probes for event in selected)),
                    str(sum(event.facility_probes for event in selected)),
                    f"{selected_ms / 1000.0:.3f}",
                    f"{selected_ms / 1000.0 / selected_probes:.3f}",
                ]
            )
        print("\nRepair by scenario (scenario time overlaps across workers)")
        print_table(
            ["Scenario", "Events", "Probes", "Battery", "Facility", "Seconds", "s/probe"],
            scenario_rows,
        )

        trigger_rows = []
        for trigger in sorted({event.trigger for event in events}):
            selected = [event for event in events if event.trigger == trigger]
            trigger_rows.append(
                [
                    trigger,
                    str(len(selected)),
                    str(sum(event.probes for event in selected)),
                    f"{sum(event.repair_ms for event in selected) / 1000.0:.3f}",
                ]
            )
        print("\nRepair by trigger")
        print_table(["Trigger", "Events", "Probes", "Scenario seconds"], trigger_rows)

        shape_rows = []
        shapes = sorted(
            {(event.battery_probes, event.facility_probes) for event in events},
            key=lambda shape: (
                -sum(
                    event.battery_probes == shape[0]
                    and event.facility_probes == shape[1]
                    for event in events
                ),
                shape,
            ),
        )
        for battery_probes, facility_probes in shapes:
            selected = [
                event
                for event in events
                if event.battery_probes == battery_probes
                and event.facility_probes == facility_probes
            ]
            selected_ms = sum(event.repair_ms for event in selected)
            selected_probes = sum(event.probes for event in selected)
            shape_rows.append(
                [
                    str(battery_probes),
                    str(facility_probes),
                    str(len(selected)),
                    f"{selected_ms / 1000.0 / len(selected):.3f}",
                    f"{selected_ms / 1000.0 / selected_probes:.3f}",
                ]
            )
        print("\nRepair by probe shape")
        print_table(
            ["Battery", "Facility", "Events", "Avg seconds/event", "Avg s/probe"],
            shape_rows,
        )
    else:
        print("\nRepair summary: no repair events")

    tail = iterations[-min(tail_iterations, len(iterations)) :]
    print(f"\nLast {len(tail)} completed Phase 2 iterations")
    print(
        f"  iter={tail[0].iteration}..{tail[-1].iteration}, "
        f"LB={tail[0].lb:.6f}->{tail[-1].lb:.6f}, "
        f"gain={tail[-1].lb - tail[0].lb:.6f}"
    )
    print(
        f"  master={sum(item.master_ms for item in tail) / 1000.0:.3f}s, "
        f"RSP={sum(item.rsp_ms for item in tail) / 1000.0:.3f}s, "
        f"Exact/repair-UB={sum(item.exact_ms for item in tail) / 1000.0:.3f}s, "
        f"repair={sum(item.repair_ms for item in tail) / 1000.0:.3f}s, "
        f"repair_probes={sum(item.repair_probes for item in tail)}"
    )


def plot_log(log_path: Path, output_path: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    text = log_path.read_text(encoding="utf-8", errors="ignore")
    phase_one = required_match(PHASE_ONE_RE, text, "Phase 1 completion record")
    instance = required_match(INSTANCE_RE, text, "instance description")
    iterations = parse_iterations(text)
    events = parse_repair_events(text)
    phase_one_ms = float(phase_one.group("elapsed"))
    phase_two_seconds = (iterations[-1].elapsed_ms - phase_one_ms) / 1000.0

    time_minutes = [
        (item.elapsed_ms - phase_one_ms) / 60000.0 for item in iterations
    ]
    lower_bounds = [item.lb for item in iterations]
    upper_points = [
        (time, item.ub)
        for time, item in zip(time_minutes, iterations)
        if math.isfinite(item.ub)
    ]

    master_seconds = sum(item.master_ms for item in iterations) / 1000.0
    rsp_seconds = sum(item.rsp_ms for item in iterations) / 1000.0
    exact_seconds = sum(item.exact_ms for item in iterations) / 1000.0
    repair_seconds = sum(item.repair_ms for item in iterations) / 1000.0
    unaccounted_seconds = phase_two_seconds - (
        master_seconds + rsp_seconds + exact_seconds + repair_seconds
    )

    colors = {
        "Master": "#64748b",
        "RSP": "#2563eb",
        "Exact/repair-UB": "#0f766e",
        "Repair probes": "#dc2626",
        "Unaccounted": "#d4d4d4",
    }
    figure, axes = plt.subplots(2, 2, figsize=(12.0, 8.0), dpi=180)
    bounds_axis, allocation_axis = axes[0]
    iteration_axis, repair_axis = axes[1]

    bounds_axis.step(
        time_minutes,
        lower_bounds,
        where="post",
        color="#2563eb",
        linewidth=2.0,
        label="LB",
    )
    if upper_points:
        bounds_axis.step(
            [point[0] for point in upper_points],
            [point[1] for point in upper_points],
            where="post",
            color="#dc2626",
            linewidth=2.0,
            label="UB",
        )
    bounds_axis.axvline(0.0, color="#525252", linestyle="--", linewidth=1.0)
    bounds_axis.annotate(
        "Phase 1 complete",
        xy=(0.0, 1.0),
        xycoords=("data", "axes fraction"),
        xytext=(6, -8),
        textcoords="offset points",
        ha="left",
        va="top",
        fontsize=8,
        color="#525252",
    )
    bounds_axis.set_title("Bound progress")
    bounds_axis.set_xlabel("Phase 2 wall-clock time (min)")
    bounds_axis.set_ylabel("Objective value")
    bounds_axis.legend(frameon=False)

    allocation = [
        ("Master", master_seconds),
        ("RSP", rsp_seconds),
        ("Exact/repair-UB", exact_seconds),
        ("Repair probes", repair_seconds),
        ("Unaccounted", max(0.0, unaccounted_seconds)),
    ]
    left = 0.0
    for name, seconds in allocation:
        allocation_axis.barh(
            [0], [seconds], left=left, color=colors[name], height=0.42, label=name
        )
        if seconds / phase_two_seconds >= 0.075:
            allocation_axis.text(
                left + seconds / 2.0,
                0,
                f"{seconds:.0f}s\n{100.0 * seconds / phase_two_seconds:.1f}%",
                ha="center",
                va="center",
                fontsize=8,
                color="white" if name != "Unaccounted" else "#262626",
            )
        left += seconds
    allocation_axis.set_title("Phase 2 wall-clock allocation")
    allocation_axis.set_xlabel("Seconds")
    allocation_axis.set_yticks([])
    allocation_axis.legend(frameon=False, fontsize=8, loc="upper center", ncol=2)

    iteration_numbers = [item.iteration for item in iterations]
    bottoms = [0.0] * len(iterations)
    for name, values in (
        ("Master", [item.master_ms / 1000.0 for item in iterations]),
        ("RSP", [item.rsp_ms / 1000.0 for item in iterations]),
        ("Exact/repair-UB", [item.exact_ms / 1000.0 for item in iterations]),
        ("Repair probes", [item.repair_ms / 1000.0 for item in iterations]),
    ):
        iteration_axis.bar(
            iteration_numbers,
            values,
            bottom=bottoms,
            color=colors[name],
            width=0.82,
            label=name,
        )
        bottoms = [bottom + value for bottom, value in zip(bottoms, values)]
    iteration_axis.set_title("Per-iteration completed work")
    iteration_axis.set_xlabel("Phase 2 iteration")
    iteration_axis.set_ylabel("Wall-clock seconds")
    iteration_axis.legend(frameon=False, fontsize=8, ncol=2)

    if events:
        scatter = repair_axis.scatter(
            [event.iteration for event in events],
            [event.scenario for event in events],
            c=[event.repair_ms / 1000.0 for event in events],
            s=[22.0 + 4.0 * event.probes for event in events],
            cmap="magma",
            edgecolors="#404040",
            linewidths=0.35,
            alpha=0.88,
        )
        colorbar = figure.colorbar(scatter, ax=repair_axis, pad=0.02)
        colorbar.set_label("Scenario repair seconds")
        repair_axis.set_yticks(sorted({event.scenario for event in events}))
    repair_axis.set_title("Repair events (size = probe count)")
    repair_axis.set_xlabel("Phase 2 iteration")
    repair_axis.set_ylabel("Scenario")

    for axis in (bounds_axis, allocation_axis, iteration_axis, repair_axis):
        axis.grid(color="#d4d4d4", linewidth=0.6, alpha=0.65)
        axis.spines["top"].set_visible(False)
        axis.spines["right"].set_visible(False)

    instance_name = re.search(r"data=([^,]+)", instance.group(0)).group(1)
    cap = re.search(r"\bcap=(\d+)", required_match(RUNTIME_RE, text, "runtime options").group(0)).group(1)
    figure.suptitle(f"{instance_name} C{cap}: Phase 2 timing diagnosis", fontsize=13)
    figure.tight_layout(rect=(0.0, 0.0, 1.0, 0.96))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, bbox_inches="tight")
    plt.close(figure)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Diagnose Phase 2 and repair wall-clock time from solver logs."
    )
    parser.add_argument("logs", type=Path, nargs="+")
    parser.add_argument("--tail-iterations", type=int, default=10)
    parser.add_argument("--plot", type=Path)
    args = parser.parse_args()
    if args.tail_iterations <= 0:
        raise ValueError("--tail-iterations must be positive")
    if args.plot is not None and len(args.logs) != 1:
        raise ValueError("--plot requires exactly one log")
    for log_path in args.logs:
        summarize_log(log_path, args.tail_iterations)
    if args.plot is not None:
        plot_log(args.logs[0], args.plot)
        print(f"\nPlot: {args.plot}")


if __name__ == "__main__":
    main()
