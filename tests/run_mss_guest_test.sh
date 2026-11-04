#!/bin/bash
set -euo pipefail
TEST_BINARY=$1
FFMPEG=$2
TMP=$(mktemp -d "${TMPDIR:-/tmp}/civ3-mss.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
"$FFMPEG" -nostdin -v error -f lavfi -i 'sine=frequency=440:duration=0.25' \
  -ar 44100 -ac 2 -c:a libmp3lame -y "$TMP/tone.mp3"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$TEST_BINARY" "$TMP/tone.mp3"
