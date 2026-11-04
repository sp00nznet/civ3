# macOS validation ledger

Local verification on **November 3, 2026**, not a claim of complete game compatibility.

## Latest performance and stability pass

The final executable for this pass is
`c08b55adf7c2827b5d6ad158c15e29644f82f6a7a93bd448faff275e78061881`.
**74 Python tests and 10 native tests passed**, including a clean source-only
native build. The prior measurements below remain historical evidence for the
versions named in their individual reports.

The latest build reduced measured main-menu CPU time by **89.07%** across three
headless runs per version and **85.33%** in one actual native-window pair, with
identical menu framebuffers. Graphics microbenchmarks are faster, but no whole-game
FPS multiplier is claimed. Saved/reused device-context failures were reproduced
and fixed before old display buffers were freed.

The same final executable completed compressed-save reload from turn 3 to turn 8,
a culture-enabled Persian small-map game through turn 4, and a 57-frame native
window recording. Targeted framebuffer memory/undefined-behavior/thread sanitizer
checks passed. Full scope, exact measurements, limitations and retained evidence:
[performance and framebuffer stability](macos-performance.md).

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
| Setup, asset preflight, preference backup and smoke-verdict Python tests | 69 passed |
| Miles guest-memory regression | Passed five synthetic MP3 open/decode/close cycles |
| INI regression | Passed repeated key updates, insertion, deletion, case-independent sections, duplicate repair, failure and zero-size-buffer checks |
| File-sharing regression | Passed bidirectional read/write sharing, inode aliases, no truncation on denial, mapped-byte preservation, and restriction release on close |
| SDL ownership regression | Passed worker pump/draw/cursor/title checks; title changes are deferred until the owner pumps |
| Synthetic-tests-only CMake build | All seven native regression tests passed from a source-only staging directory with no game assets or generated game code |
| Main executable import binding | 245 imports, zero unresolved |
| Renderer / sound imports | 121 / 136 imports, zero unresolved |
| Main menu | Reached; valid 1024x768 PPM, PNG visually inspected |
| Headless gameplay, sound disabled | Quick Start, founded Rome, progressed to turn 4 / 3800 BC; screenshot visually inspected |
| Headless gameplay, audio enabled, INI fix applied | Quick Start and turn 2; expected watchdog exit 4 |
| Native window | Cocoa window registered on screen, 1280-pixel width; direct window screenshot blocked by the Mac's capture permissions |
| Windowed gameplay, audio enabled, culture-linked starts disabled | Completed Quick Start, founded Rome, and reached turn 3 / 3850 BC; framebuffer visually inspected; expected watchdog termination |
| Compressed manual save -> fresh-process reload | Automated two-process test restored turn 3 exactly and played to turn 8, producing five new autosaves; source and loaded save hashes unchanged |
| Older saved-game compatibility | A private copy of the existing Persian 330 BC save loaded at turn 111 and continued to turn 112 / 310 BC; framebuffer visually inspected; original file SHA-256 unchanged |
| Unchanged native build | Patched source and executable timestamps unchanged; measured 0.153 seconds on the final rebuilt version for a no-op configure/build on this host |

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

Follow-up evidence:

- `macos-roundtrip-verified/result.json`, `save.log`, `reload.log`, and their
  framebuffers record the fresh-process round trip and exact tested binary hash.
- `macos-followup-python-tests.log`: 36 Python tests.
- `macos-followup-native-tests.log`: all four native regression tests.
- `followup-source-only.log`: all four tests built without commercial game data.
- `macos-final-noop-build.json`: final no-op build timing and unchanged file timestamps.
- `macos-legacy-save-verified/result.json`, `loaded.log`, `loaded.ppm`: the older
  Persian save restored and continued on the final rebuilt executable.

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

5. **Compressed saves could not reload.** The HLE ignored `CreateFileA` sharing
   restrictions and allowed a second decompression to truncate `save0.tmp`
   while the first save was still mapped. The independent and lifted decoders
   agreed byte-for-byte; file-sharing checks fix the actual I/O problem without
   replacing the codec or altering saves. See [full findings](macos-save-load.md).

**Culture-linked startup:** the prior required workaround now has an experimental
repair. Two stale leader-identity reads are resolved from pending choices and
validated before indexing the rules. Three culture-enabled new-game configurations
passed initial turns; the conservative setup default remains off. See
[full findings and limitations](macos-culture-starts.md). Exact placement equivalence,
unusual scenarios and multiplayer remain unverified.

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

## Launch and recording reliability follow-up

The latest pass adds no-clobber shell-free recording, bounded encoder shutdown,
recording finalization on interrupt, strict timing/input validation, and
non-destructive duplicate-launch protection. **69 Python and seven native tests
passed**, including all seven native tests from a source-only staging directory.
Targeted safety helpers also passed address/undefined-behavior sanitizer checks.

The rebuilt executable completed another compressed-save round trip from turn
3 to turn 8, with five new autosaves and unchanged source data. Real FFmpeg
captures covered quoted/punctuated filenames, existing-output protection,
explicit overwrite, encoder/output failures, and SIGINT/SIGTERM finalization.
Two consecutive runs of the original exit/recording play-test also passed.
See [the detailed ledger](macos-reliability.md) for the exact binary hash,
limitations and local evidence paths.

## Not yet established

- Intel/x86_64 compilation or gameplay, older macOS releases, or universal builds.
- Long campaigns, all civilizations/scenarios, multiplayer, or Windows conformance.
- Broad save compatibility across every scenario, mod and game version; the
  successful compressed-save round trip is described above, not a universal claim.
- Optimized release performance, signing/notarization or standalone app packaging.
- Manual keyboard/mouse interaction beyond the host's scripted input path.
- The game still emits upstream's Steam-initialization warning in its log; the
  verified native runs nevertheless proceeded to the menu and game.

The launch scripts use this checkout and Homebrew libraries. They are not a
self-contained redistributable game or signed application bundle.

## Additional culture-start and extended QA pass

- Final repaired build: tiny culture-linked game to turn 4; selected Persia on
  small to turn 4; standard to turn 2. A second tiny test through the reusable
  tool reached turn 3. All three matrix framebuffers were visually inspected.
- Repeated compressed-save round trip on that exact build restored turn 3 and
  continued to turn 7; source and loaded-save hashes stayed unchanged.
- Before the repair, a frozen copy of the previously verified executable ran for
  five minutes to turn 19, and a separate UI run showed Domestic/Science advisors,
  Civilopedia and Preferences before playing to turn 11. Those are baseline tests,
  not mislabeled results from the final repaired binary.
- 54 Python tests and five native tests pass, including all five from a source-only
  staging directory. The culture selector covers 961 player/human combinations.
- Final-build evidence, exact binary hashes, reproducer commands and remaining
  limitations are recorded in [macos-culture-starts.md](macos-culture-starts.md).

The final repaired executable also completed a windowed culture-linked Persian
game to turn 4 / 3800 BC with audio enabled. CoreGraphics reported the native
Cocoa window, and the final game framebuffer was visually inspected. Evidence:
`work/macos-culture-window/`.


## Main consolidation and bitmap allocation follow-up

All existing fork branches are merged into `main` with their history preserved.
The merge's source-only GitHub checks passed on both `macos-15` (Apple Silicon)
and `macos-15-intel`; this verifies runtime/host fixtures, not commercial gameplay
on Intel. The distinct early-branch host tests are now included in the suite.

The subsequent bitmap repair passed **78 Python tests and 16 native tests**
locally. The actual game saved/reloaded from turn 3 through turn 9 / 3550 BC,
and a culture-enabled Persian game reached turn 4. See
[bitmap scope, evidence and sanitizer limitation](macos-bitmap-safety.md).


## Packed pixels and bounded scalar rendering follow-up

On `main`, the 24-bit guard-page read crash and packed-format read/write failures
were reproduced and repaired. The local suite passes **78 Python tests and
18 native tests**, including 311,492 pixel-helper assertions and 1,500 independent
scaling comparisons. Focused ASan/UBSan checks passed for the dependency-free
helper only. Save/reload progressed from turn 3 to turn 8; a culture-linked
Persian game reached turn 4, and both game framebuffers were visually inspected.
The native-window menu remained byte-identical to the preceding build.
See [exact build, tests and limitations](macos-pixel-safety.md).
