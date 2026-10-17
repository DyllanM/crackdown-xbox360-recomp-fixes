#!/usr/bin/env bash
# Builds and runs the pure-logic tests in tests/ (no game files, no ReXGlue runtime) with clang++-20 on Linux.
#
#   tools/test.sh                  build and run every test
#   tools/test.sh user_data        only tests whose name matches
#
# CRACKDOWN_TEST_BUILD sets the build folder (default out/tests-linux); CRACKDOWN_SDK_DIR the unpacked linux-amd64 SDK
# (default tools/rexglue/linux-amd64).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${CRACKDOWN_TEST_BUILD:-$ROOT/out/tests-linux}"

[ "$(uname -s)" = Linux ] || { echo "ERROR: runs on Linux only (tools/test.ps1 is the Windows script)" >&2; exit 1; }

SDK_ARGS=()
[ -z "${CRACKDOWN_SDK_DIR:-}" ] || SDK_ARGS=("-DCRACKDOWN_SDK_DIR=$CRACKDOWN_SDK_DIR")
cmake -S "$ROOT/tests" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 "${SDK_ARGS[@]}"
cmake --build "$BUILD" --target crackdown_tests
if [ -n "${1:-}" ]; then
  ctest --test-dir "$BUILD" --output-on-failure -R "$1"
else
  ctest --test-dir "$BUILD" --output-on-failure
fi
