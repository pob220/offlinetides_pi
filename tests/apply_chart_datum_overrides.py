#!/usr/bin/env python3
"""Create a reproducible authoring catalogue with authority datum overrides."""

import argparse
import hashlib
import json
import math
import pathlib


def load(path):
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def distance_km(first, second):
    radius_km = 6371.0088
    lat1, lat2 = map(math.radians, (first["latitude"], second["latitude"]))
    dlat = lat2 - lat1
    dlon = math.radians(second["longitude"] - first["longitude"])
    value = math.sin(dlat / 2) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2) ** 2
    return radius_km * 2 * math.atan2(math.sqrt(value), math.sqrt(1 - value))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base", type=pathlib.Path)
    parser.add_argument("overrides", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    base = load(args.base)
    overrides = load(args.overrides)
    if base.get("schema") != "xtidal-chart-datum-stations" or base.get("schema_version") != 1:
        raise SystemExit("invalid base Chart Datum catalogue")
    if overrides.get("schema") != "xtidal-chart-datum-authority-overrides" or overrides.get("schema_version") != 1:
        raise SystemExit("invalid authority override catalogue")

    stations = list(base["stations"])
    removals = []
    next_record = max(item.get("record", 0) for item in stations) + 1
    for authority in overrides["overrides"]:
        radius = float(authority["exclusion_radius_km"])
        removed = [item for item in stations if distance_km(item, authority) <= radius]
        stations = [item for item in stations if distance_km(item, authority) > radius]
        removals.append({
            "override_id": authority["id"],
            "removed_records": [item.get("record") for item in removed],
            "removed_count": len(removed),
        })
        stations.append({
            "record": next_record,
            "name": authority["name"],
            "latitude": authority["latitude"],
            "longitude": authority["longitude"],
            "msl_above_chart_datum_m": authority["msl_above_chart_datum_m"],
            "source_datum": authority["source_datum"],
            "country": authority["country"],
            "source": authority["source"],
            "restriction": "Authority value used with explicit authoring permission",
            "authoring_permission": True,
            "confidence": authority["confidence"],
            "authority_override_id": authority["id"],
        })
        next_record += 1

    output = {
        "schema": "xtidal-chart-datum-stations",
        "schema_version": 1,
        "source_provenance": {
            "method": "base catalogue with spatially exclusive authority overrides",
            "base_file": args.base.name,
            "base_sha256": sha256(args.base),
            "override_file": args.overrides.name,
            "override_sha256": sha256(args.overrides),
            "override_sources": [
                {
                    "id": item["id"],
                    "source": item["source"],
                    "source_url": item["source_url"],
                    "source_title": item["source_title"],
                    "evidence": item["evidence"],
                }
                for item in overrides["overrides"]
            ],
            "replacement_audit": removals,
        },
        "stations": stations,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as destination:
        json.dump(output, destination, indent=2, ensure_ascii=False)
        destination.write("\n")


if __name__ == "__main__":
    main()
