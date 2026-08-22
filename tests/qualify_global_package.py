#!/usr/bin/env python3
"""Run one operational OfflineTides package against private/local canaries."""

import argparse
import json
import pathlib
import statistics
import subprocess


def load(path):
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def run_one(validator, package, reference):
    completed = subprocess.run(
        [str(validator), str(package), str(reference)],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


def number(value, suffix):
    return "unknown" if value is None else f"{value:.3f} {suffix}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--validator", required=True, type=pathlib.Path)
    parser.add_argument("--package", required=True, type=pathlib.Path)
    parser.add_argument("--reference-root", required=True, type=pathlib.Path)
    parser.add_argument("--uk-catalogue", required=True, type=pathlib.Path)
    parser.add_argument("--global-catalogue", required=True, type=pathlib.Path)
    parser.add_argument("--json-output", type=pathlib.Path)
    args = parser.parse_args()

    results = []
    for group, catalogue_path in (
        ("UK authoritative sources", args.uk_catalogue),
        ("international", args.global_catalogue),
    ):
        for canary in load(catalogue_path)["canaries"]:
            result = run_one(
                args.validator,
                args.package,
                args.reference_root / canary["reference"],
            )
            result["qualification_group"] = group
            result["canary_id"] = canary["id"]
            results.append(result)

    if any(item["model_datum_id"] != "chart-datum" for item in results):
        raise SystemExit("at least one canary lacks the Chart Datum transform")
    if any(item["matched_events"] != item["reference_events"] for item in results):
        raise SystemExit("at least one canary did not match every reference event")

    print("| Set | Station | Events | Mean time | Mean height | Mean range |")
    print("|---|---|---:|---:|---:|---:|")
    for item in results:
        print(
            f"| {item['qualification_group']} | {item['station_id']} "
            f"| {item['matched_events']}/{item['reference_events']} "
            f"| {number(item['mean_absolute_time_error_minutes'], 'min')} "
            f"| {number(item['mean_absolute_height_error_m'], 'm')} "
            f"| {number(item['mean_absolute_tidal_range_error_m'], 'm')} |"
        )
    print()
    print("Station-weighted group means:")
    for group in ("UK authoritative sources", "international"):
        selected = [item for item in results if item["qualification_group"] == group]
        print(
            f"- {group}: {statistics.mean(item['mean_absolute_time_error_minutes'] for item in selected):.3f} min; "
            f"{statistics.mean(item['mean_absolute_height_error_m'] for item in selected):.3f} m height; "
            f"{statistics.mean(item['mean_absolute_tidal_range_error_m'] for item in selected):.3f} m range"
        )

    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        with args.json_output.open("w", encoding="utf-8") as output:
            json.dump(results, output, indent=2)
            output.write("\n")


if __name__ == "__main__":
    main()
