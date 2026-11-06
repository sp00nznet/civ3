#!/bin/sh
cd "$(dirname "$0")" || exit 1
./setup-macos.sh "$@"
result=$?
if [ -t 0 ]; then
  printf '\nPress Return to close this window. '
  read -r answer
fi
exit "$result"
