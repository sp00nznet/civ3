# macOS runtime stability and repeatable app builds

Verification on an Apple Silicon Mac, November 4, 2026. This is a native
recompilation host, **not** a claim that every Civilization III campaign, mod,
multiplayer mode, or Intel Mac is now tested.

## Guest CPU-state isolation

The original portable host serialized the eight general-purpose registers and
some x87/MMX state across guest threads. It did not restore several parts of
the lifted CPU context: SSE XMM registers and MXCSR, lazy arithmetic flags,
GS base and six segment selectors. A thread switch or nested guest callback
could therefore expose state left by a different guest thread.

The build-directory-only `pcrecomp-context-state.patch` saves/restores those
fields whenever the machine is given to another guest thread or a nested
callback restores its caller's context. Each new thread receives the standard
SSE control word. The pinned upstream `../tools` checkout is not modified.

`macos_runtime_state` exercises nested callbacks, nonblocking snapshots,
fresh thread state and 4,000 coordinated handoffs with distinct register
patterns. This is targeted correctness coverage, not proof of complete CPU
instruction or Windows threading emulation.

## Exact-key function lookup cache

The immutable generated guest dispatch table is still authoritative.
`pcrecomp-dispatch-cache.patch` inserts a 256-slot, per-native-thread exact
address cache in front of its binary search. It caches misses as well as hits.
Imports are **not** cached through the dynamic shim resolver, whose current
call state needs to be updated every time.

The synthetic test checks every entry in an 8,194-entry table, neighboring
misses, hash collisions, dynamic imports, and four concurrent lookup threads.
The optional benchmark invokes five million guest lookups per process.

Five alternating runs on the same Apple Silicon machine measured:

| Synthetic dispatch lookup | Median CPU time |
|---|---:|
| Previous uncached runtime | 0.154214 seconds |
| With exact-key cache | 0.034813 seconds |

The ratio is **4.43x faster for this lookup workload**. These are process
CPU seconds, **not** whole-game FPS, AI-turn speed, or battery life estimates.
Cache size and collision behavior were chosen for correctness and modest TLS
usage; different call patterns may have different outcomes.

## Real game and safety checks

The updated runtime passed **20 native tests and 84 Python tests** on the local
Apple Silicon Mac. All 20 native tests also passed in a source-only CMake build
without commercial game code or assets. A synthetic runtime-state test covers
4,000 handoffs; the dispatch-cache test covers all entries in its large
synthetic table, four threads, cache collisions, misses and dynamic imports.

A private compressed save -> process exit -> fresh-process reload test restored
**turn 2 and advanced to turn 7**, with new autosaves and the source save's
SHA-256 unchanged. The Persian small-map, culture-linked Quick Start test
also passed, reaching **turn 2** in a later run performed alongside compilation
(and turn 4 in an earlier independent pass). The actual game's main menu
opened from the Desktop .app, using no Terminal window.

For an additional screen/CPU regression check, a single **eight-second**
headless menu pair consumed about **1.211 CPU seconds** on an older baseline
binary versus **1.019 seconds** on the updated binary. The menu framebuffer
hashes were byte-identical. This single comparison includes other intervening
rendering fixes; **it is not evidence of a reproducible 15.9% whole-game speedup
or a cache-only effect**.

Both game test runners use independent APFS copies, so normal saves and
preferences are not changed.

QA scripts now **remove their generated game copies and test saves by default**
after logging results and recording screenshots. This prevents successive
regressions from accumulating tens of gigabytes of obsolete copies. To retain
the copies for a failing reproduction, pass `--keep-test-games`. Cleanup
refuses symlinked clone directories, traversal names, and non-directory
targets; it never deletes the source game directory.

```sh
python3 tools/macos_roundtrip.py --seed-save "game/Conquests/Saves/Auto/Conquests Autosave 3900 BC.SAV"
python3 tools/macos_newgame_test.py persian-small
python3 -m unittest discover -s tests -p 'test_macos*.py' -v
ctest --test-dir build-macos --output-on-failure
```

## Source-controlled clickable Mac app

The Desktop `Civilization III.app` is a small Swift/AppKit wrapper around
your existing native executable. It does **not** bundle game data or build
binaries. The launcher/icon source is now stored in `src/macos_launcher/`,
rather than being only a disposable local `work/` artifact.

Build a signed local launcher under `dist/`:

```sh
./script/build_macos_app.sh
```

To replace an existing launcher on the Desktop after compiling and verifying
the new bundle:

```sh
./script/build_macos_app.sh --install-desktop
```

The installer only overwrites a bundle with the expected application
identifier and attempts to restore an existing version on failure.
The app automatically locates your project in common home-directory paths
or opens a folder chooser. It starts `build-macos/civ3` directly (no Terminal
window or rebuild on each launch) and writes logs to `work/Civ3-Launcher-*.log`.
It has an ad-hoc local signature, not a Developer ID or notarized release.

**Remaining limitations:** full-campaign and multiplayer correctness,
optimized generated-code builds, arbitrary mods, standalone commercial-game
packaging, and actual gameplay on Intel Macs still need validation.
