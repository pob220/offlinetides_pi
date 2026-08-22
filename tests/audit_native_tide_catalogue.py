#!/usr/bin/env python3
"""Validate a v1 native OpenCPN tide-station metadata audit."""

from __future__ import annotations

import argparse
import json
import math
from collections import Counter
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("audit", type=Path)
    args = parser.parse_args()
    document = json.loads(args.audit.read_text(encoding="utf-8"))
    stations = document.get("stations", [])
    errors: list[str] = []

    if document.get("schema_version") != 1:
        errors.append("unsupported audit schema")
    if document.get("station_count") != len(stations):
        errors.append("station_count does not match records")

    indices: set[int] = set()
    stable_ids: set[str] = set()
    status_counts: Counter[int] = Counter()
    dataset_counts: Counter[str] = Counter()
    equivalence_counts: Counter[str] = Counter()
    for station in stations:
        index = station.get("index")
        stable_id = station.get("stable_id", "")
        dataset = station.get("dataset_name", "")
        dataset_id = station.get("dataset_id", "")
        dataset_version = station.get("dataset_version", "")
        datum_status = station.get("datum_status")
        equivalence = station.get("datum_equivalence_key", "UNKNOWN")
        latitude = station.get("latitude")
        longitude = station.get("longitude")

        if index in indices:
            errors.append(f"duplicate combined index {index}")
        indices.add(index)
        if not stable_id:
            errors.append(f"index {index}: empty stable id")
        elif stable_id in stable_ids:
            errors.append(f"index {index}: duplicate stable id {stable_id}")
        stable_ids.add(stable_id)
        if not dataset or not dataset_id or not dataset_version:
            errors.append(f"index {index}: incomplete dataset identity")
        if datum_status not in (0, 1, 2):
            errors.append(f"index {index}: invalid datum status")
        if station.get("subordinate") and datum_status == 1:
            errors.append(f"index {index}: subordinate datum claimed as direct")
        if equivalence != "UNKNOWN" and datum_status == 0:
            errors.append(f"index {index}: unknown datum has equivalence")
        if not isinstance(latitude, (int, float)) or not -90 <= latitude <= 90:
            errors.append(f"index {index}: invalid latitude")
        if not isinstance(longitude, (int, float)) or not -180 <= longitude <= 180:
            errors.append(f"index {index}: invalid longitude")
        datum_offset = station.get("datum_offset_m")
        if datum_offset is not None and not math.isfinite(datum_offset):
            errors.append(f"index {index}: non-finite Z0")

        status_counts[datum_status] += 1
        dataset_counts[dataset] += 1
        equivalence_counts[equivalence] += 1

    expected_summary = {
        "declared": status_counts[1],
        "inherited": status_counts[2],
        "unknown": status_counts[0],
    }
    for key, expected in expected_summary.items():
        if document.get("summary", {}).get(key) != expected:
            errors.append(f"summary {key} does not match records")
    if dict(dataset_counts) != document.get("datasets", {}):
        errors.append("dataset counts do not match records")
    if dict(equivalence_counts) != document.get("datum_equivalence", {}):
        errors.append("datum-equivalence counts do not match records")

    print(f"stations: {len(stations)}")
    print("datasets: " + ", ".join(
        f"{name}={count}" for name, count in sorted(dataset_counts.items())))
    print("datum status: " + ", ".join(
        f"{name}={count}" for name, count in (
            ("declared", status_counts[1]),
            ("inherited", status_counts[2]),
            ("unknown", status_counts[0]),
        )))
    print("equivalence: " + ", ".join(
        f"{name}={count}" for name, count in sorted(equivalence_counts.items())))
    if errors:
        for error in errors[:50]:
            print(f"ERROR: {error}")
        if len(errors) > 50:
            print(f"ERROR: {len(errors) - 50} additional errors")
        return 1
    print("native tide catalogue audit: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
