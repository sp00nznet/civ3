#!/bin/sh
# Native arm64 or x86_64 build. Run setup-macos.sh first to generate the game.
set -eu
cd "$(dirname "$0")"
if [ "${1:-}" = --help ]; then
  printf '%s\n' 'usage: ./build-macos.sh [cmake --build options]' \
    'Environment: PCRECOMP, BUILD_DIR, CMAKE_BUILD_PARALLEL_LEVEL (default 4)' \
    'CIV3_BUILD_TESTS_ONLY=1 builds synthetic tests without game files.'
  exit 0
fi
if [ "$(uname -s)" != Darwin ]; then
  printf '%s\n' 'This build script is for macOS. Use build-linux.sh on Linux.' >&2
  exit 1
fi
BUILD_DIR=${BUILD_DIR:-build-macos}
if [ -z "${PCRECOMP:-}" ]; then
  for candidate in .deps/pcrecomp ../pcrecomp ../tools; do
    if [ -f "$candidate/runtime/win32hle/guest_mem.c" ]; then
      PCRECOMP=$(cd "$candidate" && pwd)
      break
    fi
  done
fi
if [ -z "${PCRECOMP:-}" ]; then
  printf '%s\n' 'No compatible pcrecomp checkout. Run ./setup-macos.sh first.' >&2
  exit 1
fi
if [ "${CIV3_BUILD_TESTS_ONLY:-0}" = 1 ]; then
  cmake -S . -B "$BUILD_DIR" -G Ninja -DPCRECOMP="$PCRECOMP" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
    -DCIV3_BUILD_GAME=OFF -DCIV3_BUILD_TESTS=ON
else
  cmake -S . -B "$BUILD_DIR" -G Ninja -DPCRECOMP="$PCRECOMP" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" \
    -DCIV3_BUILD_GAME=ON -DCIV3_BUILD_TESTS=OFF
fi
cmake --build "$BUILD_DIR" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}" "$@"
if [ "${CIV3_BUILD_TESTS_ONLY:-0}" = 1 ]; then
  ctest --test-dir "$BUILD_DIR" --output-on-failure
fi
