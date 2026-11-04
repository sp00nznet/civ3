#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-macos"}
BINARY="$BUILD_DIR/civ3"
MODE=run
case "${1:-}" in
  run|--debug|--verify|--logs|--telemetry) MODE=$1; shift ;;
esac
python3 tools/macos_preflight.py "$@"
# Only stop a prior process launched from this checkout, never another civ3.
PID_FILE="$ROOT/work/macos-game.pid"
if [ -f "$PID_FILE" ]; then
  PID=$(cat "$PID_FILE")
  case "$PID" in
    ''|*[!0-9]*) ;;
    *) if [ "$(ps -p "$PID" -o comm= 2>/dev/null || true)" = "$BINARY" ]; then
         kill -TERM "$PID"
         for _ in {1..50}; do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
         if kill -0 "$PID" 2>/dev/null; then echo 'Previous game has not exited; close it before rebuilding.' >&2; exit 1; fi
       fi ;;
  esac
fi
./build-macos.sh
mkdir -p work
case "$MODE" in
  --debug) exec lldb -- "$BINARY" --run "$@" ;;
  --verify) exec python3 tools/macos_smoke.py --binary "$BINARY" "$@" ;;
  --logs|--telemetry) "$BINARY" --run "$@" 2>&1 | tee work/macos-game.log ;;
  *) echo $$ > "$PID_FILE"; exec "$BINARY" --run "$@" ;;
esac
