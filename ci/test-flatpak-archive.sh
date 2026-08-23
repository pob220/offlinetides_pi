#!/bin/sh
set -eu

build_dir=${1:?Flatpak build directory is required}
pair=$("$(dirname "$0")/resolve-package-pair.sh" "$build_dir")
archive=$(printf '%s\n' "$pair" | sed -n '1p')
metadata=$(printf '%s\n' "$pair" | sed -n '2p')
target=$(sed -n \
  's:.*<target>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</target>.*:\1:p' \
  "$metadata")
case "$target" in
  flatpak-x86_64-wx32|flatpak-aarch64-wx32) ;;
  *) echo "Invalid OfflineTides Flatpak target: $target" >&2; exit 1 ;;
esac

tmp=${TMPDIR:-/tmp}/offlinetides-flatpak-test-$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp"
tar -xzf "$archive" -C "$tmp"
plugin=$(find "$tmp" -type f -path '*/lib/opencpn/libofflinetides_pi.so' -print -quit)
test -n "$plugin"
test -z "$(find "$tmp" -type f \( -name '*.a' -o -name '*.la' \) -print -quit)"
if readelf -d "$plugin" | grep -Eqi 'NEEDED.*(jsoncpp|sodium|zstd)'; then
  echo "Flatpak package retains an unbundled OfflineTides runtime dependency" >&2
  exit 1
fi
printf '%s\n' "OfflineTides Flatpak archive passed: $(basename "$archive")"
