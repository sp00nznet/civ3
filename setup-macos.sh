#!/bin/bash
# Finder starts with a minimal PATH; prefer this Mac's native Homebrew tools.
set -euo pipefail
cd "$(dirname "$0")"
case "$(uname -m)" in
  arm64) export PATH="/opt/homebrew/bin:$PATH" ;;
  x86_64) export PATH="/usr/local/bin:$PATH" ;;
esac
PYTHON=${PYTHON:-python3}
command -v "$PYTHON" >/dev/null || { echo 'Python 3.10+ is required. Install Homebrew Python first.' >&2; exit 1; }
exec "$PYTHON" tools/setup_macos.py "$@"
