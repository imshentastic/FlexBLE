#!/usr/bin/env bash
set -euo pipefail

# Host-side build of crumble-prebake. Mirrors test/run_cmb_roundtrip_test.sh's
# bare-g++ pattern -- no PlatformIO, no Arduino toolchain. Resolves the repo
# root via git so the script works from any CWD inside the worktree.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BINARY="$BUILD_DIR/crumble-prebake"

mkdir -p "$BUILD_DIR"

# Phase 1 source set. host_shim/ first in include path so its Arduino.h /
# HalStorage.h take precedence over the firmware originals. Firmware-side
# sources get added piecewise as each phase grows.
SOURCES=(
  "$SCRIPT_DIR/src/main.cpp"
  "$SCRIPT_DIR/host_shim/HalStorage.cpp"
)

INCLUDES=(
  -I "$SCRIPT_DIR/host_shim"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
)

CXX="${CXX:-g++}"

echo "[build] $CXX ${CXXFLAGS[*]} ${INCLUDES[*]} ..."
"$CXX" "${CXXFLAGS[@]}" "${INCLUDES[@]}" "${SOURCES[@]}" -o "$BINARY"

echo "[done]  $BINARY"
