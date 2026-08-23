#!/usr/bin/env bash
set -euo pipefail
set -x

repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
git config --global protocol.file.allow always
git submodule update --init --recursive

sudo apt update
sudo apt install -y flatpak flatpak-builder jq
flatpak remote-add --user --if-not-exists \
  flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub "org.freedesktop.Sdk//${SDK_VER}" >/dev/null
flatpak install --user -y flathub org.opencpn.OpenCPN >/dev/null

rm -rf build-flatpak
mkdir build-flatpak
cd build-flatpak
artifact_dir=$repo/artifacts/flatpak${SDK_VER}-${BUILD_ARCH}
log_dir=$artifact_dir/logs
package_dir=$artifact_dir/package
mkdir -p "$log_dir" "$package_dir"

cmake -DOCPN_TARGET="$OCPN_TARGET" \
  -DBUILD_ARCH="$BUILD_ARCH" \
  -DOCPN_FLATPAK_CONFIG=ON \
  -DSDK_VER="$SDK_VER" \
  -DFLATPAK_BRANCH=stable \
  -DWX_VER="$WX_VER" .. 2>&1 | tee "$log_dir/configure.log"
manifest=flatpak/org.opencpn.OpenCPN.Plugin.offlinetides.yaml
grep -q 'url: file://' "$manifest"
cmake --build . --target flatpak-build 2>&1 | tee "$log_dir/build.log"
# Frontend2's inherited flatpak-pkg target makes this harmless directory
# writable after packaging.  It is normally present in its legacy layout.
mkdir -p ../build
cmake --build . --target flatpak-pkg 2>&1 | tee "$log_dir/package.log"
../ci/test-flatpak-archive.sh . 2>&1 | tee "$log_dir/archive-validation.log"

pair=$(../ci/resolve-package-pair.sh .)
archive_source=$(printf '%s\n' "$pair" | sed -n '1p')
metadata_source=$(printf '%s\n' "$pair" | sed -n '2p')
cp -f "$archive_source" "$metadata_source" "$package_dir/"
(cd "$package_dir" && sha256sum "$(basename "$archive_source")" \
  "$(basename "$metadata_source")" >checksums.txt)
archive=$package_dir/$(basename "$archive_source")
version=$(sed -n 's:.*<version>[[:space:]]*\([^<]*\)</version>.*:\1:p' \
  "$metadata_source" | tr -d '[:space:]')
jq -n \
  --arg commit "$(git -C .. rev-parse HEAD)" \
  --arg target "flatpak${SDK_VER}-${BUILD_ARCH}" \
  --arg arch "$BUILD_ARCH" \
  --arg version "$version" \
  --arg package "$(basename "$archive")" \
  --arg checksum "$(sha256sum "$archive" | awk '{print $1}')" \
  '{schema:"offlinetides-target-result-v1",target:$target,
    repository_commit:$commit,plugin_version:$version,
    operating_system:"Flatpak",operating_system_version:"25.08",
    architecture:$arch,compiler:"Freedesktop SDK toolchain",
    build_status:"passed",test_status:"build-qualified",package_status:"passed",
    metadata_validation_status:"passed",installation_status:"staged-only",
    plugin_load_status:"not-run",package_filename:$package,
    package_checksum_sha256:$checksum,result_classification:"build-and-package-only"}' \
  >"$artifact_dir/result.json"
