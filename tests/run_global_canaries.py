#!/usr/bin/env python3
"""Author station-only XTDs and compare ten global official canaries."""

import argparse
import json
import pathlib
import subprocess


def run(command):
    return subprocess.run(command, check=True, capture_output=True, text=True)


def metric(result, name):
    value = result.get(name)
    return "unknown" if value is None else f"{value:.3f}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--author", required=True, type=pathlib.Path)
    parser.add_argument("--validator", required=True, type=pathlib.Path)
    parser.add_argument("--harmonics", required=True, type=pathlib.Path)
    parser.add_argument("--catalogue", required=True, type=pathlib.Path)
    parser.add_argument("--reference-root", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()

    with args.catalogue.open(encoding="utf-8") as source:
        catalogue = json.load(source)
    if len(catalogue["canaries"]) != 10:
        raise SystemExit("global qualification catalogue must contain ten canaries")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    results = []
    for canary in catalogue["canaries"]:
        package = args.output_dir / f"{canary['id']}.xtd"
        engine_reference = args.output_dir / f"{canary['id']}-engine.json"
        reference = args.reference_root / canary["reference"]
        run(
            [
                str(args.author),
                str(args.harmonics),
                canary["source_prefix"],
                str(canary["latitude"]),
                str(canary["longitude"]),
                "1787184000",
                "72",
                str(package),
                str(engine_reference),
            ]
        )
        comparison = json.loads(
            run([str(args.validator), str(package), str(reference)]).stdout
        )
        comparison["region"] = canary["region"]
        comparison["source_prefix"] = canary["source_prefix"]
        results.append(comparison)

    print(
        "| Station | Region | Events | Mean time | Max time | Mean height | Mean range |"
    )
    print("|---|---|---:|---:|---:|---:|---:|")
    for result in results:
        print(
            f"| {result['station_id']} | {result['region']} "
            f"| {result['matched_events']} "
            f"| {metric(result, 'mean_absolute_time_error_minutes')} min "
            f"| {metric(result, 'maximum_absolute_time_error_minutes')} min "
            f"| {metric(result, 'mean_absolute_height_error_m')} m "
            f"| {metric(result, 'mean_absolute_tidal_range_error_m')} m |"
        )
    with (args.output_dir / "results.json").open("w", encoding="utf-8") as output:
        json.dump(results, output, indent=2)
        output.write("\n")


if __name__ == "__main__":
    main()
