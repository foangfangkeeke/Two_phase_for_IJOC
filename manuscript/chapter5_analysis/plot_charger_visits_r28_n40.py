"""Plot scheduled plug-in station visits in the final R28-N40 inputs.

A visit is counted when a scheduled trip begins at an existing plug-in station,
matching IsPlugInTerminalStop in the exact scenario model. Zero-dwell visits
are included. These are scheduled visits, not charging decisions in a plan.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import shutil

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import TwoSlopeNorm
import numpy as np
import plotly.graph_objects as go
from plotly.subplots import make_subplots

from plot_route_workload_r28_n40 import data_rows, scenario_names


def global_stations(root: Path) -> tuple[list[int], list[str]]:
    rows = data_rows(root / "globalCfg.txt")
    station_count = int(rows[7][0])
    stations = [int(rows[8 + index][0]) for index in range(station_count)]
    names_by_id = {
        int(row[1]): row[0] for row in data_rows(root / "map.txt")
    }
    if station_count != 5 or len(set(stations)) != station_count:
        raise ValueError(f"Expected five distinct existing plug-in stations in {root}")
    return stations, [names_by_id[station] for station in stations]


def visits_in_scenario(
    folder: Path, global_ids: list[int]
) -> tuple[list[str], np.ndarray, np.ndarray, np.ndarray, int, int]:
    rows = data_rows(folder / "scenarioMiscCfg.txt")
    bus_count = int(rows[2][0])
    trip_counts = [int(value) for value in rows[4]]
    if len(trip_counts) != bus_count:
        raise ValueError(f"Bus records disagree in {folder}")
    trip_ids_by_bus = [[int(value) for value in row] for row in rows[5 : 5 + bus_count]]
    if len(trip_ids_by_bus) != bus_count or any(
        len(trips) != trip_counts[index] for index, trips in enumerate(trip_ids_by_bus)
    ):
        raise ValueError(f"Bus trip assignments disagree in {folder}")

    charger_offset = 5 + bus_count
    local_count = int(rows[charger_offset][0])
    local_ids = [int(rows[charger_offset + 1 + index][0]) for index in range(local_count)]
    station_ids = set(global_ids) | set(local_ids)
    if station_ids != set(global_ids):
        raise ValueError(f"Scenario has additional plug-in locations in {folder}")

    route_offset = charger_offset + 1 + local_count
    route_count = int(rows[route_offset][0])
    route_names = rows[route_offset + 1]
    route_by_bus = [int(value) for value in rows[route_offset + 2]]
    if route_count != 28 or len(route_names) != route_count or len(route_by_bus) != bus_count:
        raise ValueError(f"Route records disagree in {folder}")

    trip_stations = data_rows(folder / "tripStations.txt")
    arrivals = data_rows(folder / "tripArrTimes.txt")
    departures = data_rows(folder / "tripDepTimes.txt")
    trip_total = len(trip_stations)
    if len(arrivals) != trip_total or len(departures) != trip_total:
        raise ValueError(f"Trip record counts disagree in {folder}")
    ids = [trip for trips in trip_ids_by_bus for trip in trips]
    if sorted(ids) != list(range(trip_total)):
        raise ValueError(f"Trip IDs are not a partition in {folder}")

    route_visits = np.zeros(route_count, dtype=int)
    station_visits = np.zeros(len(global_ids), dtype=int)
    station_dwell_minutes = np.zeros(len(global_ids), dtype=int)
    index_by_station = {station: index for index, station in enumerate(global_ids)}
    zero_dwell_visits = 0
    for bus_index, trip_ids in enumerate(trip_ids_by_bus):
        route = route_by_bus[bus_index]
        for trip in trip_ids:
            station = int(trip_stations[trip][0])
            if station not in station_ids:
                continue
            dwell = int(departures[trip][0]) - int(arrivals[trip][0])
            if dwell < 0:
                raise ValueError(f"Negative charging window in {folder}, trip {trip}")
            route_visits[route] += 1
            station_index = index_by_station[station]
            station_visits[station_index] += 1
            station_dwell_minutes[station_index] += dwell
            zero_dwell_visits += dwell == 0
    if route_visits.sum() != station_visits.sum():
        raise ValueError(f"Visit totals disagree in {folder}")
    return (
        route_names,
        route_visits,
        station_visits,
        station_dwell_minutes,
        zero_dwell_visits,
        trip_total,
    )


def static_station_plot(
    counts: np.ndarray, names: list[str], destination: Path
) -> None:
    station_indices = np.arange(1, len(names) + 1)
    scenario_indices = np.arange(1, counts.shape[0] + 1)
    x, y = np.meshgrid(station_indices, scenario_indices)
    fig = plt.figure(figsize=(13, 8))
    ax = fig.add_axes([0.00, 0.01, 0.95, 0.94], projection="3d", computed_zorder=False)
    ax.plot_surface(x, y, counts, cmap="viridis", linewidth=0, antialiased=False)
    ax.set_xlabel("Existing plug-in station", labelpad=13)
    ax.set_ylabel("Scenario number", labelpad=16)
    ax.set_zlabel("Scheduled visits", labelpad=12)
    ax.set_xticks(station_indices,
                  ["Bt Batok", "Clementi", "Jurong East", "Jurong TH", "Tengah"])
    ax.set_yticks([1, 5, 10, 15, 20, 25, 30, 35, 40])
    ax.set_zlim(0, int(counts.max() * 1.09))
    ax.set_box_aspect((0.95, 1.8, 0.9))
    ax.set_proj_type("ortho")
    ax.view_init(elev=28, azim=-58)
    fig.suptitle("R28-S40: scheduled visits by station and scenario", y=0.96, fontsize=15)
    fig.savefig(destination, dpi=220, facecolor="white")
    plt.close(fig)


def static_change_plot(
    counts: np.ndarray, names: list[str], destination: Path
) -> None:
    change = (counts - counts[[0], :]).T
    bound = max(1, int(np.max(np.abs(change))))
    fig, ax = plt.subplots(figsize=(13, 3.8), layout="constrained")
    image = ax.imshow(change, aspect="auto", cmap="RdBu_r",
                      norm=TwoSlopeNorm(vmin=-bound, vcenter=0, vmax=bound))
    ax.set_yticks(range(len(names)), names)
    ax.set_xticks([0, 4, 9, 14, 19, 24, 29, 34, 39],
                  [1, 5, 10, 15, 20, 25, 30, 35, 40])
    ax.set_xlabel("Scenario number")
    ax.set_title("Change in scheduled plug-in station visits relative to the base plan")
    for boundary in (4.5, 9.5, 19.5):
        ax.axvline(boundary, color="#333", linewidth=0.7, alpha=0.6)
    fig.colorbar(image, ax=ax, label="Change in visits")
    fig.savefig(destination, dpi=220, facecolor="white")
    plt.close(fig)


def interactive_plot(
    counts: np.ndarray, names: list[str], scenarios: list[str], destination: Path
) -> None:
    x = np.arange(1, len(scenarios) + 1)
    change = (counts - counts[[0], :]).T
    surface_meta = np.empty((len(scenarios), len(names), 2), dtype=object)
    for scenario_index, scenario in enumerate(scenarios):
        for station_index, station in enumerate(names):
            surface_meta[scenario_index, station_index] = [station, scenario]
    fig = make_subplots(
        rows=1, cols=2,
        specs=[[{"type": "surface"}, {"type": "heatmap"}]],
        column_widths=[0.61, 0.39], horizontal_spacing=0.05,
        subplot_titles=("Station visits (rotate and hover)", "Change from the base plan"),
    )
    fig.add_trace(
        go.Surface(
            x=np.arange(1, len(names) + 1),
            y=np.arange(1, len(scenarios) + 1),
            z=counts,
            customdata=surface_meta,
            colorscale="Viridis",
            colorbar=dict(title="Visits", x=0.56, len=0.68),
            hovertemplate=(
                "Station %{customdata[0]}<br>"
                "Scenario %{y}: %{customdata[1]}<br>"
                "Scheduled visits %{z}<extra></extra>"
            ),
        ),
        row=1, col=1,
    )
    meta = np.tile(np.asarray(scenarios, dtype=object), (len(names), 1))
    fig.add_trace(
        go.Heatmap(
            x=x, y=names, z=change, customdata=meta,
            colorscale="RdBu_r", zmid=0,
            colorbar=dict(title="Δ visits", x=1.01, len=0.68),
            hovertemplate=(
                "Scenario %{x}: %{customdata}<br>"
                "%{y}<br>Change from base %{z:+d}<extra></extra>"
            ),
        ),
        row=1, col=2,
    )
    fig.update_layout(
        title="R28-S40: scheduled visits by station and scenario",
        height=760, width=1500,
        margin=dict(l=20, r=40, t=90, b=65),
        scene=dict(
            xaxis=dict(title="Existing plug-in station",
                       tickvals=np.arange(1, len(names) + 1), ticktext=names),
            yaxis=dict(title="Scenario number", tickvals=[1, 5, 10, 20, 30, 40]),
            zaxis=dict(title="Scheduled visits", range=[0, int(counts.max() * 1.09)]),
            aspectratio=dict(x=0.9, y=1.7, z=0.9),
            camera=dict(
                eye=dict(x=1.10, y=-1.50, z=0.85),
                projection=dict(type="orthographic"),
            ),
        ),
        xaxis2=dict(title="Scenario number", tickvals=[1, 5, 10, 20, 30, 40]),
        yaxis2=dict(title="Station", autorange="reversed"),
    )
    fig.write_html(destination, include_plotlyjs=True, full_html=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, required=True)
    parser.add_argument(
        "--output-dir", type=Path,
        default=Path(__file__).parent / "diagnostics",
    )
    args = parser.parse_args()
    root = args.data_root.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    scenarios = scenario_names(root)
    station_ids, station_names = global_stations(root)

    route_names: list[str] | None = None
    by_route = []
    by_station = []
    dwell_by_station = []
    zero_dwell = 0
    trip_totals = []
    for scenario in scenarios:
        routes, route_counts, station_counts, dwell, zeros, trip_total = (
            visits_in_scenario(root / scenario, station_ids)
        )
        if route_names is None:
            route_names = routes
        elif routes != route_names:
            raise ValueError(f"Route order differs in scenario {scenario}")
        by_route.append(route_counts)
        by_station.append(station_counts)
        dwell_by_station.append(dwell)
        zero_dwell += zeros
        trip_totals.append(trip_total)
    route_counts = np.stack(by_route)
    station_counts = np.stack(by_station)
    dwell_minutes = np.stack(dwell_by_station)

    station_csv = output / "charger_visits_by_station_r28_n40.csv"
    with station_csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["scenario_number", "scenario", "station_id", "station",
                         "visits", "change_from_base", "scheduled_dwell_minutes"])
        for scenario_index, scenario in enumerate(scenarios):
            for station_index, station in enumerate(station_names):
                writer.writerow([
                    scenario_index + 1, scenario, station_ids[station_index], station,
                    station_counts[scenario_index, station_index],
                    station_counts[scenario_index, station_index] - station_counts[0, station_index],
                    dwell_minutes[scenario_index, station_index],
                ])

    route_csv = output / "charger_visits_by_route_r28_n40.csv"
    with route_csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["scenario_number", "scenario", "route", "visits", "change_from_base"])
        for scenario_index, scenario in enumerate(scenarios):
            for route_index, route in enumerate(route_names):
                writer.writerow([
                    scenario_index + 1, scenario, route,
                    route_counts[scenario_index, route_index],
                    route_counts[scenario_index, route_index] - route_counts[0, route_index],
                ])

    png_3d = output / "charger_visits_by_station_r28_n40_3d.png"
    png_change = output / "charger_visits_by_station_r28_n40_change.png"
    html = output / "charger_visits_by_station_r28_n40_interactive.html"
    paper_png = Path(__file__).parent.parent / "figures" / "chapter5_station_visits.png"
    static_station_plot(station_counts, station_names, png_3d)
    shutil.copyfile(png_3d, paper_png)
    static_change_plot(station_counts, station_names, png_change)
    interactive_plot(station_counts, station_names, scenarios, html)

    totals = station_counts.sum(axis=1)
    print(f"Stations: {list(zip(station_ids, station_names))}")
    print(f"Visits per scenario: {totals.min()}--{totals.max()} (base {totals[0]})")
    print(f"Visits / trips in base scenario: {totals[0]}/{trip_totals[0]}")
    print(f"Zero-dwell visits across all scenarios: {zero_dwell}")
    print(f"Changed station-scenario cells: {np.count_nonzero(station_counts[1:] != station_counts[0])}/195")
    print(f"Changed route-scenario cells: {np.count_nonzero(route_counts[1:] != route_counts[0])}/1092")
    print(f"Outputs: {png_3d}, {paper_png}, {png_change}, {html}, {station_csv}, {route_csv}")


if __name__ == "__main__":
    main()
