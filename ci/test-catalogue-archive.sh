#!/usr/bin/env bash
set -euo pipefail

build_dir=${1:?package build directory is required}
stage_dir=${2:?stage directory is required}
source_dir=${3:?source directory is required}
mapfile -t pair < <("$(dirname "$0")/resolve-package-pair.sh" "$build_dir")
test "${#pair[@]}" -eq 2
archive=${pair[0]}
metadata=${pair[1]}

listing=$(mktemp)
strings_file=$(mktemp)
trap 'rm -f "$listing" "$strings_file"' EXIT HUP INT TERM
tar -tzf "$archive" >"$listing"
test "$(grep -c '/lib/opencpn/libofflinetides_pi.so$' "$listing")" -eq 1
if grep -Eqi '(TPXO|TICON|\.xtdt?$|\.nc$|\.grd$|libnetcdf|libhdf5)' "$listing"; then
  echo "Runtime archive contains an operational dataset or authoring material" >&2
  exit 1
fi

mapfile -t staged_plugins < <(
  find "$stage_dir" -type f -path '*/lib/opencpn/libofflinetides_pi.so' | sort
)
test "${#staged_plugins[@]}" -eq 1
plugin=${staged_plugins[0]}
if readelf -d "$plugin" | grep -Eqi 'NEEDED.*(netcdf|hdf5)'; then
  echo "Runtime plugin unexpectedly links an authoring-data library" >&2
  exit 1
fi
strings "$plugin" >"$strings_file"
if awk -v prefix="${source_dir%/}/" \
  'index($0, prefix) == 1 { found = 1 } END { exit !found }' "$strings_file";
then
  echo "Runtime plugin contains a developer source path" >&2
  exit 1
fi

grep -q '<name> OfflineTides </name>' "$metadata"
grep -q '<api-version> 1.21 </api-version>' "$metadata"
grep -q '<source> https://github.com/pob220/offlinetides_pi </source>' "$metadata"
grep -q '<tarball-url>' "$metadata"
if grep -q 'pob220/offlinetides-alpha' "$metadata"; then
  echo "Generated metadata must retain publication placeholders" >&2
  exit 1
fi
printf '%s\n' "OfflineTides catalogue/runtime boundary passed: $(basename "$archive")"
