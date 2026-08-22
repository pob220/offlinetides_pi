#!/usr/bin/env python3

import json
import pathlib
import subprocess
import sys
import tempfile


script, fixture, overrides = map(pathlib.Path, sys.argv[1:])
with tempfile.TemporaryDirectory() as directory:
    output = pathlib.Path(directory) / "merged.json"
    subprocess.run([sys.executable, script, fixture, overrides, output], check=True)
    merged = json.loads(output.read_text(encoding="utf-8"))
    authority = [item for item in merged["stations"] if item.get("authority_override_id")]
    assert len(authority) == 5
    assert {item["authority_override_id"] for item in authority} == {
        "au-darwin-authority-control",
        "nz-auckland-linz-2026",
        "nz-wellington-linz-2026",
        "br-rio-ilha-fiscal-chm-2026",
        "is-reykjavik-icg-2026",
    }
    assert merged["source_provenance"]["replacement_audit"]
