#!/usr/bin/env python3

import json
import pathlib
import sys


fixture = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
assert fixture["target_standard"] == {
    "product": "IHO S-104 Water Level Information for Surface Navigation",
    "edition": "2.0.0",
    "s100_edition": "5.2.0",
}
forecast = fixture["xtidal_forecast"]
assert forecast["schema"] == "xtidal-water-level-forecast"
assert forecast["schema_version"] == 1
assert forecast["geometry"]["coordinate_reference_system"] == "WGS84"
assert forecast["generated_utc"].endswith("Z")
assert all(sample["valid_time_utc"].endswith("Z") for sample in forecast["samples"])
assert forecast["water_level_unit"] == "m"
assert set(forecast["vertical_datum"]) == {"identifier", "name", "epoch"}
assert set(forecast["source"]) == {"id", "name"}
assert {sample["state"] for sample in forecast["samples"]} == {"known", "unknown", "masked"}
assert all(sample["water_level_m"] is None for sample in forecast["samples"] if sample["state"] != "known")
assert fixture["is_s104_encoded_product"] is False
