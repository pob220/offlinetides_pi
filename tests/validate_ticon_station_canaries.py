#!/usr/bin/env python3
"""Validate nearest TICON-3 gauge constants without spatial assimilation."""

import argparse
import json
import math
import pathlib
import subprocess


def distance_km(a, b):
    lat_a, lat_b = map(math.radians, (a["latitude"], b["latitude"]))
    delta_lat = lat_a - lat_b
    delta_lon = math.radians(a["longitude"] - b["longitude"])
    value = math.sin(delta_lat / 2) ** 2 + math.cos(lat_a) * math.cos(
        lat_b) * math.sin(delta_lon / 2) ** 2
    return 12742.0 * math.asin(math.sqrt(value))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--author", required=True, type=pathlib.Path)
    parser.add_argument("--validator", required=True, type=pathlib.Path)
    parser.add_argument("--ticon", required=True, type=pathlib.Path)
    parser.add_argument("--canaries", required=True, type=pathlib.Path)
    parser.add_argument("--reference-root", required=True, type=pathlib.Path)
    parser.add_argument("--output-dir", required=True, type=pathlib.Path)
    parser.add_argument("--constituent-profile", choices=("background15",))
    args = parser.parse_args()
    stations = json.loads(args.ticon.read_text(encoding="utf-8"))["stations"]
    canaries = json.loads(args.canaries.read_text(encoding="utf-8"))["canaries"]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    results = []
    for canary in canaries:
        station = min(stations, key=lambda item: distance_km(canary, item))
        distance = distance_km(canary, station)
        package = args.output_dir / f"{canary['id']}.xtdt"
        author_command = [str(args.author), str(args.ticon), station["id"],
                          str(canary["latitude"]), str(canary["longitude"]),
                          str(package)]
        if args.constituent_profile:
            author_command.append(args.constituent_profile)
        subprocess.run(
            author_command,
            check=True, capture_output=True, text=True)
        completed = subprocess.run(
            [str(args.validator), str(package),
             str(args.reference_root / canary["reference"])],
            check=False, capture_output=True, text=True)
        comparison = json.loads(completed.stdout)
        comparison["ticon_station_id"] = station["id"]
        comparison["ticon_station_distance_km"] = distance
        results.append(comparison)
    (args.output_dir / "results.json").write_text(
        json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print("| Station | TICON distance | Mean time | Max time | Mean range |")
    print("|---|---:|---:|---:|---:|")
    for result in results:
        print(
            f"| {result['station_id']} | "
            f"{result['ticon_station_distance_km']:.1f} km | "
            f"{result['mean_absolute_time_error_minutes']:.1f} min | "
            f"{result['maximum_absolute_time_error_minutes']:.1f} min | "
            f"{result['mean_absolute_tidal_range_error_m']:.3f} m |"
        )


if __name__ == "__main__":
    main()
