#!/bin/sh
# Bootstrap Python from native Homebrew when Finder has a minimal PATH.
set -eu
cd "$(dirname "$0")"
case "$(uname -m)" in
  arm64) PATH="/opt/homebrew/bin:$PATH" ;;
  x86_64) PATH="/usr/local/bin:$PATH" ;;
esac
export PATH
if ! command -v "${PYTHON:-python3}" >/dev/null 2>&1; then
  printf '%s\n' 'Python 3.10+ is required. Install Homebrew Python: brew install python' >&2
  exit 1
fi
exec "${PYTHON:-python3}" tools/setup_macos.py "$@"
