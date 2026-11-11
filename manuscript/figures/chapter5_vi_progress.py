"""Plot VI diagnostics using only the published target-machine log times."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from chapter5_progress import (
    BLUE, GREEN, TRACE_LINE_WIDTH, fields, plt, save,
)


def read_vi_log(path: Path):
    lines = path.read_text(encoding="utf-8").splitlines()
    rows = [fields(line) for line in lines
            if re.match(r"^Lifted ILS iter \d+,", line) and "elapsed_ms=" in line]
    finals = [fields(line) for line in lines if line.startswith("Lifted ILS final:")]
    vi_records = [fields(line) for line in lines if line.startswith("Lifted ILS VI audit:")]
    if not rows or len(finals) != 1 or len(vi_records) > 1:
        raise ValueError(f"Incomplete or ambiguous VI records in {path}")
    final = finals[0]
    if float(final["elapsed_ms"]) < float(rows[-1]["elapsed_ms"]):
        raise ValueError(f"Final record precedes a completed evaluation in {path}")
    return rows, final, vi_records[0] if vi_records else None


def master_return_time(row):
    downstream = float(row.get("rsp_ms", 0.0)) + float(row.get("subproblem_ms", 0.0))
    return (float(row["elapsed_ms"]) - downstream) / 1000.0


def plot_bounds(ax, rows, final, enabled):
    times = [master_return_time(row) for row in rows]
    bounds = [float(row["LB"]) for row in rows]
    terminal_time = float(final["elapsed_ms"]) / 1000
    color = GREEN if enabled else BLUE
    ax.step(times + [terminal_time], bounds + [float(final["LB"])], where="post",
            color=color, ls="-" if enabled else "--", lw=TRACE_LINE_WIDTH,
            marker="o" if enabled else None,
            markevery=list(range(len(rows))), ms=2.2,
            mfc="none", mew=0.4,
            label="With VI LB" if enabled else "No VI LB", zorder=3)
    rejected = [row for row in rows if row.get("vi_excluded") == "1"]
    if rejected:
        ax.scatter([master_return_time(row) for row in rejected],
                   [float(row["LB"]) for row in rejected], color=BLUE,
                   marker="x", s=14, linewidths=0.8,
                   label="Candidate violates VI", zorder=5)
    return terminal_time, len(rejected)


def plot_vi_progress(log_dir: Path, output: Path):
    fig, axes = plt.subplots(2, 1, figsize=(7.0, 5.0), sharex=True)
    endpoints = []
    for ax, panel, case, expected in zip(
        axes, ("a", "b"), ("R12-N5-F50-C4", "R28-N5-F50-C4"), (19, 3)
    ):
        on_rows, on_final, _ = read_vi_log(log_dir / f"{case}_lifted-ils.log")
        off_rows, off_final, vi_record = read_vi_log(log_dir / f"{case}_lifted-ils_noVI.log")
        if vi_record is None or len(off_rows) != expected:
            raise ValueError(f"Unexpected no-VI diagnostics for {case}")
        visited = int(vi_record["visited_points"])
        excluded = int(vi_record["excluded_visits"])
        if visited != expected + 1 or excluded != visited:
            raise ValueError(f"Unexpected no-VI violation counts for {case}")
        on_end, _ = plot_bounds(ax, on_rows, on_final, True)
        off_end, marked = plot_bounds(ax, off_rows, off_final, False)
        if marked != len(off_rows):
            raise ValueError(f"Expected every completed no-VI candidate to violate VI: {case}")
        endpoints.extend((on_end, off_end))
        ax.set_title(f"({panel}) {case.replace('-N', '-S')}", loc="left", fontsize=9)
        ax.set(ylabel="Lower bound", ylim=(0, max(float(on_final["LB"]),
                                                 float(off_final["LB"])) * 1.08))
        ax.grid(alpha=0.25)
        print(f"{case}: {marked} crosses at reconstructed master returns; "
              f"candidate violations {excluded}/{visited}, including one unfinished evaluation; "
              f"no-VI terminal LB {float(off_final['LB']):.6f} at {off_end:.3f} s")

    axes[0].legend(frameon=False, fontsize=7.5, loc="upper right")
    axes[-1].set(xlim=(0, max(endpoints) + 120),
                 xlabel="Elapsed algorithm time (s)")
    fig.tight_layout(h_pad=1.1)
    save(fig, output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    plot_vi_progress(args.log_dir, args.output_dir / "chapter5_vi_progress")


if __name__ == "__main__":
    main()
