#!/usr/bin/env bash
# Reachable only from the parameter-, branch- and approval-gated workflow.
set -euo pipefail
set +x

test "${CIRCLE_BRANCH:-}" = "alpha" || {
  echo "OfflineTides Alpha publication is restricted to the alpha branch" >&2
  exit 2
}
test "${OFFLINETIDES_PUBLICATION_APPROVED:-}" = "yes" || {
  echo "OFFLINETIDES_PUBLICATION_APPROVED=yes is required" >&2
  exit 2
}
test -n "${CLOUDSMITH_API_KEY:-}" || {
  echo "CLOUDSMITH_API_KEY is not configured" >&2
  exit 2
}

repo=${CLOUDSMITH_ALPHA_REPO:-pob220/offlinetides-alpha-oss}
artifact_root=${OFFLINETIDES_DEPLOY_ARTIFACT_ROOT:-artifacts}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT HUP INT TERM
found=0

while IFS= read -r -d '' archive; do
  found=1
  package_dir=$(dirname "$archive")
  mapfile -t pair < <(ci/resolve-package-pair.sh "$package_dir" "$archive")
  metadata=${pair[1]}
  target=$(sed -n 's:.*<target>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</target>.*:\1:p' "$metadata")
  target_version=$(sed -n 's:.*<target-version>[[:space:]]*\([^[:space:]<]*\)[[:space:]]*</target-version>.*:\1:p' "$metadata")
  plugin_version=$(sed -n \
    's:.*<version>[[:space:]]*\([^<]*\)</version>.*:\1:p' "$metadata" | \
    tr -d '[:space:]')
  test -n "$target"
  test -n "$target_version"
  [[ "$plugin_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || {
    echo "Invalid plugin version in $metadata" >&2
    exit 1
  }
  cloudsmith_version="${plugin_version}+${CIRCLE_BUILD_NUM:-0}.$(git rev-parse --short=7 HEAD)"
  filename=$(basename "$archive")
  package_name="offlinetides_pi-${plugin_version}-${target}-${target_version}-tarball"
  metadata_name="offlinetides_pi-${plugin_version}-${target}-${target_version}-metadata"
  staged_xml="$stage/$(basename "$metadata")"
  sed -e "s|--pkg_repo--|$repo|g" \
      -e "s|--name--|$package_name|g" \
      -e "s|--version--|$cloudsmith_version|g" \
      -e "s|--filename--|$filename|g" \
      "$metadata" >"$staged_xml"

  cloudsmith push raw --republish --no-wait-for-sync \
    --name "$metadata_name" --version "$cloudsmith_version" \
    --summary "OfflineTides OpenCPN Alpha metadata for $target" \
    "$repo" "$staged_xml"
  cloudsmith push raw --republish --no-wait-for-sync \
    --name "$package_name" --version "$cloudsmith_version" \
    --summary "OfflineTides OpenCPN Alpha package for $target" \
    "$repo" "$archive"
done < <(find "$artifact_root" -mindepth 3 -maxdepth 3 -type f \
  -path '*/package/offlinetides_pi-*.tar.gz' -print0)

test "$found" -eq 1 || {
  echo "No OfflineTides publication artifacts were found" >&2
  exit 1
}
