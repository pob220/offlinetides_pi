#!/usr/bin/env python3
"""Compare one or more global .xtdt packages against frozen canaries."""

import argparse
import json
import pathlib
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--validator", required=True, type=pathlib.Path)
    parser.add_argument("--catalogue", required=True, type=pathlib.Path)
    parser.add_argument("--reference-root", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--package", action="append", nargs=2,
                        metavar=("LABEL", "PATH"), required=True)
    args = parser.parse_args()
    catalogue = json.loads(args.catalogue.read_text(encoding="utf-8"))
    results = []
    for label, package_name in args.package:
        package = pathlib.Path(package_name)
        for canary in catalogue["canaries"]:
            reference = args.reference_root / canary["reference"]
            completed = subprocess.run(
                [str(args.validator), str(package), str(reference)],
                check=False, capture_output=True, text=True)
            if completed.returncode:
                comparison = {
                    "station_id": canary["id"],
                    "error": completed.stderr.strip() or
                             f"validator exit {completed.returncode}",
                }
            else:
                comparison = json.loads(completed.stdout)
            comparison["candidate"] = label
            comparison["region"] = canary.get("region", "UK")
            results.append(comparison)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, indent=2) + "\n",
                           encoding="utf-8")
    print("| Candidate | Station | Mean time | Max time | Mean range |")
    print("|---|---|---:|---:|---:|")
    for result in results:
        if "error" in result:
            print(f"| {result['candidate']} | {result['station_id']} | "
                  f"unknown | unknown | unknown |")
            continue
        print(
            f"| {result['candidate']} | {result['station_id']} | "
            f"{result['mean_absolute_time_error_minutes']:.1f} min | "
            f"{result['maximum_absolute_time_error_minutes']:.1f} min | "
            f"{result['mean_absolute_tidal_range_error_m']:.3f} m |"
        )


if __name__ == "__main__":
    main()
