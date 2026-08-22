#!/usr/bin/env bash
set -euo pipefail

source_dir=$(cd "${1:-/src}" && pwd)
work_dir=${2:-/work}
test_build=${work_dir}/test-build
package_build=${work_dir}/package-build
stage_dir=${work_dir}/stage
artifact_dir=${work_dir}/artifacts/${OCPN_TARGET:-linux}
log_dir=${artifact_dir}/logs
test_dir=${artifact_dir}/tests
package_dir=${artifact_dir}/package

rm -rf "$test_build" "$package_build" "$stage_dir" "$artifact_dir"
mkdir -p "$test_build" "$package_build" "$stage_dir" \
  "$log_dir" "$test_dir" "$package_dir"

cmake -S "$source_dir" -B "$test_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_GTK3=ON \
  -DXTIDAL_STANDALONE_API=ON \
  -DXTIDAL_BUILD_AUTHORING_TOOLS=ON \
  -DOCPN_BUILD_TEST=ON 2>&1 | tee "$log_dir/configure-tests.log"
cmake --build "$test_build" \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" \
  2>&1 | tee "$log_dir/build-tests.log"
ctest --test-dir "$test_build" --output-on-failure \
  --output-junit "$test_dir/ctest.xml" \
  2>&1 | tee "$log_dir/test.log"

# Package from a runtime-only tree. NetCDF and raw-model authoring code must
# not become dependencies of the distributable OpenCPN plugin.
cmake -S "$source_dir" -B "$package_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DBUILD_GTK3=ON \
  -DXTIDAL_STANDALONE_API=ON \
  -DXTIDAL_BUILD_AUTHORING_TOOLS=OFF \
  -DOCPN_BUILD_TEST=OFF 2>&1 | tee "$log_dir/configure-package.log"
cmake --build "$package_build" \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}" \
  2>&1 | tee "$log_dir/build-package.log"
DESTDIR="$stage_dir" cmake --install "$package_build" --prefix /usr \
  >"$log_dir/install.log" 2>&1
cmake --build "$package_build" --target package \
  2>&1 | tee "$log_dir/package.log"

"$source_dir/ci/test-catalogue-archive.sh" "$package_build" "$stage_dir" \
  "$source_dir" \
  2>&1 | tee "$log_dir/archive-validation.log"

"$source_dir/ci/test-publication-contract.sh" "$source_dir" \
  2>&1 | tee "$log_dir/publication-contract.log"
OFFLINETIDES_SOURCE_DIR="$source_dir" \
  "$source_dir/ci/collect-build-artifacts.sh" "$package_build" \
  "${OCPN_TARGET:-linux}" "$artifact_dir"

# Docker creates the retained evidence as root. Make it readable and removable
# by the host runner without assuming that runner has passwordless sudo.
chmod -R a+rwX "$work_dir"
