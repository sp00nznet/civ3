#!/bin/bash
# Linux setup: the same pipeline as Setup.cmd, built as a native Linux
# program (build-linux.sh, pcrecomp's win32hle: no Wine).
#
#   ./setup.sh                 # find the game in your Steam libraries
#   ./setup.sh --game-dir DIR  # or use this install
#   ./setup.sh --force         # redo every step
#
# It links game/ to the install (nothing is copied; the game keeps its saves
# and settings there), catalogs and lifts the executable and its two DLLs,
# builds build-linux/civ3, and leaves "Civilization III (recomp).sh" here.
# A rerun skips the finished steps.
set -e
cd "$(dirname "$0")"
ROOT=$PWD
FORCE=0
GAME_ARG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --force) FORCE=1 ;;
    --yes) ;;   # recomp-launcher passes it; nothing here asks
    --game-dir) GAME_ARG=$2; shift ;;
    *) echo "usage: ./setup.sh [--game-dir DIR] [--force] [--yes]" >&2; exit 2 ;;
  esac
  shift
done
say()  { printf '%s\n' "$*"; }
STEP=0 STEPS=5
step() { STEP=$((STEP + 1)); printf '\n\033[36m[%d/%d] %s\033[0m\n' "$STEP" "$STEPS" "$*"; }
fail() { printf '\n\033[31mSetup stopped: %s\033[0m\n' "$*" >&2; exit 1; }
done_already() { [ "$FORCE" = 0 ] && [ -e "$1" ]; }

step "Checking the tools"
need=()
for t in python3 gcc cmake ninja pkg-config; do command -v $t >/dev/null || need+=("$t"); done
python3 -c 'import pefile, capstone' 2>/dev/null || need+=("python3-pefile python3-capstone")
echo 'int main(void){return 0;}' | gcc -m32 -x c - -o /dev/null 2>/dev/null || need+=("gcc-multilib")
PKG_CONFIG_PATH=/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib32/pkgconfig pkg-config --exists sdl2 SDL2_ttf libavformat libavcodec libswresample \
  || need+=("SDL2, SDL2_ttf and ffmpeg's libraries for i386")
if [ ${#need[@]} -gt 0 ]; then
  say "  Missing: ${need[*]}"
  say "  Debian, Ubuntu: sudo dpkg --add-architecture i386 && sudo apt update"
  say "                  sudo apt install gcc-multilib cmake ninja-build pkg-config python3:native python3-pefile python3-capstone fonts-liberation \\"
  say "                       libsdl2-dev:i386 libsdl2-ttf-dev:i386 libavformat-dev:i386 libavcodec-dev:i386 libswresample-dev:i386 libavutil-dev:i386"
  fail "install those, then run ./setup.sh again."
fi

# The toolkit: PCRECOMP, else ../pcrecomp, else ../tools, as build-linux.sh finds it.
if [ -z "$PCRECOMP" ]; then
  for d in ../pcrecomp ../tools; do [ -d "$d/runtime/win32hle" ] && PCRECOMP=$(cd "$d" && pwd) && break; done
fi
[ -n "$PCRECOMP" ] || fail "no pcrecomp toolkit: git clone https://github.com/sp00nznet/pcrecomp ../pcrecomp"
[ -f "$PCRECOMP/runtime/win32hle/mss.c" ] || fail "$PCRECOMP is too old for Civilization III (no win32hle/mss.c): git -C $PCRECOMP pull"
export PCRECOMP
say "  pcrecomp: $PCRECOMP"

step "Finding Civilization III Complete"
if [ ! -e game ] || [ "$FORCE" = 1 ]; then
  found="$GAME_ARG"
  if [ -z "$found" ]; then
    for lib in "$HOME/.steam/steam" "$HOME/.local/share/Steam" "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; do
      [ -f "$lib/steamapps/libraryfolders.vdf" ] || continue
      while read -r p; do
        c="$p/steamapps/common/Sid Meier's Civilization III Complete"
        [ -f "$c/Conquests/Civ3Conquests.exe" ] && found=$c && break 2
      done < <(sed -n 's/.*"path"[[:space:]]*"\(.*\)".*/\1/p' "$lib/steamapps/libraryfolders.vdf"; echo "$lib")
    done
  fi
  [ -n "$found" ] && [ -f "$found/Conquests/Civ3Conquests.exe" ] || fail "no Civilization III Complete found: ./setup.sh --game-dir DIR (the folder with Conquests/ in it)"
  rm -f game
  ln -s "$found" game
fi
say "  game -> $(readlink game || echo game)"

step "Cataloging the executable (about 10 minutes)"
mkdir -p work
done_already work/Civ3Conquests.exe || cp game/Conquests/Civ3Conquests.exe work/
done_already work/vtable_seeds.json || python3 "$PCRECOMP/tools/cpp/vtable_scan.py" work/Civ3Conquests.exe --seeds work/vtable_seeds.json
done_already work/functions.json || python3 "$PCRECOMP/tools/disasm/disasm32.py" work/Civ3Conquests.exe -o work/functions.json \
  --seed-functions work/vtable_seeds.json > work/disasm32.log
for m in jgl sound; do
  done_already work/${m}_functions.json && continue
  python3 -c "import run_lift; run_lift.copy_dll('$m')"             # into work/, sound.dll rebased: cataloged where it runs
  python3 "$PCRECOMP/tools/cpp/vtable_scan.py" work/$m.dll --seeds work/${m}_vtable_seeds.json
  python3 "$PCRECOMP/tools/disasm/disasm32.py" work/$m.dll -o work/${m}_functions.json \
    --seed-functions work/${m}_vtable_seeds.json > work/${m}_disasm32.log
done

step "Lifting (a few minutes)"
done_already src/recomp/gen/recomp_dispatch.inc || python3 run_lift.py --all | tail -3
for m in jgl sound; do done_already src/recomp/gen_$m/recomp_dispatch.inc || python3 run_lift.py --all --module $m | tail -3; done

step "Building (half an hour or so)"
./build-linux.sh

cat > "Civilization III (recomp).sh" <<EOF
#!/bin/sh
cd "$ROOT" && exec build-linux/civ3 --run "\$@"
EOF
chmod +x "Civilization III (recomp).sh"
printf '\n\033[32mDone: ./"Civilization III (recomp).sh"\033[0m\n'
