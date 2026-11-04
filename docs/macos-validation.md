# macOS validation ledger

Local verification on **November 3, 2026**, not a claim of complete game compatibility.

## Environment and reproducibility

- Host observed: macOS 27.2 (26B123), Apple Silicon `arm64`, 16 GB RAM.
- Civ3 upstream base: `672038e`.
- pcrecomp: `ef3f0da6e08a8eb7a3f869273f3df071ca8d7373` (clean pinned checkout).
- Game input: supported Steam executable SHA-1
  `01f705b1ce6f5d1cfd5a861f88ca1413764f8019`.
- Python packages: `pefile==2024.8.26`, `capstone==5.0.9` in a local venv.
- Built with Apple Clang, CMake/Ninja, native SDL2/SDL2_ttf/FFmpeg.
- Tested generated-code optimization: **`-O0`**, debug/bring-up build.
- `file build-macos/civ3`: Mach-O 64-bit executable arm64.
- No Wine, Rosetta, Windows VM or JIT in the tested native launch path.

## Verified results

| Check | Observed result |
|---|---|
| Upstream based-memory HLE self-tests | 12 passed natively on this Mac |
| Setup, asset preflight, preference backup and smoke-verdict Python tests | 19 passed |
| Miles guest-memory regression | Passed five synthetic MP3 open/decode/close cycles |
| INI regression | Passed repeated key updates, insertion, deletion, case-independent sections, duplicate repair, failure and zero-size-buffer checks |
| SDL ownership regression | Passed worker pump/draw/cursor/title checks; title changes are deferred until the owner pumps |
| Synthetic-tests-only CMake build | All three native regression tests passed from a source-only staging directory with no game assets or generated game code |
| Main executable import binding | 245 imports, zero unresolved |
| Renderer / sound imports | 121 / 136 imports, zero unresolved |
| Main menu | Reached; valid 1024x768 PPM, PNG visually inspected |
| Headless gameplay, sound disabled | Quick Start, founded Rome, progressed to turn 4 / 3800 BC; screenshot visually inspected |
| Headless gameplay, audio enabled, INI fix applied | Quick Start and turn 2; expected watchdog exit 4 |
| Native window | Cocoa window registered on screen, 1280-pixel width; direct window screenshot blocked by the Mac's capture permissions |
| Windowed gameplay, audio enabled, culture-linked starts disabled | Completed Quick Start, founded Rome, and reached turn 3 / 3850 BC; framebuffer visually inspected; expected watchdog termination |

The main menu milestone alone does not prove gameplay. A valid nonuniform
framebuffer is required by the smoke checker. The explicit turn counts above
come from completed bounded runs, not from a running process or an empty log.

Local evidence remains in the ignored `work/` directory:

- `macos-runtime-tests.log`, `macos-setup-tests.log`, `macos-regression-tests.log`.
- `macos-dry-run.log`, `macos-smoke.log`, `macos-menu.png`.
- `macos-gameplay.log`, `macos-gameplay.png` (silent turn-4 run).
- `macos-profile-game.log`, `macos-profile-game.ppm` (audio-enabled turn-2 run).
- `macos-window-check.log` (OS window evidence), `macos-window-safe.log`,
  `macos-window-safe.png` (successful native windowed turn-3 run).
- `macos-window-fixed.log` preserves the earlier culture-linked-start crash.
- `source-only-validation.log` records all three native tests built without game code.

## Failures found and repaired

1. **64-bit pointer conversion in Miles audio.** Two static strings and ASI
   handles were host allocations returned to the guest. The build overlay puts
   guest-visible objects in the guest allocator. Synthetic decoding tests cover it.
2. **INI updates were invisible.** The runtime appended new copies of a section
   but the reader stopped at the first section. Updates now replace keys in the
   existing section, preserve unrelated data, and atomically replace the file.
3. **Cocoa event polling from the audio worker.** Actual windowed playback raised
   `nextEventMatchingMask should only be called from the Main Thread`. Polling
   and presentation are now restricted to the SDL owner; worker title/cursor
   requests are coalesced for that thread.
4. **Incomplete local game data.** The original installation lacked startup
   cursor animation files and one music track. These were repaired only in the
   private test copy, as detailed below. Setup now checks these dependencies.

**Remaining game bug / workaround:** Quick Start can still crash with culturally
linked starts enabled, including after the INI fix. Instrumentation identified a
civilization ID of `-1` in the optional culture-group comparison, causing a read
before the rules allocation. The observed failures depend on whether the preceding guest page is mapped;
other runs reached gameplay despite the same invalid lookup. Fresh setup now disables only this optional rule (`0x40`).
The game algorithm itself has not been patched; re-enabling the option is unsafe.
This does not prove every world seed or civilization is safe.

## Private asset provenance and original-install safety

The test installation is an APFS copy-on-write copy under `game/`, not a link
back to the user's installation. The original executable, saves and preferences
were not edited. The supported `.exe.orig` was restored as `.exe` only in the
private copy. Imported preferences were retained in timestamped `work/` backups.

The missing cursor FLC/INI came from the user's already-downloaded **Erlend's
Interface Improvements 1.5** archive, by Erlend Philip Wood. Archive SHA-256:
`a517be9bebf1665856f508d3f5eb6335beff96ec3fe168fe4922980bfb2f1d1d`.
Both extracted members were also checked against the earlier project's hashes.
This is a **community cursor replacement**, not a pristine-stock Steam asset test.
Only the two missing files were extracted; no interface overhaul was applied.
The missing `AncGRfull.mp3` was copied from another existing local installation.
The detailed source paths and hashes are in ignored provenance JSON files.

No game assets, game-derived C, proprietary binaries, fonts or saves are
committed or distributed with this fork.

## Not yet established

- Intel/x86_64 compilation or gameplay, older macOS releases, or universal builds.
- Long campaigns, all civilizations/scenarios, multiplayer, or Windows conformance.
- Save/load round-trips on this port (existing upstream coverage is not Mac proof).
- Optimized release performance, signing/notarization or standalone app packaging.
- Manual keyboard/mouse interaction beyond the host's scripted input path.
- The game still emits upstream's Steam-initialization warning in its log; the
  verified native runs nevertheless proceeded to the menu and game.

The launch scripts use this checkout and Homebrew libraries. They are not a
self-contained redistributable game or signed application bundle.
