#!/usr/bin/env python3
"""Measure disagreement between two published HW/LW reference samples."""

import argparse
import datetime as dt
import json
import pathlib
import statistics


def load(path):
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def utc_seconds(event):
    if "unix_seconds" in event:
        return int(event["unix_seconds"])
    return int(
        dt.datetime.fromisoformat(event["utc"].replace("Z", "+00:00")).timestamp()
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("first", type=pathlib.Path)
    parser.add_argument("second", type=pathlib.Path)
    args = parser.parse_args()
    first = load(args.first)
    second = load(args.second)

    if first["station"]["id"] != second["station"]["id"]:
        raise SystemExit("references are for different stations")
    first_datum = first["reference"]["vertical_datum_id"]
    second_datum = second["reference"]["vertical_datum_id"]
    height_values_comparable = first_datum == second_datum

    second_events = list(second["events"])
    matches = []
    for event in first["events"]:
        candidates = [item for item in second_events if item["type"] == event["type"]]
        if not candidates:
            continue
        match = min(candidates, key=lambda item: abs(utc_seconds(item) - utc_seconds(event)))
        if abs(utc_seconds(match) - utc_seconds(event)) > 3 * 3600:
            continue
        second_events.remove(match)
        item = {
                "type": event["type"],
                "first_utc": event["utc"],
                "second_utc": match["utc"],
                "first_height_m": float(event["height_m"]),
                "second_height_m": float(match["height_m"]),
                "absolute_time_difference_minutes": abs(
                    utc_seconds(match) - utc_seconds(event)
                )
                / 60.0,
                "absolute_height_difference_m": None,
            }
        if height_values_comparable:
            item["absolute_height_difference_m"] = abs(
                item["second_height_m"] - item["first_height_m"]
            )
        matches.append(item)

    if not matches:
        raise SystemExit("references have no comparable events")
    range_differences = []
    for before, after in zip(matches, matches[1:]):
        if before["type"] == after["type"]:
            continue
        first_range = abs(after["first_height_m"] - before["first_height_m"])
        second_range = abs(after["second_height_m"] - before["second_height_m"])
        range_differences.append(abs(second_range - first_range))
    comparable_heights = [
        item["absolute_height_difference_m"]
        for item in matches
        if item["absolute_height_difference_m"] is not None
    ]
    result = {
        "station_id": first["station"]["id"],
        "first_vertical_datum_id": first_datum,
        "second_vertical_datum_id": second_datum,
        "height_values_comparable": height_values_comparable,
        "first_publisher": first["reference"]["publisher"],
        "second_publisher": second["reference"]["publisher"],
        "matched_events": len(matches),
        "mean_absolute_time_difference_minutes": statistics.fmean(
            item["absolute_time_difference_minutes"] for item in matches
        ),
        "maximum_absolute_time_difference_minutes": max(
            item["absolute_time_difference_minutes"] for item in matches
        ),
        "mean_absolute_height_difference_m": (
            statistics.fmean(comparable_heights) if comparable_heights else None
        ),
        "maximum_absolute_height_difference_m": (
            max(comparable_heights) if comparable_heights else None
        ),
        "tidal_range_comparisons": len(range_differences),
        "mean_absolute_tidal_range_difference_m": (
            statistics.fmean(range_differences) if range_differences else None
        ),
        "maximum_absolute_tidal_range_difference_m": (
            max(range_differences) if range_differences else None
        ),
        "matches": matches,
    }
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
