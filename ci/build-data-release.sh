#!/usr/bin/env bash
# Build a reproducible public bundle from an already-authored OfflineTides
# package after checking its privately retained adjacent provenance sidecar.
set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: $0 PACKAGE.xtdt PACKAGE.xtdt.prv OUTPUT_DIRECTORY" >&2
  exit 2
fi

package=$1
provenance=$2
output_dir=$3
version=1.0.0-alpha1
bundle_name="offlinetides-global-data-${version}"

case "$package" in
  *.xtdt) ;;
  *) echo "Operational package must use .xtdt" >&2; exit 2 ;;
esac
test "$provenance" = "${package}.prv" || {
  echo "Provenance must be the exact adjacent <name>.xtdt.prv sidecar" >&2
  exit 2
}
test -f "$package"
test -f "$provenance"

package_sha=$(sha256sum "$package" | awk '{print $1}')
package_bytes=$(stat -c '%s' "$package")

python3 - "$package" "$provenance" "$package_sha" <<'PY'
import json
import pathlib
import sys

package = pathlib.Path(sys.argv[1])
sidecar = pathlib.Path(sys.argv[2])
package_sha = sys.argv[3]
document = json.loads(sidecar.read_text(encoding="utf-8"))
if document.get("schema") != "xtdt-provenance":
    raise SystemExit("unexpected provenance schema")
if document.get("xtdt_file") != package.name:
    raise SystemExit("provenance filename does not match package")
if document.get("xtdt_sha256") != package_sha:
    raise SystemExit("provenance hash does not match package")
distribution = document.get("distribution", {})
for field in (
    "contains_raw_tpxo",
    "contains_raw_ticon3",
    "contains_raw_eot20",
    "contains_raw_hamtide",
    "contains_raw_gtsm",
    "contains_raw_fes",
    "contains_raw_source_models",
    "runtime_requires_tpxo",
    "runtime_requires_ticon3",
    "runtime_requires_raw_source_models",
):
    if distribution.get(field) is not False:
        raise SystemExit(f"release boundary is not explicit for {field}")
PY

mkdir -p "$output_dir"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
release_dir="$stage/$bundle_name"
mkdir -p "$release_dir"
cp "$package" "$release_dir/"

python3 - "$release_dir/manifest.json" "$(basename "$package")" \
  "$package_sha" "$package_bytes" <<'PY'
import json
import pathlib
import sys

manifest = {
    "schema": "offlinetides-data-release",
    "schema_version": 1,
    "release": "1.0.0-alpha1",
    "operational_file": {
        "name": sys.argv[2],
        "sha256": sys.argv[3],
        "bytes": int(sys.argv[4]),
    },
    "authoring_record": "retained privately; not distributed or required at runtime",
    "runtime_requires_raw_source_models": False,
}
pathlib.Path(sys.argv[1]).write_text(
    json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
)
PY

(
  cd "$release_dir"
  sha256sum "$(basename "$package")" manifest.json >SHA256SUMS
)

printf '%s\n' \
  'OfflineTides global data 1.0.0-alpha1' \
  '' \
  'Extract this directory, open OfflineTides in OpenCPN, select' \
  '"Open .xtdt package...", and choose the included .xtdt file.' \
  '' \
  '.xtd and .xtdt are independently designed, self-contained tidal-harmonic' \
  'formats. This release contains only authenticated, quantised derived' \
  'coefficients and supporting quality/datum fields which the author is' \
  'permitted to distribute. It contains and requires no raw third-party source' \
  'dataset. OfflineTides requires no access to such datasets at runtime.' \
  '' \
  'The detailed authoring record is retained privately. It is not included in' \
  'this archive and is not required by OfflineTides.' \
  '' \
  'This Alpha is an astronomical secondary prediction aid. It does not model' \
  'weather surge, river flow or live sea-level anomaly and is not a replacement' \
  'for authoritative local publications, notices or observations.' \
  >"$release_dir/README.txt"

archive="$output_dir/${bundle_name}.tar.gz"
tar --sort=name --mtime='UTC 2026-08-22' --owner=0 --group=0 \
  --numeric-owner -czf "$archive" -C "$stage" "$bundle_name"
sha256sum "$archive" >"${archive}.sha256"

tar -tzf "$archive"
echo "Created $archive"
