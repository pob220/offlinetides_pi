#!/usr/bin/env bash
set -euo pipefail

prefix=${1:?static dependency install prefix is required}
work=${2:-${prefix}.work}
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-2}

jsoncpp_version=1.9.6
zstd_version=1.5.7
libsodium_version=1.0.20

jsoncpp_sha=f93b6dd7ce796b13d02c108bc9f79812245a82e577581c4c9aabe57075c90ea2
zstd_sha=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
libsodium_sha=ebb65ef6ca439333c2bb41a0c1990587288da07f6c7fd07cb3a18cc18d30ce19

download() {
  url=$1
  output=$2
  checksum=$3
  if command -v sha256sum >/dev/null 2>&1; then
    checksum_command='sha256sum -c -'
  else
    checksum_command='shasum -a 256 -c -'
  fi
  if ! test -f "$output" || ! printf '%s  %s\n' "$checksum" "$output" | sh -c "$checksum_command"; then
    rm -f "$output"
    curl -fL --retry 3 --retry-delay 2 -o "$output" "$url"
  fi
  printf '%s  %s\n' "$checksum" "$output" | sh -c "$checksum_command"
}

rm -rf "$prefix" "$work/src" "$work/build"
mkdir -p "$prefix" "$work/downloads" "$work/src" "$work/build"

jsoncpp_archive=$work/downloads/jsoncpp-${jsoncpp_version}.tar.gz
zstd_archive=$work/downloads/zstd-${zstd_version}.tar.gz
libsodium_archive=$work/downloads/libsodium-${libsodium_version}.tar.gz

download \
  "https://github.com/open-source-parsers/jsoncpp/archive/refs/tags/${jsoncpp_version}.tar.gz" \
  "$jsoncpp_archive" "$jsoncpp_sha"
download \
  "https://github.com/facebook/zstd/releases/download/v${zstd_version}/zstd-${zstd_version}.tar.gz" \
  "$zstd_archive" "$zstd_sha"
download \
  "https://download.libsodium.org/libsodium/releases/libsodium-${libsodium_version}.tar.gz" \
  "$libsodium_archive" "$libsodium_sha"

mkdir -p "$work/src/jsoncpp" "$work/src/zstd" "$work/src/libsodium"
tar -xzf "$jsoncpp_archive" --strip-components=1 -C "$work/src/jsoncpp"
tar -xzf "$zstd_archive" --strip-components=1 -C "$work/src/zstd"
tar -xzf "$libsodium_archive" --strip-components=1 -C "$work/src/libsodium"

cmake -S "$work/src/jsoncpp" -B "$work/build/jsoncpp" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DBUILD_SHARED_LIBS=OFF \
  -DJSONCPP_WITH_TESTS=OFF \
  -DJSONCPP_WITH_POST_BUILD_UNITTEST=OFF \
  -DJSONCPP_WITH_PKGCONFIG_SUPPORT=ON
cmake --build "$work/build/jsoncpp" --parallel "$jobs"
cmake --install "$work/build/jsoncpp"

cmake -S "$work/src/zstd/build/cmake" -B "$work/build/zstd" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DZSTD_BUILD_SHARED=OFF \
  -DZSTD_BUILD_STATIC=ON \
  -DZSTD_BUILD_PROGRAMS=OFF \
  -DZSTD_BUILD_TESTS=OFF
cmake --build "$work/build/zstd" --parallel "$jobs"
cmake --install "$work/build/zstd"

(
  cd "$work/src/libsodium"
  ./configure --prefix="$prefix" --disable-shared --enable-static --with-pic
  make -j"$jobs"
  make install
)

test -f "$prefix/lib/libjsoncpp.a"
test -f "$prefix/lib/libzstd.a"
test -f "$prefix/lib/libsodium.a"

printf '%s\n' \
  "jsoncpp ${jsoncpp_version} ${jsoncpp_sha}" \
  "zstd ${zstd_version} ${zstd_sha}" \
  "libsodium ${libsodium_version} ${libsodium_sha}" \
  >"$prefix/offlinetides-runtime-deps.txt"
