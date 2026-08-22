#!/usr/bin/env python3
"""Contract test for the authoring-only TICON-3 importer."""

import json
import math
import pathlib
import subprocess
import sys
import tempfile


def main():
    importer = pathlib.Path(sys.argv[1])
    fixture = pathlib.Path(sys.argv[2])
    with tempfile.TemporaryDirectory() as directory:
        output = pathlib.Path(directory) / "catalogue.json"
        subprocess.run(
            [
                sys.executable,
                str(importer),
                "--input",
                str(fixture),
                "--output",
                str(output),
                "--west",
                "-2",
                "--south",
                "50",
                "--east",
                "0",
                "--north",
                "52",
                "--margin-deg",
                "0",
            ],
            check=True,
        )
        catalogue = json.loads(output.read_text(encoding="utf-8"))
    assert catalogue["schema"] == "xtidal-ticon3-authoring-catalogue"
    assert catalogue["source"]["license"] == "CC-BY-4.0"
    assert catalogue["source"]["role"] == "offline authoring only"
    assert catalogue["statistics"]["selected_stations"] == 2
    coastal = next(
        station for station in catalogue["stations"] if station["gauge_type"] == "coastal"
    )
    assert coastal["longitude"] == -1.0
    assert math.isclose(coastal["constituents"]["m2"]["real_m"], 1.0)
    assert math.isclose(coastal["constituents"]["m2"]["imaginary_m"], 0.0)
    assert abs(coastal["constituents"]["s2"]["real_m"]) < 1e-12
    assert math.isclose(coastal["constituents"]["s2"]["imaginary_m"], -0.5)
    assert set(coastal["constituents"]) == {
        "eps2", "lda2", "m2", "mks2", "mu2", "nu2", "s2"
    }
    assert math.isclose(coastal["constituents"]["eps2"]["real_m"], -0.2)
    assert coastal["quality"]["supported_constituent_count"] == 7


if __name__ == "__main__":
    main()
