#!/usr/bin/env bash
set -euo pipefail
set -x

project_dir=${CIRCLE_WORKING_DIRECTORY:-$PWD}
cd "$project_dir"
git submodule update --init --recursive

test -n "${DOCKER_IMAGE:-}"
test -n "${OCPN_TARGET:-}"
test -n "${BUILD_ENV:-}"

circle_sha=${CIRCLE_SHA1:-}
if [[ -z "$circle_sha" ]]; then
  circle_sha=$(git rev-parse HEAD 2>/dev/null || printf unknown)
fi

mkdir -p build artifacts
docker build --build-arg "BASE_IMAGE=${DOCKER_IMAGE}" \
  -f "${DOCKERFILE:-ci/Dockerfile.linux}" \
  -t offlinetides-linux-build ci
docker run --rm \
  -e "OCPN_TARGET=${OCPN_TARGET}" \
  -e "BUILD_ENV=${BUILD_ENV}" \
  -e "BUILD_GTK3=${BUILD_GTK3:-true}" \
  -e "WX_VER=${WX_VER:-32}" \
  -e "CIRCLE_SHA1=${circle_sha}" \
  -e "CMAKE_BUILD_PARALLEL_LEVEL=${CMAKE_BUILD_PARALLEL_LEVEL:-3}" \
  -v "${PWD}:/src:rw" \
  -v "${PWD}/build:/work" \
  offlinetides-linux-build \
  /src/ci/build-linux-catalogue.sh

cp -a build/artifacts/. artifacts/
