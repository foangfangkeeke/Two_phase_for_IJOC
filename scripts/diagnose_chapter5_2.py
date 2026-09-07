from __future__ import annotations

import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "tower_base_data"
SCENARIO = DATA / "base"
WPT = ROOT / "results" / "check_base1_full_with_wpt"
NO_WPT = ROOT / "results" / "check_base1_solver_no_wpt"
OUT = ROOT / "results" / "chapter5_2_diagnostics"

POS_TOL = 1.0e-6
FEAS_TOL = 1.0e-5

STATION_RE = re.compile(
    r"^sc\d+_c_b(?P<bus>\d+)_n(?P<trip>\d+)_s(?P<station>\d+)_i(?P<station_idx>\d+)$"
)
LINK_RE = re.compile(
    r"^sc\d+_c_b(?P<bus>\d+)_n(?P<trip_dep>\d+)_s(?P<station_dep>\d+)_i(?P<station_idx_dep>\d+)"
    r"To_n(?P<trip_arr>\d+)_s(?P<station_arr>\d+)_i(?P<station_idx_arr>\d+)$"
)
ENERGY_RE = re.compile(
    r"^sc\d+_e_(?P<state>Arr|Dep)_b(?P<bus>\d+)_n(?P<trip>\d+)_s(?P<station>\d+)_i(?P<station_idx>\d+)$"
)
NIGHT_RE = re.compile(r"^sc\d+_c_Night_b(?P<bus>\d+)$")
OCCUPANCY_RE = re.compile(
    r"^sc\d+_x_b(?P<bus>\d+)_n(?P<trip>\d+)_s(?P<station>\d+)_i(?P<station_idx>\d+)_t(?P<time>\d+)$"
)


def csv_ints(line: str) -> list[int]:
    return [int(x) for x in line.split(",")]


def numeric_table(path: Path, dtype: type) -> list[list]:
    rows = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if line and not line.startswith("#"):
            rows.append([dtype(x) for x in line.split(",")])
    return rows


def metric_file(path: Path) -> dict[str, str]:
    frame = pd.read_csv(path, dtype=str)
    return dict(zip(frame["metric"], frame["value"]))


def parse_inputs() -> dict:
    stations = numeric_table(SCENARIO / "tripStations.txt", int)
    arrivals = numeric_table(SCENARIO / "tripArrTimes.txt", int)
    departures = numeric_table(SCENARIO / "tripDepTimes.txt", int)
    assert len(stations) == len(arrivals) == len(departures)
    assert all(len(s) == len(a) == len(d) for s, a, d in zip(stations, arrivals, departures))

    cfg_lines = [
        line
        for line in (SCENARIO / "scenarioMiscCfg.txt").read_text(encoding="utf-8-sig").splitlines()
        if line and not line.startswith("#")
    ]
    start_distances = [float(x) for x in cfg_lines[0].split(",")]
    end_distances = [float(x) for x in cfg_lines[1].split(",")]
    bus_count = int(cfg_lines[2])
    buses = csv_ints(cfg_lines[3])
    trip_counts = csv_ints(cfg_lines[4])
    bus_trips = [csv_ints(cfg_lines[5 + i]) for i in range(bus_count)]
    charger_offset = 5 + bus_count
    scenario_charger_count = int(cfg_lines[charger_offset])
    route_offset = charger_offset + 1 + scenario_charger_count
    route_count = int(cfg_lines[route_offset])
    route_names = cfg_lines[route_offset + 1].split(",")
    route_by_bus = csv_ints(cfg_lines[route_offset + 2])

    assert len(start_distances) == len(end_distances) == bus_count
    assert len(buses) == len(trip_counts) == len(bus_trips) == len(route_by_bus) == bus_count
    assert all(len(trips) == count for trips, count in zip(bus_trips, trip_counts))
    assert len(route_names) == route_count

    global_lines = [
        line
        for line in (DATA / "globalCfg.txt").read_text(encoding="utf-8-sig").splitlines()
        if line and not line.startswith("#")
    ]
    operation = [float(x) for x in global_lines[0].split(",")]
    bus_energy = [float(x) for x in global_lines[2].split(",")]
    rates = [float(x) for x in global_lines[4].split(",")]
    efficiencies = [float(x) for x in global_lines[5].split(",")]
    plug_count = int(global_lines[7])
    plug_rows = [csv_ints(global_lines[8 + i]) for i in range(plug_count)]
    plug_stations = {row[0] for row in plug_rows}
    plug_capacity = {row[0]: row[1] for row in plug_rows}

    distance_rows = numeric_table(SCENARIO / "linkDistance.txt", float)
    distances = {}
    for start, end, distance in distance_rows:
        distances[(int(start), int(end))] = distance

    facilities = pd.read_csv(WPT / "selected_facilities.csv")
    selected_swc = set(
        facilities.loc[facilities["facility_type"].eq("station"), "facility_id"].astype(int)
    )
    selected_dwc = set(
        zip(
            facilities.loc[facilities["facility_type"].eq("link"), "from_node"].astype(int),
            facilities.loc[facilities["facility_type"].eq("link"), "to_node"].astype(int),
        )
    )

    return {
        "stations": stations,
        "arrivals": arrivals,
        "departures": departures,
        "buses": buses,
        "bus_trips": bus_trips,
        "route_by_bus": route_by_bus,
        "route_names": route_names,
        "distances": distances,
        "distance_row_count": len(distance_rows),
        "plug_stations": plug_stations,
        "plug_capacity": plug_capacity,
        "selected_swc": selected_swc,
        "selected_dwc": selected_dwc,
        "rates": rates,
        "efficiencies": efficiencies,
        "soc_min": operation[4],
        "soc_max": operation[3],
        "curb_weight": bus_energy[0],
        "weight_per_energy": bus_energy[1],
        "mu": bus_energy[2],
        "g": bus_energy[3],
    }


def construct_events(data: dict) -> tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame]:
    station_rows = []
    link_rows = []
    duty_rows = []
    for bus_idx, (bus, trips, route) in enumerate(
        zip(data["buses"], data["bus_trips"], data["route_by_bus"])
    ):
        for trip_pos, trip in enumerate(trips):
            station_ids = data["stations"][trip]
            arr = data["arrivals"][trip]
            dep = data["departures"][trip]
            for idx, station in enumerate(station_ids):
                station_rows.append(
                    {
                        "bus": bus,
                        "bus_idx": bus_idx,
                        "route": route,
                        "trip": trip,
                        "trip_pos": trip_pos,
                        "station": station,
                        "station_idx": idx,
                        "arrival": arr[idx],
                        "departure": dep[idx],
                        "window": dep[idx] - arr[idx],
                        "is_plugin": idx == 0 and station in data["plug_stations"],
                        "is_selected_swc": station in data["selected_swc"],
                    }
                )
                if idx + 1 < len(station_ids):
                    link_rows.append(
                        {
                            "bus": bus,
                            "bus_idx": bus_idx,
                            "route": route,
                            "trip_dep": trip,
                            "station_dep": station,
                            "station_idx_dep": idx,
                            "trip_arr": trip,
                            "station_arr": station_ids[idx + 1],
                            "station_idx_arr": idx + 1,
                            "departure": dep[idx],
                            "arrival": arr[idx + 1],
                            "window": arr[idx + 1] - dep[idx],
                            "intertrip": False,
                            "terminal_match": True,
                        }
                    )
                elif trip_pos + 1 < len(trips):
                    next_trip = trips[trip_pos + 1]
                    next_station = data["stations"][next_trip][0]
                    next_arrival = data["arrivals"][next_trip][0]
                    link_rows.append(
                        {
                            "bus": bus,
                            "bus_idx": bus_idx,
                            "route": route,
                            "trip_dep": trip,
                            "station_dep": station,
                            "station_idx_dep": idx,
                            "trip_arr": next_trip,
                            "station_arr": next_station,
                            "station_idx_arr": 0,
                            "departure": dep[idx],
                            "arrival": next_arrival,
                            "window": next_arrival - dep[idx],
                            "intertrip": True,
                            "terminal_match": station == next_station,
                        }
                    )
        duty_rows.append(
            {
                "bus": bus,
                "route": route,
                "trips": len(trips),
                "start": data["arrivals"][trips[0]][0],
                "end": data["departures"][trips[-1]][-1],
            }
        )

    station_events = pd.DataFrame(station_rows)
    link_events = pd.DataFrame(link_rows)
    duty_events = pd.DataFrame(duty_rows)
    duty_events["span"] = duty_events["end"] - duty_events["start"]
    link_events["is_selected_dwc"] = [
        (a, b) in data["selected_dwc"]
        for a, b in zip(link_events["station_dep"], link_events["station_arr"])
    ]
    link_events["distance"] = [
        0.0 if transfer else data["distances"].get((a, b), np.nan)
        for a, b, transfer in zip(
            link_events["station_dep"], link_events["station_arr"], link_events["intertrip"]
        )
    ]
    return station_events, link_events, duty_events


def extract_rows(frame: pd.DataFrame, pattern: re.Pattern) -> pd.DataFrame:
    fields = frame["var_name"].str.extract(pattern)
    keep = fields.notna().all(axis=1)
    result = fields.loc[keep].copy()
    for column in result.columns:
        if column != "state":
            result[column] = result[column].astype(int)
    result["value"] = frame.loc[keep, "value"].astype(float).to_numpy()
    result["var_name"] = frame.loc[keep, "var_name"].to_numpy()
    return result.reset_index(drop=True)


def load_solution(path: Path, detail_name: str, label: str) -> dict:
    raw = pd.read_csv(path / detail_name, usecols=["var_name", "value"])
    station = extract_rows(raw, STATION_RE)
    link = extract_rows(raw, LINK_RE)
    energy = extract_rows(raw, ENERGY_RE)
    night = extract_rows(raw, NIGHT_RE)
    occupancy = extract_rows(raw, OCCUPANCY_RE)
    return {
        "label": label,
        "station": station,
        "link": link,
        "energy": energy,
        "night": night,
        "occupancy": occupancy,
        "battery": pd.read_csv(path / "route_battery_summary.csv"),
        "metadata": metric_file(path / "solution_summary.csv"),
    }


def attach_solution(
    solution: dict, station_events: pd.DataFrame, link_events: pd.DataFrame, data: dict
) -> dict:
    station_keys = ["bus", "trip", "station", "station_idx"]
    link_keys = [
        "bus",
        "trip_dep",
        "station_dep",
        "station_idx_dep",
        "trip_arr",
        "station_arr",
        "station_idx_arr",
    ]
    station = station_events.merge(
        solution["station"][station_keys + ["value"]].rename(columns={"value": "charge"}),
        on=station_keys,
        how="left",
        validate="one_to_one",
    )
    link = link_events.merge(
        solution["link"][link_keys + ["value"]].rename(columns={"value": "charge"}),
        on=link_keys,
        how="left",
        validate="one_to_one",
    )
    assert station["charge"].notna().all() and link["charge"].notna().all()

    station["mode"] = np.where(
        station["is_plugin"], "Plug-in", np.where(station["station"].isin(data["plug_stations"]), "Invalid plug-in stop", "SWC")
    )
    station["charge_ub"] = np.where(
        station["mode"].eq("Plug-in"), data["rates"][0] * station["window"], data["rates"][1] * station["window"]
    )
    link["charge_ub"] = np.where(link["intertrip"], 0.0, data["rates"][2] * link["window"])

    capacity = solution["battery"].set_index("route_id")["battery_capacity"]
    energy = solution["energy"].pivot(
        index=station_keys, columns="state", values="value"
    ).reset_index()
    energy = station[station_keys + ["route", "mode", "charge"]].merge(
        energy, on=station_keys, how="left", validate="one_to_one"
    )
    energy["capacity"] = energy["route"].map(capacity)
    energy["soc_arr"] = energy["Arr"] / energy["capacity"]
    energy["soc_dep"] = energy["Dep"] / energy["capacity"]
    energy["efficiency"] = np.where(
        energy["station"].isin(data["plug_stations"]), data["efficiencies"][0], data["efficiencies"][1]
    )
    energy["station_residual"] = (
        energy["Dep"] - energy["Arr"] - energy["efficiency"] * energy["charge"]
    )

    energy_lookup = energy[station_keys + ["Arr", "Dep"]]
    dep = energy_lookup.rename(
        columns={"trip": "trip_dep", "station": "station_dep", "station_idx": "station_idx_dep", "Dep": "energy_dep"}
    )[["bus", "trip_dep", "station_dep", "station_idx_dep", "energy_dep"]]
    arr = energy_lookup.rename(
        columns={"trip": "trip_arr", "station": "station_arr", "station_idx": "station_idx_arr", "Arr": "energy_arr"}
    )[["bus", "trip_arr", "station_arr", "station_idx_arr", "energy_arr"]]
    link_energy = link.merge(dep, on=["bus", "trip_dep", "station_dep", "station_idx_dep"], validate="many_to_one")
    link_energy = link_energy.merge(arr, on=["bus", "trip_arr", "station_arr", "station_idx_arr"], validate="many_to_one")
    energy_scale = float(solution["metadata"]["energy_scale"])
    consumption = (
        link_energy["distance"]
        * data["curb_weight"]
        * data["mu"]
        * energy_scale
        * data["g"]
        / 3.6
    )
    link_energy["link_residual"] = (
        link_energy["energy_arr"]
        - link_energy["energy_dep"]
        - data["efficiencies"][2] * link_energy["charge"]
        + consumption
    )

    occupancy = solution["occupancy"]
    occupancy_by_event = occupancy.groupby(station_keys, as_index=False)["value"].sum().rename(columns={"value": "occupied_minutes"})
    station = station.merge(occupancy_by_event, on=station_keys, how="left")
    station["occupied_minutes"] = station["occupied_minutes"].fillna(0.0)
    station["plugin_x_ub"] = data["rates"][0] * station["occupied_minutes"]

    return {
        **solution,
        "station_events": station,
        "link_events": link,
        "energy_events": energy,
        "link_energy": link_energy,
    }


def add_metric(rows: list[dict], category: str, metric: str, value: float, detail: str = "") -> None:
    rows.append({"category": category, "metric": metric, "value": value, "detail": detail})


def network_diagnostics(
    data: dict, station: pd.DataFrame, link: pd.DataFrame, duty: pd.DataFrame
) -> pd.DataFrame:
    rows = []
    assigned = [trip for trips in data["bus_trips"] for trip in trips]
    counts = pd.Series(assigned).value_counts()
    all_trip_ids = set(range(len(data["stations"])))
    add_metric(rows, "assignment", "trip rows", len(data["stations"]))
    add_metric(rows, "assignment", "assigned trips", len(assigned))
    add_metric(rows, "assignment", "duplicate trip assignments", int((counts - 1).clip(lower=0).sum()))
    add_metric(rows, "assignment", "unassigned trips", len(all_trip_ids - set(assigned)))

    add_metric(rows, "station timing", "negative dwell windows", int((station["window"] < 0).sum()))
    add_metric(rows, "station timing", "zero dwell windows", int((station["window"] == 0).sum()))
    add_metric(rows, "station timing", "zero plug-in dwell windows", int(((station["window"] == 0) & station["is_plugin"]).sum()))
    add_metric(rows, "station timing", "maximum dwell window (min)", float(station["window"].max()))
    intra = link.loc[~link["intertrip"]]
    transfers = link.loc[link["intertrip"]]
    add_metric(rows, "link timing", "negative intra-trip travel windows", int((intra["window"] < 0).sum()))
    add_metric(rows, "link timing", "zero intra-trip travel windows", int((intra["window"] == 0).sum()))
    add_metric(rows, "link timing", "maximum intra-trip travel window (min)", float(intra["window"].max()))
    add_metric(rows, "transfer timing", "negative inter-trip windows", int((transfers["window"] < 0).sum()))
    add_metric(rows, "transfer timing", "zero inter-trip windows", int((transfers["window"] == 0).sum()))
    add_metric(rows, "transfer timing", "maximum inter-trip window (min)", float(transfers["window"].max()))
    add_metric(rows, "transfer timing", "inter-trip windows above 120 min", int((transfers["window"] > 120).sum()))
    add_metric(rows, "transfer topology", "terminal mismatches", int((~transfers["terminal_match"]).sum()))
    add_metric(rows, "network", "missing intra-trip distances", int(intra["distance"].isna().sum()))
    add_metric(rows, "network", "nonpositive intra-trip distances", int((intra["distance"] <= 0).sum()))
    add_metric(rows, "network", "physical stations", station["station"].nunique())
    add_metric(rows, "network", "directed-link records", data["distance_row_count"])
    add_metric(rows, "network", "unique directed links", len(data["distances"]))
    speed = 60.0 * intra["distance"] / intra["window"]
    add_metric(rows, "network", "maximum implied in-service speed (km/h)", float(speed.max()))
    add_metric(rows, "network", "in-service events above 80 km/h", int((speed > 80).sum()))
    add_metric(rows, "duty", "negative duty spans", int((duty["span"] < 0).sum()))

    repeated_stations = 0
    repeated_links = 0
    immediate_repeats = 0
    for station_ids in data["stations"]:
        repeated_stations += len(station_ids) - len(set(station_ids))
        edges = list(zip(station_ids[:-1], station_ids[1:]))
        repeated_links += len(edges) - len(set(edges))
        immediate_repeats += sum(a == b for a, b in edges)
    trips_with_station_repeats = sum(len(stops) != len(set(stops)) for stops in data["stations"])
    trips_with_link_repeats = 0
    for stops in data["stations"]:
        edges = list(zip(stops[:-1], stops[1:]))
        trips_with_link_repeats += len(edges) != len(set(edges))
    add_metric(rows, "network", "repeated station occurrences within trips", repeated_stations)
    add_metric(rows, "network", "trips containing repeated stations", trips_with_station_repeats)
    add_metric(rows, "network", "repeated directed-link occurrences within trips", repeated_links)
    add_metric(rows, "network", "trips containing repeated directed links", trips_with_link_repeats)
    add_metric(rows, "network", "immediate repeated-station links", immediate_repeats)
    return pd.DataFrame(rows)


def solution_diagnostics(solution: dict, data: dict) -> pd.DataFrame:
    rows = []
    label = solution["label"]
    station = solution["station_events"]
    link = solution["link_events"]
    energy = solution["energy_events"]
    link_energy = solution["link_energy"]
    occupancy = solution["occupancy"]

    add_metric(rows, label, "station charge above time-window upper bound", int((station["charge"] > station["charge_ub"] + FEAS_TOL).sum()))
    add_metric(rows, label, "link charge above time-window upper bound", int((link["charge"] > link["charge_ub"] + FEAS_TOL).sum()))
    add_metric(rows, label, "positive charge at invalid plug-in stop", int(((station["mode"] == "Invalid plug-in stop") & (station["charge"] > POS_TOL)).sum()))
    add_metric(rows, label, "positive charge at unselected SWC", int(((station["mode"] == "SWC") & ~station["is_selected_swc"] & (station["charge"] > POS_TOL)).sum()))
    add_metric(rows, label, "positive charge at unselected DWC", int((~link["is_selected_dwc"] & (link["charge"] > POS_TOL)).sum()))
    add_metric(rows, label, "positive charge on inter-trip connector", int((link["intertrip"] & (link["charge"] > POS_TOL)).sum()))
    add_metric(rows, label, "plug-in charge above occupied-minute upper bound", int(((station["mode"] == "Plug-in") & (station["charge"] > station["plugin_x_ub"] + FEAS_TOL)).sum()))

    if len(occupancy):
        occupied = occupancy.loc[occupancy["value"] > 0.5]
        concurrency = occupied.groupby(["station", "time"]).size()
        capacity = pd.Series(data["plug_capacity"])
        limits = concurrency.index.get_level_values("station").map(capacity)
        add_metric(rows, label, "plug-in occupancy above charger capacity", int((concurrency.to_numpy() > limits.to_numpy()).sum()))
        occupied = occupied.merge(
            station[["bus", "trip", "station", "station_idx", "arrival", "departure"]],
            on=["bus", "trip", "station", "station_idx"],
            validate="many_to_one",
        )
        invalid_time = (occupied["time"] < occupied["arrival"]) | (occupied["time"] >= occupied["departure"])
        add_metric(rows, label, "occupied minutes outside dwell window", int(invalid_time.sum()))

    soc_low = min(data["soc_min"], data["soc_max"])
    soc_high = max(data["soc_min"], data["soc_max"])
    soc_values = pd.concat([energy["soc_arr"], energy["soc_dep"]], ignore_index=True)
    add_metric(rows, label, "SoC values below lower bound", int((soc_values < soc_low - FEAS_TOL).sum()))
    add_metric(rows, label, "SoC values above upper bound", int((soc_values > soc_high + FEAS_TOL).sum()))
    add_metric(rows, label, "maximum station-balance residual (kWh)", float(energy["station_residual"].abs().max()))
    add_metric(rows, label, "maximum link-balance residual (kWh)", float(link_energy["link_residual"].abs().max()))
    reconstructed_total = (
        station["charge"].sum() + link["charge"].sum() + solution["night"]["value"].sum()
    )
    exported_total = float(solution["metadata"]["expected_total_charge_kwh"])
    add_metric(rows, label, "reconstructed total grid-side charge (kWh)", reconstructed_total)
    add_metric(rows, label, "exported total grid-side charge (kWh)", exported_total)
    add_metric(rows, label, "absolute detailed-vs-summary charge difference (kWh)", abs(reconstructed_total - exported_total))
    return pd.DataFrame(rows)


def ecdf(ax, values: pd.Series, label: str, color: str) -> None:
    values = np.sort(values.to_numpy(dtype=float))
    y = np.arange(1, len(values) + 1) / len(values)
    ax.step(values, y, where="post", label=label, color=color, linewidth=1.8)


def plot_opportunities(station: pd.DataFrame, link: pd.DataFrame) -> None:
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    plug = station.loc[station["is_plugin"], "window"]
    swc_all = station.loc[~station["station"].isin(DATA_CACHE["plug_stations"]), "window"]
    swc_selected = station.loc[station["is_selected_swc"], "window"]
    ecdf(axes[0, 0], plug, "Plug-in terminal events", "#0072B2")
    ecdf(axes[0, 0], swc_all, "All SWC candidate events", "#999999")
    ecdf(axes[0, 0], swc_selected, "Selected-SWC events", "#D55E00")
    axes[0, 0].set(xlabel="Dwell window (min)", ylabel="Empirical CDF", title="Station charging opportunities")
    axes[0, 0].legend(frameon=False)

    intra = link.loc[~link["intertrip"]]
    ecdf(axes[0, 1], intra["window"], "All directed traversals", "#999999")
    ecdf(axes[0, 1], intra.loc[intra["is_selected_dwc"], "window"], "Selected-DWC traversals", "#009E73")
    axes[0, 1].set(xlabel="Travel window (min)", ylabel="Empirical CDF", title="DWC charging opportunities")
    axes[0, 1].legend(frameon=False)

    transfers = link.loc[link["intertrip"]]
    axes[1, 0].hist(transfers["window"], bins=40, color="#56B4E9", edgecolor="white")
    axes[1, 0].axvline(0, color="black", linewidth=0.8)
    axes[1, 0].set(xlabel="Connector window (min)", ylabel="Count", title="Inter-trip connector windows (not chargeable)")

    labels = ["Plug-in\ndwell", "All SWC\ndwell", "Selected SWC\ndwell", "All DWC\ntravel", "Selected DWC\ntravel", "Inter-trip\nconnector"]
    groups = [plug, swc_all, swc_selected, intra["window"], intra.loc[intra["is_selected_dwc"], "window"], transfers["window"]]
    percentiles = np.array([[np.min(x), np.median(x), np.quantile(x, 0.95), np.quantile(x, 0.99), np.max(x)] for x in groups])
    im = axes[1, 1].imshow(percentiles, aspect="auto", cmap="YlGnBu")
    axes[1, 1].set_xticks(range(5), ["Min", "Median", "P95", "P99", "Max"])
    axes[1, 1].set_yticks(range(len(labels)), labels)
    axes[1, 1].set_title("Opportunity-window percentiles (min)")
    for i in range(percentiles.shape[0]):
        for j in range(percentiles.shape[1]):
            axes[1, 1].text(j, i, f"{percentiles[i, j]:.0f}", ha="center", va="center", fontsize=8)
    fig.colorbar(im, ax=axes[1, 1], fraction=0.046, pad=0.04)
    fig.tight_layout()
    fig.savefig(OUT / "01_charging_opportunity_windows.png", dpi=220)
    plt.close(fig)


def charging_summary(solution: dict) -> pd.DataFrame:
    station = solution["station_events"]
    link = solution["link_events"]
    rows = []
    for mode in ["Plug-in", "SWC"]:
        subset = station.loc[station["mode"].eq(mode)]
        positive = subset.loc[subset["charge"] > POS_TOL]
        rows.append({"solution": solution["label"], "mode": mode, "energy": subset["charge"].sum(), "events": len(positive)})
    positive_link = link.loc[link["charge"] > POS_TOL]
    rows.append({"solution": solution["label"], "mode": "DWC", "energy": link["charge"].sum(), "events": len(positive_link)})
    rows.append({"solution": solution["label"], "mode": "Night", "energy": solution["night"]["value"].sum(), "events": int((solution["night"]["value"] > POS_TOL).sum())})
    return pd.DataFrame(rows)


def plot_charging(wpt: dict, no_wpt: dict, data: dict) -> None:
    summary = pd.concat([charging_summary(wpt), charging_summary(no_wpt)], ignore_index=True)
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    modes = ["Plug-in", "SWC", "DWC", "Night"]
    x = np.arange(len(modes))
    width = 0.36
    for offset, solution, color in [(-width / 2, "With WPT", "#0072B2"), (width / 2, "No WPT", "#D55E00")]:
        part = summary.loc[summary["solution"].eq(solution)].set_index("mode").reindex(modes)
        axes[0, 0].bar(x + offset, part["energy"], width, label=solution, color=color)
        axes[0, 1].bar(x + offset, part["events"], width, label=solution, color=color)
    axes[0, 0].set_xticks(x, modes)
    axes[0, 0].set(ylabel="Grid-side energy (kWh)", title="Charging energy reconstructed from detailed variables")
    axes[0, 1].set_xticks(x, modes)
    axes[0, 1].set(ylabel="Positive charging events", title="Positive charging-event counts")
    axes[0, 0].legend(frameon=False)

    util_frames = []
    station = wpt["station_events"]
    for mode in ["Plug-in", "SWC"]:
        subset = station.loc[(station["mode"] == mode) & (station["charge"] > POS_TOL)].copy()
        denominator = subset["plugin_x_ub"] if mode == "Plug-in" else subset["charge_ub"]
        subset["utilization"] = subset["charge"] / denominator.replace(0, np.nan)
        subset["util_mode"] = mode
        util_frames.append(subset[["utilization", "util_mode"]])
    subset = wpt["link_events"].loc[wpt["link_events"]["charge"] > POS_TOL].copy()
    subset["utilization"] = subset["charge"] / subset["charge_ub"].replace(0, np.nan)
    subset["util_mode"] = "DWC"
    util_frames.append(subset[["utilization", "util_mode"]])
    utilization = pd.concat(util_frames, ignore_index=True).dropna()
    arrays = [utilization.loc[utilization["util_mode"].eq(mode), "utilization"] for mode in ["Plug-in", "SWC", "DWC"]]
    axes[1, 0].boxplot(arrays, tick_labels=["Plug-in", "SWC", "DWC"], showfliers=False)
    axes[1, 0].axhline(1.0, color="black", linestyle="--", linewidth=0.8)
    axes[1, 0].set(ylabel="Charge / available upper bound", title="Utilization of positive WPT-solution events")

    occupied = wpt["occupancy"].loc[wpt["occupancy"]["value"] > 0.5]
    concurrency = occupied.groupby(["station", "time"]).size()
    station_max = concurrency.groupby(level="station").max().reindex(sorted(data["plug_stations"]), fill_value=0)
    axes[1, 1].bar(station_max.index.astype(str), station_max.values, color="#009E73")
    axes[1, 1].axhline(max(data["plug_capacity"].values()), color="black", linestyle="--", label="Capacity")
    axes[1, 1].set(xlabel="Plug-in station ID", ylabel="Maximum simultaneous occupancies", title="Plug-in capacity check")
    axes[1, 1].legend(frameon=False)
    fig.tight_layout()
    fig.savefig(OUT / "02_charging_energy_and_utilization.png", dpi=220)
    plt.close(fig)
    summary.to_csv(OUT / "charging_summary.csv", index=False)


def plot_facility_exposure(wpt: dict, station: pd.DataFrame, link: pd.DataFrame, data: dict) -> None:
    swc_exposure = station.loc[~station["station"].isin(data["plug_stations"])].groupby("station").size().rename("exposure")
    swc_charge = wpt["station_events"].loc[wpt["station_events"]["charge"] > POS_TOL].groupby("station").size().rename("positive_charges")
    swc = pd.DataFrame(index=sorted(set(swc_exposure.index))).join(swc_exposure).join(swc_charge).fillna(0)
    swc["selected"] = swc.index.isin(data["selected_swc"])

    intra = link.loc[~link["intertrip"]]
    dwc_exposure = intra.groupby(["station_dep", "station_arr"]).size().rename("exposure")
    dwc_charge = wpt["link_events"].loc[wpt["link_events"]["charge"] > POS_TOL].groupby(["station_dep", "station_arr"]).size().rename("positive_charges")
    dwc = pd.DataFrame(index=pd.MultiIndex.from_tuples(sorted(data["distances"]), names=["station_dep", "station_arr"]))
    dwc = dwc.join(dwc_exposure).join(dwc_charge).fillna(0)
    dwc["selected"] = dwc.index.isin(data["selected_dwc"])

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    for frame, ax, title, color in [(swc, axes[0], "SWC candidates", "#D55E00"), (dwc, axes[1], "DWC candidates", "#009E73")]:
        ax.scatter(frame.loc[~frame["selected"], "exposure"], frame.loc[~frame["selected"], "positive_charges"], s=14, alpha=0.35, color="#888888", label="Not selected")
        ax.scatter(frame.loc[frame["selected"], "exposure"], frame.loc[frame["selected"], "positive_charges"], s=38, alpha=0.9, color=color, label="Selected")
        ax.set(xlabel="Operational event exposure", ylabel="Positive charging events", title=title)
        ax.legend(frameon=False)
    fig.tight_layout()
    fig.savefig(OUT / "03_facility_exposure.png", dpi=220)
    plt.close(fig)
    swc.reset_index(names="station").to_csv(OUT / "swc_facility_diagnostics.csv", index=False)
    dwc.reset_index().to_csv(OUT / "dwc_facility_diagnostics.csv", index=False)


def plot_routes(wpt: dict, no_wpt: dict, station: pd.DataFrame, link: pd.DataFrame, data: dict) -> None:
    routes = np.arange(len(data["route_names"]))
    wpt_battery = wpt["battery"].set_index("route_id")["battery_capacity"].reindex(routes)
    no_wpt_battery = no_wpt["battery"].set_index("route_id")["battery_capacity"].reindex(routes)
    swc_count = station.loc[station["is_selected_swc"]].groupby("route")["station"].nunique().reindex(routes, fill_value=0)
    dwc_count = link.loc[(~link["intertrip"]) & link["is_selected_dwc"]].groupby("route").apply(lambda x: len(set(zip(x["station_dep"], x["station_arr"]))), include_groups=False).reindex(routes, fill_value=0)

    fig, ax = plt.subplots(figsize=(13, 5.6))
    ax.plot(routes, no_wpt_battery, marker="o", markersize=3.5, color="#D55E00", label="Battery without WPT")
    ax.plot(routes, wpt_battery, marker="o", markersize=3.5, color="#0072B2", label="Battery with WPT")
    ax.fill_between(routes, wpt_battery, no_wpt_battery, color="#56B4E9", alpha=0.18)
    ax.set(xlabel="Route", ylabel="Selected battery capacity (kWh)")
    ax.set_xticks(routes, data["route_names"], rotation=55, ha="right", fontsize=8)
    ax2 = ax.twinx()
    ax2.bar(routes - 0.16, swc_count, width=0.32, color="#009E73", alpha=0.42, label="Selected SWC stations on route")
    ax2.bar(routes + 0.16, dwc_count, width=0.32, color="#CC79A7", alpha=0.65, label="Selected DWC links on route")
    ax2.set_ylabel("Unique selected facilities encountered")
    handles, labels = ax.get_legend_handles_labels()
    handles2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(handles + handles2, labels + labels2, ncol=2, frameon=False, loc="upper center")
    ax.set_title("Route batteries and available selected wireless facilities")
    fig.tight_layout()
    fig.savefig(OUT / "04_route_battery_and_opportunities.png", dpi=220)
    plt.close(fig)


def plot_soc_balance(wpt: dict, no_wpt: dict, data: dict) -> None:
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    for solution, ax, title in [(wpt, axes[0, 0], "With WPT"), (no_wpt, axes[0, 1], "No WPT")]:
        energy = solution["energy_events"]
        route_soc = energy.groupby("route").agg(min_arr=("soc_arr", "min"), max_dep=("soc_dep", "max")).reindex(range(len(data["route_names"])))
        route_ids = route_soc.index.to_numpy()
        ax.vlines(route_ids, route_soc["min_arr"], route_soc["max_dep"], color="#0072B2", linewidth=2)
        ax.scatter(route_ids, route_soc["min_arr"], s=12, color="#D55E00", label="Minimum arrival SoC")
        ax.scatter(route_ids, route_soc["max_dep"], s=12, color="#009E73", label="Maximum departure SoC")
        ax.axhline(data["soc_min"], color="black", linestyle="--", linewidth=0.8)
        ax.axhline(data["soc_max"], color="black", linestyle="--", linewidth=0.8)
        ax.set(xlabel="Route index", ylabel="State of charge", title=title)
    axes[0, 0].legend(frameon=False, fontsize=8)

    for solution, color in [(wpt, "#0072B2"), (no_wpt, "#D55E00")]:
        station_res = solution["energy_events"]["station_residual"].abs().clip(lower=1e-12)
        link_res = solution["link_energy"]["link_residual"].abs().clip(lower=1e-12)
        axes[1, 0].hist(np.log10(station_res), bins=45, alpha=0.55, color=color, label=solution["label"])
        axes[1, 1].hist(np.log10(link_res), bins=45, alpha=0.55, color=color, label=solution["label"])
    axes[1, 0].set(xlabel="log10 absolute residual (kWh)", ylabel="Events", title="Station energy-balance residuals")
    axes[1, 1].set(xlabel="log10 absolute residual (kWh)", ylabel="Events", title="Link energy-balance residuals")
    axes[1, 0].legend(frameon=False)
    axes[1, 1].legend(frameon=False)
    fig.tight_layout()
    fig.savefig(OUT / "05_soc_and_energy_balance.png", dpi=220)
    plt.close(fig)


def plot_duty_timing(duty: pd.DataFrame, link: pd.DataFrame) -> None:
    transfers = link.loc[link["intertrip"]]
    by_bus = transfers.groupby("bus")["window"].agg(["min", "median", "max"])
    merged = duty.set_index("bus").join(by_bus).sort_values("start")
    y = np.arange(len(merged))
    fig, axes = plt.subplots(1, 2, figsize=(12, 7))
    axes[0].hlines(y, merged["start"], merged["end"], color="#0072B2", linewidth=1.2)
    axes[0].scatter(merged["start"], y, s=7, color="#009E73")
    axes[0].scatter(merged["end"], y, s=7, color="#D55E00")
    axes[0].set(xlabel="Minute of representative day", ylabel="Duties sorted by start time", title="Duty operating spans")
    axes[1].scatter(merged["trips"], merged["max"], s=16, alpha=0.65, color="#CC79A7")
    axes[1].axhline(0, color="black", linewidth=0.8)
    axes[1].set(xlabel="Trips in duty", ylabel="Maximum inter-trip connector window (min)", title="Trip-chain timing")
    fig.tight_layout()
    fig.savefig(OUT / "06_duty_timing.png", dpi=220)
    plt.close(fig)


def plot_network_timing(station: pd.DataFrame, link: pd.DataFrame, data: dict) -> None:
    intra = link.loc[~link["intertrip"]].copy()
    intra["speed"] = 60.0 * intra["distance"] / intra["window"]
    trip_rows = []
    for trip, stops in enumerate(data["stations"]):
        edges = list(zip(stops[:-1], stops[1:]))
        trip_rows.append(
            {
                "trip": trip,
                "stops": len(stops),
                "repeated_stations": len(stops) - len(set(stops)),
                "repeated_links": len(edges) - len(set(edges)),
            }
        )
    trip_stats = pd.DataFrame(trip_rows)

    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    axes[0, 0].scatter(intra["distance"], intra["window"], s=10, alpha=0.18, color="#0072B2")
    axes[0, 0].set(xlabel="Directed-link distance (km)", ylabel="Travel window (min)", title="Distance and generated in-service time")
    axes[0, 1].hist(intra["speed"], bins=np.arange(0, 45, 2), color="#009E73", edgecolor="white")
    axes[0, 1].axvline(80, color="black", linestyle="--", linewidth=0.8)
    axes[0, 1].set(xlabel="Implied speed (km/h)", ylabel="Traversal events", title="Implied in-service speed")

    repeat_counts = pd.DataFrame(
        {
            "No repeat": [int((trip_stats["repeated_stations"] == 0).sum()), int((trip_stats["repeated_links"] == 0).sum())],
            "Has repeat": [int((trip_stats["repeated_stations"] > 0).sum()), int((trip_stats["repeated_links"] > 0).sum())],
        },
        index=["Physical station", "Directed link"],
    )
    repeat_counts.plot(kind="bar", stacked=True, ax=axes[1, 0], color=["#999999", "#D55E00"], rot=0)
    axes[1, 0].set(ylabel="Trips", title="Repeated physical elements within a trip")
    axes[1, 0].legend(frameon=False)

    zero_dwell = station.loc[station["window"] == 0]
    axes[1, 1].hist(station["window"], bins=np.arange(-0.5, 11.5, 1), color="#56B4E9", edgecolor="white")
    axes[1, 1].set(xlabel="Dwell window (min)", ylabel="Station events", title=f"Station dwell times ({len(zero_dwell)} zero-minute events)")
    fig.tight_layout()
    fig.savefig(OUT / "07_network_timing_and_repeats.png", dpi=220)
    plt.close(fig)

    trip_stats.to_csv(OUT / "trip_repeat_diagnostics.csv", index=False)
    timing_outliers = pd.concat(
        [
            station.loc[station["window"] == 0, ["bus", "route", "trip", "station", "station_idx", "arrival", "departure", "window"]].assign(event_type="zero_dwell"),
            link.loc[link["intertrip"] & (link["window"] > 120), ["bus", "route", "trip_dep", "station_dep", "trip_arr", "station_arr", "departure", "arrival", "window"]].assign(event_type="long_intertrip_connector"),
        ],
        ignore_index=True,
    )
    timing_outliers.to_csv(OUT / "timing_outliers.csv", index=False)


def plot_terminal_opportunity_accounting(
    station: pd.DataFrame, link: pd.DataFrame, data: dict
) -> None:
    transfers = link.loc[link["intertrip"]].copy()
    next_dwell = station.rename(
        columns={"trip": "trip_arr", "station": "station_arr", "station_idx": "station_idx_arr", "window": "modeled_first_stop_dwell"}
    )[["bus", "trip_arr", "station_arr", "station_idx_arr", "modeled_first_stop_dwell"]]
    transfers = transfers.merge(
        next_dwell,
        on=["bus", "trip_arr", "station_arr", "station_idx_arr"],
        validate="one_to_one",
    )
    transfers["facility_type"] = np.where(
        transfers["station_dep"].isin(data["plug_stations"]), "Plug-in", "Selected SWC"
    )
    summary = transfers.groupby(["station_dep", "facility_type"], as_index=False).agg(
        transfer_events=("window", "size"),
        excluded_connector_minutes=("window", "sum"),
        modeled_first_stop_minutes=("modeled_first_stop_dwell", "sum"),
        maximum_connector_minutes=("window", "max"),
    )
    summary = summary.sort_values("excluded_connector_minutes", ascending=False)

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    x = np.arange(len(summary))
    width = 0.38
    axes[0].bar(x - width / 2, summary["excluded_connector_minutes"], width, color="#D55E00", label="Inter-trip connector (not chargeable)")
    axes[0].bar(x + width / 2, summary["modeled_first_stop_minutes"], width, color="#0072B2", label="Next-trip first-stop dwell (chargeable)")
    labels = [f"{row.station_dep}\n{row.facility_type}" for row in summary.itertuples()]
    axes[0].set_xticks(x, labels)
    axes[0].set(ylabel="Total minutes", title="Terminal time represented as charging opportunity")
    axes[0].legend(frameon=False, fontsize=8)

    colors = np.where(transfers["facility_type"].eq("Plug-in"), "#0072B2", "#009E73")
    axes[1].scatter(
        transfers["window"],
        transfers["modeled_first_stop_dwell"],
        c=colors,
        s=13,
        alpha=0.45,
    )
    axes[1].set(
        xlabel="Inter-trip connector window (min, not chargeable)",
        ylabel="Next-trip first-stop dwell (min, chargeable)",
        title="Split of terminal layover time",
    )
    axes[1].set_xscale("symlog", linthresh=10)
    fig.tight_layout()
    fig.savefig(OUT / "08_terminal_opportunity_accounting.png", dpi=220)
    plt.close(fig)
    summary.to_csv(OUT / "terminal_opportunity_accounting.csv", index=False)


def main() -> None:
    global DATA_CACHE
    OUT.mkdir(parents=True, exist_ok=True)
    DATA_CACHE = parse_inputs()
    station, link, duty = construct_events(DATA_CACHE)
    wpt = attach_solution(
        load_solution(WPT, "subproblem_variables.csv", "With WPT"), station, link, DATA_CACHE
    )
    no_wpt = attach_solution(
        load_solution(NO_WPT, "model_variables.csv", "No WPT"), station, link, DATA_CACHE
    )

    metrics = pd.concat(
        [
            network_diagnostics(DATA_CACHE, station, link, duty),
            solution_diagnostics(wpt, DATA_CACHE),
            solution_diagnostics(no_wpt, DATA_CACHE),
        ],
        ignore_index=True,
    )
    metrics.to_csv(OUT / "diagnostic_metrics.csv", index=False)
    plot_opportunities(station, link)
    plot_charging(wpt, no_wpt, DATA_CACHE)
    plot_facility_exposure(wpt, station, link, DATA_CACHE)
    plot_routes(wpt, no_wpt, station, link, DATA_CACHE)
    plot_soc_balance(wpt, no_wpt, DATA_CACHE)
    plot_duty_timing(duty, link)
    plot_network_timing(station, link, DATA_CACHE)
    plot_terminal_opportunity_accounting(station, link, DATA_CACHE)

    print(metrics.to_string(index=False))
    print(f"\nDiagnostic outputs: {OUT}")


if __name__ == "__main__":
    main()
