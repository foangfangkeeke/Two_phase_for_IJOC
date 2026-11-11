"""Inspect trips and buses by route and scenario in the final R28-N40 data.

Run with --data-root pointing to the final experiment snapshot's R28_N40_data.
The outputs are diagnostic figures, not manuscript figures.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import plotly.graph_objects as go
from plotly.subplots import make_subplots


def data_rows(path: Path) -> list[list[str]]:
    return [
        [item.strip() for item in line.split(",")]
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]


def scenario_names(data_root: Path) -> list[str]:
    rows = data_rows(data_root / "scenarioCfg.txt")
    expected = int(rows[0][0])
    names = [row[0] for row in rows[1:]]
    if expected != 40 or len(names) != expected:
        raise ValueError(f"Expected 40 scenarios, found {expected} declared and {len(names)} listed")
    return names


def count_by_route(folder: Path) -> tuple[list[str], np.ndarray, np.ndarray]:
    rows = data_rows(folder / "scenarioMiscCfg.txt")
    bus_count = int(rows[2][0])
    trip_counts = np.asarray(rows[4], dtype=int)
    if len(rows[3]) != bus_count or len(trip_counts) != bus_count:
        raise ValueError(f"Bus records disagree in {folder}")

    trip_ids = [int(value) for row in rows[5 : 5 + bus_count] for value in row]
    with (folder / "tripStations.txt").open(encoding="utf-8") as handle:
        trip_lines = sum(bool(line.strip()) for line in handle)
    if len(trip_ids) != trip_lines or sorted(trip_ids) != list(range(trip_lines)):
        raise ValueError(f"Trip assignment does not match tripStations.txt in {folder}")

    charger_offset = 5 + bus_count
    plug_in_locations = int(rows[charger_offset][0])
    route_offset = charger_offset + 1 + plug_in_locations
    route_count = int(rows[route_offset][0])
    routes = rows[route_offset + 1]
    route_by_bus = np.asarray(rows[route_offset + 2], dtype=int)
    if route_count != 28 or len(routes) != route_count or len(route_by_bus) != bus_count:
        raise ValueError(f"Route records disagree in {folder}")
    if np.any(route_by_bus < 0) or np.any(route_by_bus >= route_count):
        raise ValueError(f"Invalid route index in {folder}")
    route_trips = np.bincount(route_by_bus, weights=trip_counts, minlength=route_count).astype(int)
    route_buses = np.bincount(route_by_bus, minlength=route_count).astype(int)
    if route_trips.sum() != trip_lines or route_buses.sum() != bus_count:
        raise ValueError(f"Trip totals disagree in {folder}")
    return routes, route_trips, route_buses


def category(name: str) -> str:
    if name == "base":
        return "base"
    if name.startswith("route_"):
        return "individual route"
    if name.startswith("charger_"):
        return "terminal area"
    if name.startswith("global_") or name.startswith("lo"):
        return "systemwide"
    raise ValueError(f"Unrecognized scenario type: {name}")


def make_static(counts: np.ndarray, routes: list[str], destination: Path, unit: str) -> None:
    x, y = np.meshgrid(np.arange(1, 41), np.arange(1, 29))
    z = counts.T
    fig = plt.figure(figsize=(13, 8))
    ax = fig.add_axes([0.00, 0.01, 0.95, 0.94], projection="3d", computed_zorder=False)
    ax.plot_surface(x, y, z, cmap="viridis", linewidth=0, antialiased=False)
    ax.set_xlabel("Scenario number", labelpad=13)
    ax.set_ylabel("Route", labelpad=16)
    ax.set_zlabel(f"Number of {unit}", labelpad=12)
    ax.set_xticks([1, 5, 10, 15, 20, 25, 30, 35, 40])
    tick_routes = np.arange(1, 29, 3)
    ax.set_yticks(tick_routes, [routes[index - 1] for index in tick_routes])
    ax.set_zlim(0, int(np.max(z) * 1.08))
    ax.set_box_aspect((1.8, 1.15, 0.7))
    ax.view_init(elev=28, azim=-61)
    fig.suptitle(f"R28-S40: {unit} by scenario and route", y=0.96, fontsize=15)
    fig.savefig(destination, dpi=220, facecolor="white")
    plt.close(fig)


def make_interactive(
    counts: np.ndarray, routes: list[str], scenarios: list[str], destination: Path, unit: str
) -> None:
    x = np.arange(1, 41)
    y = np.arange(1, 29)
    z = counts.T
    change = z - z[:, [0]]
    meta = np.empty((28, 40, 2), dtype=object)
    for route_index, route in enumerate(routes):
        for scenario_index, scenario in enumerate(scenarios):
            meta[route_index, scenario_index] = [route, scenario]

    fig = make_subplots(
        rows=1,
        cols=2,
        specs=[[{"type": "surface"}, {"type": "heatmap"}]],
        column_widths=[0.63, 0.37],
        horizontal_spacing=0.04,
        subplot_titles=(f"{unit.title()} per route (rotate and hover)", "Change from the base plan"),
    )
    fig.add_trace(
        go.Surface(
            x=x,
            y=y,
            z=z,
            customdata=meta,
            colorscale="Viridis",
            colorbar=dict(title=unit.title(), x=0.57, len=0.68),
            hovertemplate=(
                "Scenario %{x}: %{customdata[1]}<br>"
                f"Route %{{customdata[0]}}<br>{unit.title()} %{{z}}<extra></extra>"
            ),
        ),
        row=1,
        col=1,
    )
    fig.add_trace(
        go.Heatmap(
            x=x,
            y=routes,
            z=change,
            customdata=meta,
            colorscale="RdBu_r",
            zmid=0,
            colorbar=dict(title=f"Δ {unit}", x=1.01, len=0.68),
            hovertemplate=(
                "Scenario %{x}: %{customdata[1]}<br>"
                "Route %{y}<br>Change from base %{z:+d}<extra></extra>"
            ),
        ),
        row=1,
        col=2,
    )
    fig.update_layout(
        title=f"R28-S40 service-plan scenarios: {unit} by route",
        height=820,
        width=1550,
        margin=dict(l=25, r=45, t=85, b=80),
        scene=dict(
            xaxis=dict(title="Scenario number", tickvals=[1, 5, 10, 20, 30, 40]),
            yaxis=dict(title="Route index", tickvals=y, ticktext=routes),
            zaxis=dict(title=f"Number of {unit}", range=[0, int(z.max() * 1.08)]),
            aspectratio=dict(x=1.7, y=1.15, z=0.75),
        ),
        xaxis2=dict(title="Scenario number", tickvals=[1, 5, 10, 20, 30, 40]),
        yaxis2=dict(title="Route", autorange="reversed"),
    )
    fig.add_annotation(
        x=0.5,
        y=-0.10,
        xref="paper",
        yref="paper",
        text="Both horizontal dimensions are categorical; the surface joins points only for visual inspection.",
        showarrow=False,
        font=dict(size=12, color="#555"),
    )
    fig.write_html(destination, include_plotlyjs=True, full_html=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument(
        "--output-dir", type=Path, default=Path(__file__).parent / "diagnostics"
    )
    args = parser.parse_args()
    data_root = args.data_root.resolve(strict=True)
    destination = args.output_dir.resolve()
    destination.mkdir(parents=True, exist_ok=True)

    scenarios = scenario_names(data_root)
    first_routes, first_trips, first_buses = count_by_route(data_root / scenarios[0])
    trip_matrix = [first_trips]
    bus_matrix = [first_buses]
    for scenario in scenarios[1:]:
        routes, trips, buses = count_by_route(data_root / scenario)
        if routes != first_routes:
            raise ValueError(f"Route order differs in scenario {scenario}")
        trip_matrix.append(trips)
        bus_matrix.append(buses)

    for unit, counts in (
        ("trips", np.stack(trip_matrix)),
        ("buses", np.stack(bus_matrix)),
    ):
        prefix = "trip_counts_r28_n40" if unit == "trips" else "bus_counts_r28_n40"
        csv_path = destination / f"{prefix}.csv"
        with csv_path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["scenario_number", "scenario", "type", "route", unit, "change_from_base"])
            for scenario_index, scenario in enumerate(scenarios):
                for route_index, route in enumerate(first_routes):
                    writer.writerow(
                        [
                            scenario_index + 1,
                            scenario,
                            category(scenario),
                            route,
                            counts[scenario_index, route_index],
                            counts[scenario_index, route_index] - counts[0, route_index],
                        ]
                    )

        png_path = destination / f"{prefix}_3d.png"
        html_path = destination / f"{prefix}_interactive.html"
        make_static(counts, first_routes, png_path, unit)
        make_interactive(counts, first_routes, scenarios, html_path, unit)
        totals = counts.sum(axis=1)
        changed_cells = np.count_nonzero(counts[1:] != counts[0])
        changed_totals = np.count_nonzero(totals[1:] != totals[0])
        print(
            f"{unit.title()} per scenario: {totals.min()}--{totals.max()} "
            f"(base {totals[0]}); changed route-scenario cells: "
            f"{changed_cells}/{39 * len(first_routes)}; changed scenario totals: "
            f"{changed_totals}/39"
        )
        print(f"Outputs: {png_path}, {html_path}, {csv_path}")

    print(f"Scenarios: {len(scenarios)}; routes: {len(first_routes)}")
    print(f"Scenario types: { {kind: sum(category(s) == kind for s in scenarios) for kind in ('base', 'systemwide', 'terminal area', 'individual route')} }")


if __name__ == "__main__":
    main()
