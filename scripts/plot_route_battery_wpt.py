import csv
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np


ROOT = Path(__file__).resolve().parents[1]
SCENARIO_DIR = ROOT / "data" / "tower_5_data" / "base"
WITH_WPT_DIR = ROOT / "results" / "chapter5_2" / "full_with_wpt"
WITHOUT_WPT_DIR = ROOT / "results" / "chapter5_2" / "solver_no_wpt"
OUTPUT_PATH = ROOT / "out" / "route_battery_wpt_preview.png"

SWC_RATE_KWH_PER_MIN = 1.0
DWC_RATE_KWH_PER_MIN = 1.0
DWC_MINUTES_PER_TRAVERSAL = 1.0


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def read_misc_config():
    lines = (SCENARIO_DIR / "scenarioMiscCfg.txt").read_text(
        encoding="utf-8"
    ).splitlines()

    def value_after(marker):
        return lines[lines.index(marker) + 1]

    route_names = value_after("# routeNames").split(",")
    route_ids_by_eb = [int(value) for value in value_after("# routeIdByEBs").split(",")]
    number_of_ebs = int(value_after("# numEB"))
    trip_eb_start = lines.index("# tripEBs") + 1
    trips_by_eb = [
        [int(value) for value in lines[trip_eb_start + offset].split(",")]
        for offset in range(number_of_ebs)
    ]
    return route_names, route_ids_by_eb, trips_by_eb


def read_integer_rows(path):
    return [
        [int(value) for value in line.split(",")]
        for line in path.read_text(encoding="utf-8").splitlines()
    ]


def mean_battery_by_route(solution_dir):
    values = defaultdict(list)
    for row in read_csv(solution_dir / "route_battery_summary.csv"):
        values[int(row["route_id"])].append(float(row["battery_capacity"]))
    return {
        route_id: sum(capacities) / len(capacities)
        for route_id, capacities in values.items()
    }


def route_cycle_charging_upper_bounds():
    route_names, route_ids_by_eb, trips_by_eb = read_misc_config()
    trip_stations = read_integer_rows(SCENARIO_DIR / "tripStations.txt")
    trip_arrivals = read_integer_rows(SCENARIO_DIR / "tripArrTimes.txt")
    trip_departures = read_integer_rows(SCENARIO_DIR / "tripDepTimes.txt")

    selected_rows = read_csv(WITH_WPT_DIR / "selected_facilities.csv")
    selected_swc = {
        int(row["facility_id"])
        for row in selected_rows
        if row["facility_type"] == "station"
    }
    selected_dwc = {
        (int(row["from_node"]), int(row["to_node"]))
        for row in selected_rows
        if row["facility_type"] == "link"
    }

    route_trips = defaultdict(set)
    route_connectors = defaultdict(set)
    for eb_index, trips in enumerate(trips_by_eb):
        route_id = route_ids_by_eb[eb_index]
        route_trips[route_id].update(trips)
        for previous_trip, next_trip in zip(trips, trips[1:]):
            route_connectors[route_id].add(
                (trip_stations[previous_trip][-1], trip_stations[next_trip][0])
            )

    swc_upper_bounds = []
    dwc_upper_bounds = []
    for route_id in range(len(route_names)):
        # One representative trip is retained for each distinct route direction.
        representative_trips = {
            tuple(trip_stations[trip]): trip for trip in sorted(route_trips[route_id])
        }
        swc_upper_bound = 0.0
        dwc_traversals = 0

        for station_sequence, trip in representative_trips.items():
            for station_index, station in enumerate(station_sequence):
                if station in selected_swc:
                    dwell_time = (
                        trip_departures[trip][station_index]
                        - trip_arrivals[trip][station_index]
                    )
                    swc_upper_bound += SWC_RATE_KWH_PER_MIN * dwell_time

            dwc_traversals += sum(
                link in selected_dwc
                for link in zip(station_sequence, station_sequence[1:])
            )

        dwc_traversals += sum(
            link in selected_dwc for link in route_connectors[route_id]
        )
        swc_upper_bounds.append(swc_upper_bound)
        dwc_upper_bounds.append(
            DWC_RATE_KWH_PER_MIN * DWC_MINUTES_PER_TRAVERSAL * dwc_traversals
        )

    return route_names, swc_upper_bounds, dwc_upper_bounds


def draw_figure():
    route_names, swc_upper_bounds, dwc_upper_bounds = (
        route_cycle_charging_upper_bounds()
    )
    with_wpt = mean_battery_by_route(WITH_WPT_DIR)
    without_wpt = mean_battery_by_route(WITHOUT_WPT_DIR)

    x = np.arange(len(route_names))
    with_wpt_values = np.array([with_wpt[index] for index in range(len(route_names))])
    without_wpt_values = np.array(
        [without_wpt[index] for index in range(len(route_names))]
    )
    swc_values = np.array(swc_upper_bounds)
    dwc_values = np.array(dwc_upper_bounds)

    plt.rcParams.update(
        {
            "font.family": "DejaVu Sans",
            "font.size": 9,
            "axes.labelsize": 10,
            "xtick.labelsize": 8,
            "ytick.labelsize": 8,
            "legend.fontsize": 8.5,
        }
    )

    figure, battery_axis = plt.subplots(figsize=(11.2, 5.0), dpi=160)
    charging_axis = battery_axis.twinx()

    charging_axis.bar(
        x,
        swc_values,
        width=0.68,
        color="#8BA6A1",
        alpha=0.50,
        edgecolor="none",
        label="SWC charging upper bound",
        zorder=1,
    )
    charging_axis.bar(
        x,
        dwc_values,
        width=0.68,
        bottom=swc_values,
        color="#3F6668",
        alpha=0.78,
        edgecolor="none",
        label="DWC charging upper bound",
        zorder=2,
    )

    battery_axis.fill_between(
        x,
        with_wpt_values,
        without_wpt_values,
        color="#7699BE",
        alpha=0.20,
        linewidth=0,
        zorder=2,
    )
    battery_axis.plot(
        x,
        without_wpt_values,
        color="#174A72",
        marker="o",
        markersize=4.0,
        markerfacecolor="white",
        markeredgewidth=1.1,
        linewidth=1.8,
        label="Battery without WPT",
        zorder=4,
    )
    battery_axis.plot(
        x,
        with_wpt_values,
        color="#C45A24",
        marker="s",
        markersize=3.7,
        linewidth=1.8,
        label="Battery with WPT",
        zorder=5,
    )

    battery_axis.set_xlim(-0.7, len(route_names) - 0.3)
    battery_axis.set_ylim(bottom=0)
    charging_axis.set_ylim(bottom=0)
    battery_axis.set_xticks(x, route_names)
    battery_axis.set_xlabel("Route")
    battery_axis.set_ylabel("Mean route battery capacity (kWh)")
    charging_axis.set_ylabel("WPT charging upper bound (kWh/route cycle)")

    battery_axis.grid(axis="y", color="#D6DADD", linewidth=0.6, alpha=0.8)
    battery_axis.set_axisbelow(True)
    battery_axis.spines["top"].set_visible(False)
    charging_axis.spines["top"].set_visible(False)
    battery_axis.spines["right"].set_visible(False)
    charging_axis.spines["left"].set_visible(False)
    battery_axis.tick_params(axis="x", length=0, pad=5)
    charging_axis.tick_params(axis="y", colors="#3F6668")
    charging_axis.yaxis.label.set_color("#3F6668")

    battery_handles, battery_labels = battery_axis.get_legend_handles_labels()
    charging_handles, charging_labels = charging_axis.get_legend_handles_labels()
    battery_axis.legend(
        battery_handles + charging_handles,
        battery_labels + charging_labels,
        loc="upper center",
        bbox_to_anchor=(0.5, 1.13),
        ncol=4,
        frameon=False,
        handlelength=2.2,
        columnspacing=1.4,
    )

    figure.text(
        0.5,
        0.012,
        "Provisional convention: each selected DWC traversal contributes a one-minute charging opportunity; bars report grid-side upper bounds.",
        ha="center",
        va="bottom",
        fontsize=8,
        color="#4F5559",
    )
    figure.subplots_adjust(left=0.075, right=0.925, bottom=0.14, top=0.84)
    figure.savefig(OUTPUT_PATH, dpi=300, bbox_inches="tight", facecolor="white")
    plt.close(figure)


if __name__ == "__main__":
    draw_figure()
    print(OUTPUT_PATH)
