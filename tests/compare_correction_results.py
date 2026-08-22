#!/usr/bin/env python3
"""Run reproducible before/after X-Tidal correction qualification."""

import argparse
import json
import pathlib
import subprocess


def validate(executable, package, reference):
    result = subprocess.run(
        [str(executable), str(package), str(reference)],
        check=False,
        capture_output=True,
        text=True,
    )
    if not result.stdout:
        with open(reference, encoding="utf-8") as source:
            manifest = json.load(source)
        return {
            "station_id": manifest["station"]["id"],
            "matched_events": 0,
            "error": result.stderr.strip() or "validator returned no result",
        }
    return json.loads(result.stdout)


def number(result, field):
    value = result.get(field)
    return "unknown" if not result.get("matched_events") or value is None else f"{value:.3f}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--validator", required=True, type=pathlib.Path)
    parser.add_argument("--before", required=True, type=pathlib.Path)
    parser.add_argument("--after", required=True, type=pathlib.Path)
    parser.add_argument("--candidate", type=pathlib.Path)
    parser.add_argument("references", nargs="+", type=pathlib.Path)
    args = parser.parse_args()

    if args.candidate:
        print("| Station | Time base | Time v1 | Time v2 | Range base | Range v1 | Range v2 | Height v1 | Height v2 |")
        print("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    else:
        print("| Station | Time before | Time after | Range before | Range after | Height after |")
        print("|---|---:|---:|---:|---:|---:|")
    for reference in args.references:
        before = validate(args.validator, args.before, reference)
        after = validate(args.validator, args.after, reference)
        if args.candidate:
            candidate = validate(args.validator, args.candidate, reference)
            print(
                f"| {after['station_id']} "
                f"| {number(before, 'mean_absolute_time_error_minutes')} min "
                f"| {number(after, 'mean_absolute_time_error_minutes')} min "
                f"| {number(candidate, 'mean_absolute_time_error_minutes')} min "
                f"| {number(before, 'mean_absolute_tidal_range_error_m')} m "
                f"| {number(after, 'mean_absolute_tidal_range_error_m')} m "
                f"| {number(candidate, 'mean_absolute_tidal_range_error_m')} m "
                f"| {number(after, 'mean_absolute_height_error_m')} m "
                f"| {number(candidate, 'mean_absolute_height_error_m')} m |"
            )
        else:
            print(
                f"| {after['station_id']} "
                f"| {number(before, 'mean_absolute_time_error_minutes')} min "
                f"| {number(after, 'mean_absolute_time_error_minutes')} min "
                f"| {number(before, 'mean_absolute_tidal_range_error_m')} m "
                f"| {number(after, 'mean_absolute_tidal_range_error_m')} m "
                f"| {number(after, 'mean_absolute_height_error_m')} m |"
            )


if __name__ == "__main__":
    main()
