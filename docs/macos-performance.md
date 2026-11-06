# Profile-guided performance and framebuffer stability

Verified locally on **November 3, 2026**, on the Apple Silicon Mac described in
[the validation ledger](macos-validation.md). This pass reduces main-menu CPU
work and accelerates a specific graphics-transfer path. It does **not** establish
a whole-game frame-rate multiplier, faster AI turns, longer battery life, or
complete campaign stability.

## Exact builds compared

- Baseline: the preceding verified port, commit `67f344e`, executable SHA-256
  `d21322e100bd341b660c2e45219559a4e797d92d00d98a4352ea203eb760f996`.
- Updated executable SHA-256:
  `c08b55adf7c2827b5d6ad158c15e29644f82f6a7a93bd448faff275e78061881`.

The generated game/DLL code remains at the tested `-O0` setting. Only the
profiled GDI device-context implementation is explicitly compiled with `-O2`.
The thread scheduler has not been globally optimized or replaced. No fast-math,
JIT, Wine, Rosetta, original-binary modification, or save conversion was added.

## Idle-menu CPU measurements

The baseline menu repeatedly polled an empty message queue, measured text and
searched function dispatch tables. A host-only pacing rule now pauses for **2 ms**
after an empty, unfiltered `PeekMessageA` call on the original UI thread while the
main-menu flag is set. It releases the guest-machine lock during the pause.
Queued-message results, filtered polls, `PM_NOYIELD`, worker threads and gameplay
are not paced. Input arriving during a pause may wait for that short pause to end.
This is an explicit host policy, not a claim that Win32 requires such a delay.

`tools/macos_performance.py` uses independent APFS game copies and the same save.
It alternates baseline/candidate run order and uses `wait4` to collect each game
process's own CPU usage, not that of the profiler, compiler or sibling processes.
Each run spends 12 seconds at the menu; CPU totals also include startup and exit.
The checker rejects failed runs or different framebuffer hashes.

| Measurement | Baseline CPU time | Updated CPU time | Reduction |
|---|---:|---:|---:|
| Headless menu, median of three runs per build | 12.707650 s | 1.388388 s | **89.07%** |
| Actual native window, one baseline/candidate pair | 12.828700 s | 1.882179 s | **85.33%** |

All compared menu framebuffers were byte-identical. The headless median elapsed
times were 12.670 s and 12.668 s: this is less CPU work over the same interval,
**not** an 89% faster startup. The native-window result is a single pair, not a
three-run median. These are local measurements, not guarantees on other Macs.
Peak RSS was about 140-143 MB headless and 211 MB windowed; no general memory-use
reduction is claimed from these short runs.

The menu delay is configurable without changing preferences:

```sh
./script/build_and_run.sh                       # default: 2 ms, main menu only
./script/build_and_run.sh --menu-idle-ms 0       # disable pacing for comparison
```

The accepted range is 0-16 ms. The default remains zero on non-Mac POSIX hosts.

## Graphics-transfer measurements

The GDI blitter previously performed generic per-pixel work even for an ordinary
full-size copy. The new path clips once, then copies complete rows or expands
RGB555/RGB565 directly into 32-bit pixels. It is limited to non-stretched,
non-overlapping true-color transfers. Palette operations, stretching and aliased
storage keep the existing scalar behavior. Negative pitches and clipped edges
are supported. Overlap falls back conservatively; this does not claim to repair
all historical Windows raster-operation or overlap semantics.

Five alternating measurements per version, each performing 40 transfers of a
1024x768 surface through the actual HLE API, produced these medians:

| Synthetic transfer | Baseline | Updated | Speedup in this operation |
|---|---:|---:|---:|
| 16-bit to 32-bit | 0.337772 s | 0.021742 s | **15.54x** |
| 32-bit to 32-bit | 0.198637 s | 0.001659 s | **119.73x** |

Output hashes matched in every comparison. These hot-buffer microbenchmarks are
**not whole-game FPS figures**. The improvement includes both the specialized
path and optimization of the GDI implementation; it does not isolate their
individual contributions.

The native graphics regression checks all 65,536 possible 16-bit values for
**both** RGB555 and RGB565, plus cropping, clipping, negative source pitches,
stretch fallback and overlapping-storage fallback.

## Stability repairs

### Framebuffer replacement and saved device contexts

The old host leaked each replaced display buffer. Simply freeing it would have
introduced a use-after-free: `SaveDC` snapshots still retained the old window
pixels, and a reused DC slot could restore an unrelated old snapshot.

The saved-DC regression failed on the preceding runtime. The repair refreshes
both active and saved window DCs before releasing an old framebuffer and clears
saved state when DC slots are released/reused. The replacement allocator checks
geometry, leaves the existing display intact on failure and reuses an unchanged
mode without another allocation.

The DC-lifetime patch is also staged for the shared Linux POSIX host, which uses
the same framebuffer replacement code. The Mac performance path remains
Mac-only. Linux compilation/gameplay has **not** been retested in this pass.

### Consistent screenshots and bounded watchdog reads

Screen-change detection, scripted state waits and snapshots now read under the
guest-machine lock. Main-menu publication uses a C atomic. Recording and
screenshots copy coherent pixel buffers before doing potentially slow output.
The PPM writer batches a row at a time and atomically replaces its destination;
a failed write returns failure rather than pretending a screenshot exists.

A new nonblocking machine-lock entrypoint lets the Mac watchdog bound its state
snapshot attempt to 250 ms. A guest stuck holding the lock cannot indefinitely
block the watchdog merely because it was asked to capture evidence. When a
requested snapshot cannot be obtained or written, the run reports failure code 5.
This does not claim that every runtime data race or deadlock has been eliminated.

## Completed verification on the updated executable

| Check | Result |
|---|---|
| Python tests | **74 passed** |
| Native tests | **10 passed**, also from a source-only staging directory without commercial assets or generated game C |
| Saved/reused DC regressions | Reproduced failures before repair; passed afterward |
| Framebuffer lifetime | Invalid geometry, 500 replacement cycles, independent snapshots, exact RGB bytes and 1,000 synchronized writer/reader iterations passed |
| Nonblocking guest snapshots | 10,000 contended attempts, register preservation, nested acquisition and reacquisition passed |
| Focused framebuffer sanitizers | AddressSanitizer + UndefinedBehaviorSanitizer and a separate ThreadSanitizer run passed |
| Manual save -> process exit -> reload | Restored turn 3 and played to **turn 8 / 3600 BC**, producing five fresh autosaves; source unchanged; final game framebuffer visually inspected |
| Culture-linked Persia, small map | Passed through **turn 4 / 3800 BC** with the rule still enabled; original preferences unchanged |
| Actual native window and recording | OS window registration confirmed; **57-frame, 1024x768 H.264** capture decoded; menu framebuffer visually inspected |
| Invalid screenshot directory | Reported `[shot] FAILED` and exit **5** |

Sanitizer coverage is limited to the framebuffer helper, not the entire game.
LeakSanitizer is unavailable on this Mac, so `detect_leaks` was disabled; no
LeakSanitizer success is claimed. Existing saved-game protections and the
experimental culture-start repair remain in place.

## Reproduction and retained evidence

```sh
python3 -m unittest discover -s tests -p 'test_macos*.py' -v
ctest --test-dir build-macos --output-on-failure
./build-macos/civ3_gdi_perf_test --benchmark
python3 tools/macos_performance.py \
  --baseline /path/to/prior/native/civ3 \
  --candidate build-macos/civ3 \
  --seed-save "/path/to/an/owned/save.SAV"
# Add --windowed to measure actual native windows instead of headless rendering.
```

Local logs and private test copies remain in ignored `work/macos-performance-v1/`:
`final-ab/result.json`, `windowed-ab/result.json`, `gdi-benchmark.json`, the two
profiler samples, `roundtrip/result.json`, `culture-newgame/result.json`,
`capture/result.json`, `source-only-final.log`, and the sanitizer/test logs.
The initial profiler's cumulative-child RSS includes the profiler itself and is
not used for the game-memory measurements above.

No game assets, original binaries, generated game code, fonts, saves or recordings
are committed. Long campaigns, multiplayer, Intel Macs, all scenarios and broad
optimized-release performance remain unverified.
