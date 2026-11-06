#!/bin/bash
# Reproducible, source-only Finder launcher. It uses the locally built game.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  echo "Usage: ./script/build_macos_app.sh [--install-desktop]"
  echo "Build an ad-hoc signed Civilization III.app without proprietary game files."
  echo "The optional flag replaces only our matching Desktop launcher after validation."
  exit 0
fi
if [ "$#" -gt 1 ] || { [ "$#" -eq 1 ] && [ "$1" != "--install-desktop" ]; }; then
  echo "Invalid argument. Use --help." >&2
  exit 2
fi
[ "$(uname -s)" = Darwin ] || { echo "Run on macOS." >&2; exit 2; }
if [ "${1:-}" = "--install-desktop" ] && pgrep -x Civ3Launcher >/dev/null 2>&1; then
  echo "Close the running Civilization III app before replacing the Desktop launcher." >&2
  exit 75
fi
for tool in xcrun sips iconutil plutil codesign ditto; do
  command -v "$tool" >/dev/null || { echo "Missing $tool." >&2; exit 1; }
done

DEST="$ROOT/dist/Civilization III.app"
mkdir -p "$ROOT/dist"
STAGE=$(mktemp -d "$ROOT/dist/.civ3-build.XXXXXX")
DESKTOP_STAGE=""
trap 'rm -rf "$STAGE"; if [ -n "$DESKTOP_STAGE" ]; then rm -rf "$DESKTOP_STAGE"; fi' EXIT
APP="$STAGE/Civilization III.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

xcrun swiftc -O -framework AppKit "$ROOT/src/macos_launcher/Civ3Launcher.swift" \
  -o "$APP/Contents/MacOS/Civ3Launcher"
xcrun swiftc -O -framework AppKit "$ROOT/src/macos_launcher/MakeIcon.swift" \
  -o "$STAGE/MakeIcon"
"$STAGE/MakeIcon" "$STAGE/icon-1024.png"
ICONSET="$STAGE/CivilizationIII.iconset"
mkdir -p "$ICONSET"
for size in 16 32 128 256 512; do
  sips -s format png -z "$size" "$size" "$STAGE/icon-1024.png" \
    --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
  size2=$((size*2))
  sips -s format png -z "$size2" "$size2" "$STAGE/icon-1024.png" \
    --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/CivilizationIII.icns"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>Civilization III</string>
  <key>CFBundleDisplayName</key><string>Civilization III</string>
  <key>CFBundleExecutable</key><string>Civ3Launcher</string>
  <key>CFBundleIdentifier</key><string>dev.biro.civilization3.launcher</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>CFBundleIconFile</key><string>CivilizationIII.icns</string>
  <key>LSMinimumSystemVersion</key><string>12.0</string>
  <key>LSUIElement</key><true/>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
plutil -lint "$APP/Contents/Info.plist"
xattr -cr "$APP"
codesign --force --sign - "$APP"
codesign --verify "$APP"

# The dist output is intentionally only a launcher, not a redistributable game.
if [ -L "$DEST" ]; then echo "Refusing symlinked destination $DEST" >&2; exit 1; fi
if [ -d "$DEST" ]; then
  CURRENT=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$DEST/Contents/Info.plist" 2>/dev/null || true)
  [ "$CURRENT" = dev.biro.civilization3.launcher ] || { echo "Refusing unrelated app at $DEST" >&2; exit 1; }
  mv "$DEST" "$STAGE/previous.app"
fi
if ! mv "$APP" "$DEST"; then
  if [ -d "$STAGE/previous.app" ]; then mv "$STAGE/previous.app" "$DEST"; fi
  echo "App build failed to install; restored previous version" >&2
  exit 1
fi
if ! codesign --verify "$DEST"; then
  rm -rf "$DEST"
  if [ -d "$STAGE/previous.app" ]; then mv "$STAGE/previous.app" "$DEST"; fi
  echo "New app failed signature validation; restored previous version" >&2
  exit 1
fi
echo "Built: $DEST"

if [ "${1:-}" = "--install-desktop" ]; then
  TARGET="$HOME/Desktop/Civilization III.app"
  if [ -L "$TARGET" ]; then echo "Refusing symlinked Desktop destination" >&2; exit 1; fi
  if [ -e "$TARGET" ]; then
    CURRENT=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$TARGET/Contents/Info.plist" 2>/dev/null || true)
    [ "$CURRENT" = dev.biro.civilization3.launcher ] || { echo "Refusing unrelated Desktop app" >&2; exit 1; }
  fi
  DESKTOP_STAGE=$(mktemp -d "$HOME/Desktop/.civ3-install.XXXXXX")
  ditto "$DEST" "$DESKTOP_STAGE/Civilization III.app"
  codesign --verify "$DESKTOP_STAGE/Civilization III.app"
  if [ -e "$TARGET" ]; then mv "$TARGET" "$DESKTOP_STAGE/previous.app"; fi
  if ! mv "$DESKTOP_STAGE/Civilization III.app" "$TARGET"; then
    if [ -d "$DESKTOP_STAGE/previous.app" ]; then mv "$DESKTOP_STAGE/previous.app" "$TARGET"; fi
    echo "Desktop update failed; restored previous launcher" >&2
    exit 1
  fi
  if ! codesign --verify "$TARGET"; then
    rm -rf "$TARGET"
    if [ -d "$DESKTOP_STAGE/previous.app" ]; then
      mv "$DESKTOP_STAGE/previous.app" "$TARGET"
    fi
    echo "Desktop launcher failed signature validation; restored old version" >&2
    exit 1
  fi
  echo "Installed: $TARGET"
fi
