# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Reconnaissance of the Steam `Civ3Conquests.exe`: VC6, no DRM, 245 imports
  from 13 DLLs, renderer in `jgl.dll` ([docs/RECON.md](docs/RECON.md)).
- Function catalog seeded from `vtable_scan` (no RTTI): 15,308 functions,
  95.2% of `.text`.
- `run_lift.py`: the whole executable lifted, 15,929 functions, 0 errors.
- 32-bit host on pcrecomp `native32` (`src/runtime/host.c`): boots through
  the CRT, WinMain and Steam init to the main menu.
- 16-bit colour shim in jgl.dll's imports, standing in for the app-compat shim
  Windows applies only to the original file name.
- `--headless --record out.mp4`: jgl's GDI blits mirrored into a shadow bitmap
  and piped to ffmpeg; fixed display-mode list so it also works over RDP.
- Scripted input (`--move`, `--click`, `--key`).
- `tools/conformance.py`: 5 boot milestones and lift health against
  `conformance.json`.
- `Setup.cmd` quick start.
- `[game]` milestones from game state (the main menu's modal flag), and two
  more harness milestones: main menu open, and a scripted Quick Start into a
  game. The harness is at 7/7.
- `--peek`: the UI widget tree and globals from a host thread, for bring-up.
- Plays: a scripted run goes Quick Start, found Rome, end turn, 3950 BC.
- `--original` (`src/runtime/oracle.c`): the shipping machine code run
  natively in the same host, and `tools/oracle.py`, which plays both builds
  from one save and compares the autosaves. The same game state every turn for
  19 turns; the bytes that differ are a GUID, timing values and pointers.
- `tools/playtest.py`: 100 cases with pass/fail verdicts (every main-menu
  item and the way back, every in-game screen by hotkey and button then 5
  turns, all 31 civilizations, all 12 scenarios), a private game folder per
  case ([docs/testing.md](docs/testing.md)).
- Scripted input: `--open` (click until the screen changes), `--wait VA@s`
  (hold until a game value is set; `0xA74EA4` is the turn number), modifiers
  on `--key` (`c+0x53` is Ctrl-S) through `GetKeyState`/`GetAsyncKeyState`
  shims, and the left button for code that polls it.
- `--nosteam`, and `--dump` now covers every writable region (globals included).
- The resolution keys in `conquests.ini`: `KeepRes=1` with `Video Mode=1280`
  runs at 1280x1024 (README, *Resolution*). The headless mode list has
  1152x864, 1600x1200 and the desktop's size for them.

### Fixed
- Headless scripts clicked each main-menu item once, which only selects it;
  the menu activates an item on the second click, as the original does.
  Documented, and the scripts click twice.
- A game folder deep enough to overflow the game's fixed path buffers booted
  to a blank, spinning screen; the host now passes the 8.3 short path.
- `--record` with `--watchdog`: exit code `0xC0000409` from closing the ffmpeg
  pipe mid-write, and a recording reopened (truncated) after it was closed.
- Exit stalled under load with `--record`: frames were written to ffmpeg
  while holding the lock jgl's blits wait on. And an exit already under way
  no longer gets reported as a watchdog timeout.
- Play-test cases shared the install's `conquests.ini` through hard links,
  and the game rewrites it in place; each case now gets a copy.
