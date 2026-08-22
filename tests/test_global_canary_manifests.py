#!/usr/bin/env python3
"""Structural checks for the frozen worldwide authoritative canaries."""

import json
import pathlib
import sys


def main():
    catalogue_path = pathlib.Path(sys.argv[1])
    reference_root = pathlib.Path(sys.argv[2])
    with catalogue_path.open(encoding="utf-8") as source:
        catalogue = json.load(source)
    canaries = catalogue["canaries"]
    assert catalogue["schema_version"] == 1
    assert len(canaries) == 10
    assert len({item["id"] for item in canaries}) == 10
    assert len({item["region"] for item in canaries}) >= 8

    publishers = set()
    for canary in canaries:
        with (reference_root / canary["reference"]).open(encoding="utf-8") as source:
            reference = json.load(source)
        assert reference["station"]["id"] == canary["id"]
        assert reference["reference"]["vertical_datum_id"] == "chart-datum"
        assert reference["reference"]["source_url"].startswith("https://")
        assert reference["reference"]["comparison_mode"] == (
            "event_time_and_tidal_range"
        )
        assert len(reference["events"]) >= 2
        assert all(event["type"] in ("high", "low") for event in reference["events"])
        assert all(event["utc"].endswith("Z") for event in reference["events"])
        publishers.add(reference["reference"]["publisher"])
    assert len(publishers) >= 8


if __name__ == "__main__":
    main()
