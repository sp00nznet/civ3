#!/bin/bash
# Native Apple Silicon/Intel build; never uses -m32, Wine, or Rosetta.
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$PWD
if [ "$(uname -s)" != Darwin ]; then
  echo 'Use build-macos.sh on macOS; Linux uses build-linux.sh.' >&2
  exit 2
fi
if [ -z "${PCRECOMP:-}" ]; then
  for d in ../tools ../pcrecomp; do
    if [ -f "$d/runtime/win32hle/guest_mem.c" ]; then
      PCRECOMP=$(cd "$d" && pwd)
      break
    fi
  done
fi
: "${PCRECOMP:?Run ./setup-macos.sh first (pcrecomp is missing)}"
for t in cmake ninja pkg-config; do
  command -v "$t" >/dev/null || { echo "Missing $t: run ./setup-macos.sh --install-deps" >&2; exit 1; }
done
pkg-config --exists sdl2 SDL2_ttf libavformat libavcodec libavutil libswresample || {
  echo 'Missing native SDL2/SDL2_ttf/FFmpeg libraries; run ./setup-macos.sh --install-deps' >&2
  exit 1
}
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-macos"}
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DPCRECOMP="$PCRECOMP" -DGEN_OPT="${GEN_OPT:--O0}" "$@"
# Modest default: generated C can use substantial compiler memory.
cmake --build "$BUILD_DIR" --parallel "${JOBS:-3}"
