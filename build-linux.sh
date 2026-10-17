#!/bin/sh
# Configure and build the native Linux host: the lifted C on pcrecomp's
# runtime/win32hle, a 32-bit Linux program with no Wine (src/linux,
# cmake/linux-i386.cmake).
#
#   ./build-linux.sh            # -> build-linux/civ3
#   build-linux/civ3 --run      # from here, with the game in ./game
#
# Needs the three lifts (run_lift.py --all, --module jgl, --module sound),
# gcc with -m32 (gcc-multilib), cmake, ninja, and SDL2, SDL2_ttf and
# ffmpeg's libraries for i386 (README, Linux).
set -e
cd "$(dirname "$0")"
BUILD_DIR=${BUILD_DIR:-build-linux}
# The toolkit: PCRECOMP, else ../pcrecomp, else ../tools (older setups).
if [ -z "$PCRECOMP" ]; then
  if [ ! -d ../pcrecomp ] && [ -d ../tools/runtime/win32hle ]; then PCRECOMP=$(cd ../tools && pwd)
  else PCRECOMP=$(cd .. && pwd)/pcrecomp; fi
fi
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  # shellcheck disable=SC2086  # CMAKE_ARGS is a list of flags
  cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/linux-i386.cmake -DPCRECOMP="$PCRECOMP" $CMAKE_ARGS
fi
cmake --build "$BUILD_DIR" "$@"
