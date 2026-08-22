#!/usr/bin/env python3
"""Normalize TICON-3 into an authoring-only X-Tidal station catalogue."""

import argparse
import csv
import datetime as dt
import hashlib
import io
import json
import math
import pathlib
import zipfile


TICON3_CONSTITUENTS = {
    "2n2", "2q1", "ep2", "j1", "k1", "k2", "l2", "lm2", "m1", "m2",
    "m3", "m4", "m6", "m8", "ma2", "mb2", "mf", "mi2", "mks", "mm",
    "mn4", "ms4", "msf", "msq", "mtm", "n2", "n4", "ni2", "o1", "oo1",
    "p1", "q1", "r2", "s1", "s2", "s3", "s4", "sa", "ssa", "t2",
}

# TICON-3 uses IHO display labels for several constituents.  Normalize these
# at the authoring boundary so an operational XTD contains the canonical names
# understood by the predictor, rather than silently losing valid coefficients.
TICON3_CANONICAL_NAMES = {
    "ep2": "eps2",
    "lm2": "lda2",
    "mi2": "mu2",
    "mks": "mks2",
    "ni2": "nu2",
}


def source_bytes(path):
    raw = path.read_bytes()
    if zipfile.is_zipfile(io.BytesIO(raw)):
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            raw = archive.read("TICON_3.txt")
    return raw


def longitude_wgs84(value):
    return value - 360.0 if value > 180.0 else value


def station_id(key):
    digest = hashlib.sha256("\x1f".join(map(str, key)).encode()).hexdigest()
    return "ticon3-" + digest[:16]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--west", type=float)
    parser.add_argument("--south", type=float)
    parser.add_argument("--east", type=float)
    parser.add_argument("--north", type=float)
    parser.add_argument("--margin-deg", type=float, default=1.0)
    parser.add_argument(
        "--gauge-type", action="append", choices=("Coastal", "River", "Lake")
    )
    args = parser.parse_args()

    bounds = (args.west, args.south, args.east, args.north)
    if any(value is not None for value in bounds) and not all(
        value is not None for value in bounds
    ):
        parser.error("--west, --south, --east and --north must be used together")
    bounded = all(value is not None for value in bounds)

    accepted_types = set(args.gauge_type or ("Coastal", "River"))
    raw = source_bytes(args.input)
    groups = {}
    rows_seen = 0
    rows_selected = 0
    rows_missing_coefficients = 0
    reader = csv.reader(io.StringIO(raw.decode("utf-8-sig")), delimiter="\t")
    for row in reader:
        rows_seen += 1
        if len(row) != 14:
            raise SystemExit(f"TICON-3 row {rows_seen} has {len(row)} columns")
        row = [value.strip() for value in row]
        latitude = float(row[0])
        longitude = longitude_wgs84(float(row[1]))
        gauge_type = row[13]
        if gauge_type not in accepted_types:
            continue
        if bounded and not (
            args.west - args.margin_deg <= longitude <= args.east + args.margin_deg
            and args.south - args.margin_deg <= latitude <= args.north + args.margin_deg
        ):
            continue
        source_name = row[2].lower()
        if source_name not in TICON3_CONSTITUENTS:
            continue
        name = TICON3_CANONICAL_NAMES.get(source_name, source_name)
        if not all(row[index] for index in (3, 4, 5, 6)):
            rows_missing_coefficients += 1
            continue
        key = (
            round(latitude, 7),
            round(longitude, 7),
            row[12],
            row[10],
            row[11],
            gauge_type,
        )
        station = groups.setdefault(
            key,
            {
                "id": station_id(key),
                "latitude": latitude,
                "longitude": longitude,
                "gauge_type": gauge_type.lower(),
                "source_code": row[12],
                "observation_start": row[10],
                "observation_end": row[11],
                "observation_count": int(row[8]),
                "maximum_gap_days": float(row[9]),
                "missing_percent": float(row[7]),
                "constituents": {},
            },
        )
        amplitude_m = float(row[3]) / 100.0
        phase_degrees = float(row[4])
        phase_radians = math.radians(phase_degrees)
        amplitude_std_m = float(row[5]) / 100.0
        phase_std_degrees = float(row[6])
        phase_std_radians = math.radians(phase_std_degrees)
        station["constituents"][name] = {
            # TICON uses Greenwich phase lag G in A*cos(V+u-G).  The XTD
            # ATLAS convention predicts Re(z)*cos(theta)-Im(z)*sin(theta),
            # hence z=A*exp(-iG).
            "real_m": amplitude_m * math.cos(phase_radians),
            "imaginary_m": -amplitude_m * math.sin(phase_radians),
            "amplitude_std_m": amplitude_std_m,
            "phase_std_degrees": phase_std_degrees,
            "complex_std_m": math.hypot(
                amplitude_std_m, amplitude_m * phase_std_radians
            ),
        }
        rows_selected += 1

    stations = sorted(
        groups.values(), key=lambda station: (station["latitude"], station["longitude"], station["id"])
    )
    for station in stations:
        station["constituents"] = dict(sorted(station["constituents"].items()))
        station["quality"] = {
            "valid_fraction": 1.0 - station["missing_percent"] / 100.0,
            "supported_constituent_count": len(station["constituents"]),
        }
    catalogue = {
        "schema": "xtidal-ticon3-authoring-catalogue",
        "schema_version": 1,
        "created_utc": dt.datetime.now(dt.timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "source": {
            "title": "TICON-3: Tidal Constants based on GESLA-3 sea-level records",
            "doi": "10.1594/PANGAEA.951610",
            "license": "CC-BY-4.0",
            "input_file": args.input.name,
            "input_sha256": hashlib.sha256(args.input.read_bytes()).hexdigest(),
            "phase_convention": "Greenwich phase lag",
            "amplitude_units": "metres",
            "role": "offline authoring only",
        },
        "filter": {
            "bbox_wgs84": (
                {
                    "west": args.west,
                    "south": args.south,
                    "east": args.east,
                    "north": args.north,
                }
                if bounded
                else None
            ),
            "margin_degrees": args.margin_deg,
            "gauge_types": sorted(accepted_types),
            "constituents": sorted(TICON3_CONSTITUENTS),
        },
        "statistics": {
            "input_rows": rows_seen,
            "selected_rows": rows_selected,
            "selected_stations": len(stations),
            "rows_missing_coefficients": rows_missing_coefficients,
        },
        "stations": stations,
    }
    if not stations:
        raise SystemExit("TICON-3 selection contains no stations")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(catalogue, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
