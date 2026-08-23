#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
git submodule update --init --recursive

export HOMEBREW_NO_AUTO_UPDATE=1
while IFS= read -r package; do
  case "$package" in
    ''|'#'*) continue ;;
  esac
  brew list --versions "$package" >/dev/null 2>&1 || brew install "$package" </dev/null
done <build-deps/macos-deps

brew_prefix=$(brew --prefix)
wx_prefix=$(brew --prefix wxwidgets@3.2)
export PATH="${wx_prefix}/bin:${brew_prefix}/opt/gettext/bin:${brew_prefix}/bin:${PATH}"
export PKG_CONFIG_PATH="${wx_prefix}/lib/pkgconfig:${brew_prefix}/lib/pkgconfig"
export CMAKE_PREFIX_PATH="${wx_prefix};${brew_prefix}"
export WX_CONFIG="${wx_prefix}/bin/wx-config-3.2"
export OCPN_TARGET=macos-arm64
export WX_VER=32

test_build=$repo/build-macos-tests
package_build=$repo/build-macos-package
stage=$repo/stage-macos
runtime_prefix=$repo/build-macos-runtime-deps
runtime_work=$repo/build-macos-runtime-work
artifact=$repo/artifacts/macos-arm64
log_dir=$artifact/logs
test_dir=$artifact/tests
package_dir=$artifact/package
rm -rf "$test_build" "$package_build" "$stage" "$artifact"
mkdir -p "$test_build" "$package_build" "$stage" "$log_dir" "$test_dir" "$package_dir"

while IFS= read -r package; do
  case "$package" in
    ''|'#'*) continue ;;
  esac
  brew list --versions "$package"
done <build-deps/macos-deps >"$log_dir/dependencies.log"

cmake -S . -B "$test_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH" \
  -DwxWidgets_CONFIG_EXECUTABLE="$WX_CONFIG" \
  -DXTIDAL_STANDALONE_API=ON \
  -DXTIDAL_BUILD_AUTHORING_TOOLS=ON \
  -DOCPN_BUILD_TEST=ON \
  2>&1 | tee "$log_dir/configure-tests.log"
cmake --build "$test_build" --parallel 3 2>&1 | tee "$log_dir/build-tests.log"
ctest --test-dir "$test_build" --output-on-failure \
  --output-junit "$test_dir/ctest.xml" 2>&1 | tee "$log_dir/test.log"

ci/build-runtime-deps-unix.sh "$runtime_prefix" "$runtime_work" \
  2>&1 | tee "$log_dir/runtime-dependencies.log"
export PKG_CONFIG_PATH="${runtime_prefix}/lib/pkgconfig:${wx_prefix}/lib/pkgconfig:${brew_prefix}/lib/pkgconfig"
cmake -S . -B "$package_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_PREFIX_PATH="${runtime_prefix};${CMAKE_PREFIX_PATH}" \
  -DwxWidgets_CONFIG_EXECUTABLE="$WX_CONFIG" \
  -DOFFLINETIDES_STATIC_RUNTIME_DEPS=ON \
  -DXTIDAL_STANDALONE_API=ON \
  -DXTIDAL_BUILD_AUTHORING_TOOLS=OFF \
  -DOCPN_BUILD_TEST=OFF \
  2>&1 | tee "$log_dir/configure-package.log"
cmake --build "$package_build" --parallel 3 2>&1 | tee "$log_dir/build-package.log"
cmake --install "$package_build" --prefix "$stage" 2>&1 | tee "$log_dir/install.log"
cmake --build "$package_build" --target package 2>&1 | tee "$log_dir/package.log"

pair=$(ci/resolve-package-pair.sh "$package_build")
archive_source=$(printf '%s\n' "$pair" | sed -n '1p')
metadata_source=$(printf '%s\n' "$pair" | sed -n '2p')
grep -q '<target>darwin-wx32</target>' "$metadata_source"
tar -tzf "$archive_source" >"$test_dir/archive-contents.txt"
grep -q 'libofflinetides_pi.dylib' "$test_dir/archive-contents.txt"
if tar -xOf "$archive_source" "$(tar -tzf "$archive_source" | grep 'libofflinetides_pi.dylib$' | head -1)" \
    >"$test_dir/libofflinetides_pi.dylib"; then
  otool -L "$test_dir/libofflinetides_pi.dylib" >"$test_dir/dylib-dependencies.txt"
fi
if grep -Eqi '(jsoncpp|sodium|zstd)' "$test_dir/dylib-dependencies.txt"; then
  echo "macOS package retains an unbundled OfflineTides runtime dependency" >&2
  exit 1
fi

cp -f "$archive_source" "$metadata_source" "$package_dir/"
(cd "$package_dir" && shasum -a 256 "$(basename "$archive_source")" \
  "$(basename "$metadata_source")" >checksums.txt)
archive=$package_dir/$(basename "$archive_source")
version=$(sed -n 's:.*<version>[[:space:]]*\([^<]*\)</version>.*:\1:p' "$metadata_source")
jq -n \
  --arg commit "$(git rev-parse HEAD)" \
  --arg version "$version" \
  --arg os "$(sw_vers -productVersion)" \
  --arg package "$(basename "$archive")" \
  --arg checksum "$(shasum -a 256 "$archive" | awk '{print $1}')" \
  '{schema:"offlinetides-target-result-v1",target:"macos-arm64",
    repository_commit:$commit,plugin_version:$version,operating_system:"macOS",
    operating_system_version:$os,architecture:"arm64",compiler:"Apple Clang",
    build_status:"passed",test_status:"passed",package_status:"passed",
    metadata_validation_status:"passed",installation_status:"staged-only",
    plugin_load_status:"not-run",package_filename:$package,
    package_checksum_sha256:$checksum,result_classification:"build-and-package-only"}' \
  >"$artifact/result.json"
