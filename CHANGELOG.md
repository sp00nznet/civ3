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

### Fixed
- Headless scripts clicked each main-menu item once, which only selects it;
  the menu activates an item on the second click, as the original does.
  Documented, and the scripts click twice.
- A game folder deep enough to overflow the game's fixed path buffers booted
  to a blank, spinning screen; the host now passes the 8.3 short path.
- `--record` with `--watchdog`: exit code `0xC0000409` from closing the ffmpeg
  pipe mid-write, and a recording reopened (truncated) after it was closed.
