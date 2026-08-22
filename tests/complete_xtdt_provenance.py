#!/usr/bin/env python3
"""Complete a composite .xtdt.prv without changing the operational package."""

import argparse
import hashlib
import json
import os
import pathlib
import tempfile


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("sidecar", type=pathlib.Path)
    parser.add_argument("backgrounds", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    document = json.loads(args.sidecar.read_text(encoding="utf-8"))
    package = pathlib.Path(str(args.sidecar).removesuffix(".prv"))
    if sha256(package) != document["xtdt_sha256"]:
        raise SystemExit("candidate hash does not match its provenance")
    members = document.get("background_members", [])
    by_name = {path.name: path for path in args.backgrounds}
    if len(members) != len(by_name):
        raise SystemExit("background list does not match composite provenance")
    for member in members:
        source = by_name.get(member["file"])
        if source is None or sha256(source) != member["sha256"]:
            raise SystemExit(f"background hash mismatch: {member['file']}")
        source_sidecar = pathlib.Path(str(source) + ".prv")
        nested = json.loads(source_sidecar.read_text(encoding="utf-8"))
        if nested.get("xtdt_sha256") != member["sha256"]:
            raise SystemExit(f"nested provenance mismatch: {member['file']}")
        member["provenance"] = nested
    document["provenance_completeness"] = {
        "background_sidecars_embedded": True,
        "background_count": len(members),
        "operational_package_contains_provenance": False,
    }
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=args.sidecar.name + ".", dir=args.sidecar.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(document, output, indent=2)
            output.write("\n")
        os.replace(temporary_name, args.sidecar)
    except BaseException:
        pathlib.Path(temporary_name).unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    main()
