#!/usr/bin/env bash
set -euo pipefail

# Host-side build + run of the .cmb format library round-trip test.
# No ESP-IDF, no platformio.  Just g++/clang on the format library
# sources plus the test driver.  Exercises CmbWriter -> CmbReader
# end-to-end on a temp file.
#
# Usage:
#   test/run_cmb_roundtrip_test.sh
#
# Exit status mirrors the test binary (0 on PASS).

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/cmb_roundtrip"
BINARY="$BUILD_DIR/CmbRoundtripTest"

mkdir -p "$BUILD_DIR"

SOURCES=(
  "$ROOT_DIR/test/cmb_roundtrip/CmbRoundtripTest.cpp"
  "$ROOT_DIR/lib/Cmb/CmbWriter.cpp"
  "$ROOT_DIR/lib/Cmb/CmbReader.cpp"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  -pedantic
  -I"$ROOT_DIR"
  -I"$ROOT_DIR/lib"
)

CXX="${CXX:-g++}"

echo "[build] $CXX ${CXXFLAGS[*]} ..."
"$CXX" "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

echo "[run]   $BINARY"
"$BINARY"
