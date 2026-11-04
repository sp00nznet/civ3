# Native macOS port

This fork adds a **native 64-bit macOS host**, using the upstream pcrecomp
`win32hle` based-memory runtime. The game's 32-bit addresses stay inside a
separate guest-memory mapping; they are not truncated native Mac pointers.
No Wine, Windows VM, Rosetta, or JIT is used by this path.

**Status: experimental.** Native runtime and setup tests pass on Apple Silicon; the main menu and an initial multi-turn game have rendered successfully.
Full-game validation is tracked in `macos-validation.md`. Intel compilation,
multiplayer, long campaigns, and distribution/signing are not yet verified.
The upstream Windows/Linux compatibility table is not a macOS test result.

**Culture-linked starts:** the Mac build now includes an experimental repair
for the uninitialized civilization lookup that caused the earlier startup crash.
Tiny, small (Persia), and standard culture-linked games have completed initial
turns. Setup still leaves the option **off by default**, and existing preferences
are not changed. Broader scenario and exact placement behavior are unverified;
see [the repair and test evidence](macos-culture-starts.md).

## Requirements

- A Mac with Xcode Command Line Tools, Homebrew, Python 3.10+, and Git.
- Native `cmake`, `ninja`, `pkgconf`, `sdl2`, `sdl2_ttf`, and `ffmpeg` libraries.
- Your own supported **Steam Civilization III Complete** installation.
  The executable SHA-1 must be
  `01f705b1ce6f5d1cfd5a861f88ca1413764f8019`.
  A matching `Civ3Conquests.exe.orig` backup is accepted when the normal
  executable is missing or patched. No original installation file is changed.
- Free disk space for a private game copy, generated C, and native build.

The pcrecomp revision is pinned in `.pcrecomp-revision`. Setup clones it into
`../tools` when that directory does not exist. For an existing toolkit checkout,
use `PCRECOMP=/absolute/path/to/checkout`. Setup refuses to reset another
checkout or discard its changes.

## Setup and launch

```sh
./setup-macos.sh --install-deps --game-dir "/path/to/Sid Meier's Civilization III Complete"
./script/build_and_run.sh
```

Homebrew installation occurs only with `--install-deps`. Setup creates a local
`.venv` with the versions in `requirements-macos.txt`; it does not change the
system Python environment. It stages an isolated APFS copy-on-write game copy,
so saves and settings made by the port stay in this checkout's `game/`.
On first setup, imported preferences are backed up under `work/` and replaced
with portable defaults. `--keep-settings` preserves them instead;
`--reset-settings` explicitly backs up and resets only the private copy.
A preflight catches known missing startup assets (cursor animation, one music
track, rules, labels, messages and font). This is not a full asset manifest.
An existing `game/` is reused. To switch installations, preserve/move that
folder first; `--force` regenerates code but does not erase your saves.

Setup also creates `Civilization III (recomp).command` for double-click launch.
The Codex Run action uses the same `script/build_and_run.sh` entrypoint.
The launcher depends on this checkout and the installed native libraries; it
is not a standalone, signed application bundle.

Rerunning setup reuses successful code-generation stages only when their
fingerprints and required output files match. The fingerprint includes the
input executable/DLLs, lifter, Python requirements, and pinned toolkit revision.
An incomplete stage or a lift with errors/stubs is not marked successful.
`./setup-macos.sh --check` performs read-only prerequisite and game checks.

## Build and debugging

```sh
./build-macos.sh
./script/build_and_run.sh --debug
./script/build_and_run.sh --logs
./script/build_and_run.sh --verify
```

Unchanged launches no longer rebuild patched runtime files: patch staging preserves
source and executable timestamps when their contents are unchanged. The launch
preflight checks the selected game copy and warns about the limited validation
of culture-linked starts without changing your preferences.

The initial bring-up build uses **`GEN_OPT=-O0`** for generated code to avoid
expensive optimization before correctness is established. This is a debug/
validation build, not a performance-tuned release. An optimized rebuild is:

```sh
GEN_OPT=-O1 JOBS=3 ./build-macos.sh
```

Use the same `GEN_OPT` when launching through the build-and-run script to avoid
switching the cached build back to the default. `JOBS` defaults to three to
limit compiler memory use. `BUILD_DIR` selects a separate build directory.

## Safe launching and recording

Rerunning `./script/build_and_run.sh` no longer terminates an earlier game.
Save and close it first. Kernel locks prevent duplicate launches against the
same writable installation, including when the binary is started directly.
A stale lock file does not require deletion. Separate private test installations
can run at the same time.

`--logs` creates a timestamped `work/macos-game-*.log` instead of overwriting the
previous log. `--verify` accepts `--game`, `--work` and `--exe` for another setup.

```sh
./script/build_and_run.sh --logs
./script/build_and_run.sh --record "work/A player's game.mp4" --fps 15
```

Video filenames are passed literally, without a shell. Existing recordings are
not overwritten unless `--overwrite-recording` is supplied. Recording failures
are reported; Ctrl-C and SIGTERM finalize the recording before exiting. Frame
rates must be 1–240, and malformed scripted input is rejected before startup.
See [reliability changes and test evidence](macos-reliability.md).

## Tests and evidence

```sh
python3 -m unittest discover -s tests -p 'test_macos*.py' -v
HLE_MEM_BASE=0x100000000000ull ../tools/runtime/win32hle/build_selftests.sh
ctest --test-dir build-macos --output-on-failure
python3 tools/macos_smoke.py
python3 tools/macos_newgame_test.py random-tiny
python3 tools/playtest.py --list
```

`macos_smoke.py` requires a main-menu milestone, a fresh framebuffer snapshot,
and the host's expected watchdog exit. A running process or a successful
`--help` is not enough. It does **not** establish complete gameplay support.
The existing play-test harness selects `build-macos/civ3` on macOS.

Logs, snapshots, catalogs, and generated sources remain under gitignored
`work/`, `build-macos/`, and `src/recomp/gen*` directories. Neither game assets
nor game-derived binaries/source are published with this fork.


### Save/load round-trip test

Use an owned save from turn 1 or later. This test uses two fresh game processes
and private copy-on-write installations; it does not alter the source save or
source installation:

```sh
python3 tools/macos_roundtrip.py --seed-save "/path/to/your-save.SAV"
```

It loads the seed, creates a compressed manual save through the game's Save
dialog, exits, reloads that new save in another process, then requires at least
three completed turns and three fresh autosaves. `result.json`, process exit
codes, logs, framebuffers and SHA-256 hashes remain under `work/roundtrip-*`.
An error dialog, a menu-only run or a missing framebuffer is a failure.
The scripted coordinates assume the game's 1024x768 interface. The seed must
load without extra scenario-specific dialogs. See [save/load findings](macos-save-load.md).

## Changes from upstream

- Allow a 64-bit Apple host without removing the 32-bit Windows/Linux checks.
- Use pcrecomp's guest-memory reservation/release API for the lifted DLLs.
- Translate guest addresses when resolving DLL export names.
- Avoid ELF-only linker options on macOS.
- Fix Miles audio strings/handles crossing the 64-bit host/32-bit guest boundary.
- Keep Cocoa/SDL polling and presentation on the window thread; defer worker
  cursor and title requests to that thread.
- Honor in-process `CreateFileA` sharing restrictions before truncating files,
  so compressed-save loading cannot overwrite its still-mapped temporary file.
- Fix INI updates so existing keys change in place instead of becoming invisible
  duplicate-section entries. Writes use a temporary file and atomic rename.
- Add pinned, resumable setup, native build and launch entrypoints, and tests.

The four runtime fixes patch **build-directory copies** of upstream files;
the pinned toolkit checkout remains clean. Their regression tests use only
synthetic INIs and a synthesized tone, never commercial game files.
They can be built independently of the game:

```sh
cmake -S . -B build-macos-tests -G Ninja -DCIV3_RUNTIME_TESTS_ONLY=ON -DPCRECOMP="$(cd ../tools && pwd)"
cmake --build build-macos-tests --parallel 3
ctest --test-dir build-macos-tests --output-on-failure
```

The portable memory allocator, Win32 implementation, SDL presentation/audio,
and FFmpeg integration are upstream pcrecomp components, not newly rewritten
in this fork.

## Culture-start build overlay

The default Mac build applies the checked culture-start overlay only to a
build-directory copy of the generated game code. It requires the full supported
lift and fails closed on changed instructions. See [details and limitations](macos-culture-starts.md).
