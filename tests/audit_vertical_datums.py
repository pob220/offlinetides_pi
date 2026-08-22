#!/usr/bin/env python3
"""Flag station-wide height offsets which look like vertical-datum errors."""

import argparse
import json
import pathlib
import statistics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=pathlib.Path)
    parser.add_argument("--json-output", type=pathlib.Path)
    args = parser.parse_args()
    with args.results.open(encoding="utf-8") as source:
        results = json.load(source)

    audit = []
    for result in results:
        signed = [match["signed_height_error_m"] for match in result.get("matches", [])]
        if len(signed) < 3:
            continue
        bias = statistics.mean(signed)
        spread = statistics.pstdev(signed)
        range_error = result.get("mean_absolute_tidal_range_error_m")
        likely = (
            abs(bias) >= 0.20
            and spread <= 0.20
            and range_error is not None
            and range_error <= 0.25
        )
        audit.append({
            "station_id": result["station_id"],
            "mean_signed_height_error_m": bias,
            "event_error_spread_m": spread,
            "mean_absolute_tidal_range_error_m": range_error,
            "classification": "probable_vertical_datum_bias" if likely else "no_clear_datum_bias",
        })

    print("| Station | Signed height bias | Event spread | Range error | Audit |")
    print("|---|---:|---:|---:|---|")
    for item in audit:
        print(
            f"| {item['station_id']} | {item['mean_signed_height_error_m']:.3f} m "
            f"| {item['event_error_spread_m']:.3f} m "
            f"| {item['mean_absolute_tidal_range_error_m']:.3f} m "
            f"| {item['classification']} |"
        )
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        with args.json_output.open("w", encoding="utf-8") as output:
            json.dump(audit, output, indent=2)
            output.write("\n")


if __name__ == "__main__":
    main()
