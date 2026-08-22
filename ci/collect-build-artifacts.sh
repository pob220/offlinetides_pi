#!/usr/bin/env bash
set -euo pipefail

if (( $# != 3 )); then
  echo "usage: $0 BUILD_DIRECTORY TARGET ARTIFACT_DIRECTORY" >&2
  exit 2
fi

readonly build_dir="$(cd "$1" && pwd)"
readonly target="$2"
readonly artifact_dir="$3"
readonly package_dir="${artifact_dir}/package"
mapfile -t pair < <("$(dirname "$0")/resolve-package-pair.sh" "$build_dir")
if (( ${#pair[@]} != 2 )); then
  echo "Package resolver returned an invalid archive/metadata pair" >&2
  exit 1
fi

readonly archive_source="${pair[0]}"
readonly metadata_source="${pair[1]}"
plugin_version=$(sed -n \
  's:.*<version>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</version>.*:\1:p' \
  "$metadata_source")
if [[ ! "$plugin_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Invalid or missing plugin version in $metadata_source" >&2
  exit 1
fi

mkdir -p "$package_dir"
find "$package_dir" -maxdepth 1 -type f \
  \( -name 'offlinetides_pi-*' -o -name checksums.txt \) -delete
cp -f "$archive_source" "$metadata_source" "$package_dir/"
(cd "$package_dir" && find . -maxdepth 1 -type f ! -name checksums.txt \
  -print0 | sort -z | xargs -0 sha256sum >checksums.txt)

readonly archive="$package_dir/$(basename "$archive_source")"
os_name=$(uname -s)
if [[ -r /etc/os-release ]]; then
  os_name=$(sed -n 's/^PRETTY_NAME=//p' /etc/os-release | head -1 | tr -d '"')
fi
commit=${CIRCLE_SHA1:-}
if [[ -z "$commit" ]]; then
  commit=$(git -C "${OFFLINETIDES_SOURCE_DIR:-${CIRCLE_WORKING_DIRECTORY:-.}}" \
    rev-parse HEAD 2>/dev/null || printf unknown)
fi

jq -n \
  --arg schema "offlinetides-target-result-v1" \
  --arg commit "$commit" \
  --arg version "$plugin_version" \
  --arg target "$target" \
  --arg os "$os_name" \
  --arg architecture "$(uname -m)" \
  --arg compiler "$(c++ --version | head -1)" \
  --arg cmake "$(cmake --version | head -1)" \
  --arg wxwidgets "$(wx-config --version 2>/dev/null || printf not-reported)" \
  --arg package "$(basename "$archive")" \
  --arg checksum "$(sha256sum "$archive" | awk '{print $1}')" \
  '{schema: $schema, target: $target, repository_commit: $commit,
    plugin_version: $version, operating_system: $os,
    architecture: $architecture, compiler: $compiler,
    cmake_version: $cmake, wxwidgets_version: $wxwidgets,
    build_status: "passed", test_status: "passed",
    package_status: "passed", metadata_validation_status: "passed",
    installation_status: "staged-only", plugin_load_status: "not-run",
    graphical_test_status: "not-run", package_filename: $package,
    package_checksum_sha256: $checksum,
    result_classification: "build-and-package-only"}' \
  >"${artifact_dir}/result.json"
