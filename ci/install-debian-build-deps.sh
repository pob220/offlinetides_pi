#!/bin/sh
set -eu

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  binutils \
  build-essential \
  ca-certificates \
  cmake \
  gettext \
  git \
  jq \
  lsb-release \
  ninja-build \
  pkg-config \
  tzdata \
  libbz2-dev \
  libcurl4-openssl-dev \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  libgtk-3-dev \
  libjsoncpp-dev \
  libnetcdf-dev \
  libsodium-dev \
  libwxgtk3.2-dev \
  libzstd-dev \
  zlib1g-dev

rm -rf /var/lib/apt/lists/*
