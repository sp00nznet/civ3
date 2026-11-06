# DirectSound memory lifetime, input bounds and synchronization

Verified locally on November 4, 2026, on an Apple Silicon Mac with the native
64-bit Civilization III host. The tested game executable SHA-256 is
`5c02d97fce8de46e0ffc58ee206bc009f97fa5effc1bd8d12e56ee08ca5b389d`.
The compatibility overlay is `patches/pcrecomp-dsound-lifetime.patch`;
the pinned upstream pcrecomp checkout was not modified.

## Reproduced failures

The old DirectSound implementation could fill all 256 buffer slots, after
which each failed `IDirectSound::DuplicateSoundBuffer` call allocated a new
native wrapper, incremented the shared PCM allocation's refcount, and then
returned without undoing either action. After releasing all *real* handles,
the shared guest sample block could remain allocated indefinitely.

The old code also accepted zero-length secondary buffers and invalid stream
rates/channels. Its audio callback uses the buffer length and channel count
to index sample data, so those invalid inputs were unsafe. Setting a buffer's
format or changing master gain could race with the audio callback.

Before applying the repair, the new synthetic test reported failures for
native-heap growth, sample-buffer lifetime, zero-byte buffers and invalid
stream formats, and then terminated with a segmentation fault. This
demonstrates a reachable defect in the synthetic test; it is **not proof
that a user's game previously crashed on that path**.

## Corrections

- A rejected duplicate operation now clears its output handle, releases
  the attempted native wrapper and reverses the shared PCM reference.
- Creating a buffer checks native/guest allocation failures before use and
  refuses a zero-byte secondary buffer.
- Stream creation validates its rate and mono/stereo channel count, checks
  ring-allocation failure, and avoids pushing after a stream has closed.
- A buffer format change validates PCM tag, channels, sample bits, block
  alignment and positive sample rate *before* updating live data, under the
  existing audio mixer mutex.
- Master gain is written under that mutex. The callback snapshots a
  consistent gain before processing its output buffer.

All changes are limited to an overlay applied to the Mac build-directory
copy of `dsound.c`; the underlying game executable, assets and primary saves
remain unmodified.

## Measurements and regression coverage

The synthetic DirectSound COM test fills all 256 slots, executes thousands
of failed duplicates, checks both the native allocator and the small guest
sample allocation, releases every valid handle, then confirms slot reuse.
It additionally tests ordinary sound-buffer create/release cycles,
format validation and host-stream open/push/close lifecycle.

| Tested allocation path | Native malloc in-use growth |
|---|---:|
| 4,096 rejected buffer duplicates with all slots occupied | **0 bytes** |
| 2,048 ordinary buffer create/release cycles | **0 bytes** |

These measurements come from `malloc_zone_statistics` in one Apple Silicon
test process. They establish that *these tested operations* no longer grow
native heap allocations; they do not mean the whole game is leak-free.

The patch passed all **21 native tests and 91 Python tests**. All 21 native
tests also passed in a separate source-only CMake build with no commercial
game assets or lifted game code.

Actual-game checks on the updated build:

- Native menu boot: audio initialized through CoreAudio and the menu appeared.
- Save -> exit -> fresh-process reload: restored turn 2 and reached turn 7,
  generating five fresh autosaves. The original seed save was unchanged.
- Culture-linked Persian small map: advanced to turn 4 with normal
  game and asset loading. Its temporary test game/saves were discarded.

## Menu CPU and framebuffer regression check

A three-pair, alternating-order macOS headless menu check spent eight seconds
at the menu for each run and compared a frozen older executable against the
updated sound-lifetime build. The median process CPU times were **1.008022 s**
for the baseline and **1.010556 s** for the updated build; the difference
was +0.25%, within normal run-to-run variation. Maximum observed RSS was
approximately 143.65 MB vs. 143.49 MB, respectively. **Every recorded menu
framebuffer had an identical SHA-256**.

These measurements provide evidence against a visible menu rendering
regression or obvious menu CPU penalty. They do not isolate the cost of
individual DirectSound changes, measure AI turn duration, or demonstrate an
overall gameplay speedup.

The A/B tool automatically removed its disposable game copies and preserved
only logs, JSON results and framebuffer snapshots.

## Reproduction

```sh
./build-macos.sh
ctest --test-dir build-macos -R macos_dsound_lifetime --output-on-failure
SDL_AUDIODRIVER=dummy build-macos/tests/civ3_dsound_lifetime_test
python3 -m unittest discover -s tests -p 'test_macos*.py'
```

For source-only tests, use `-DCIV3_RUNTIME_TESTS_ONLY=ON` with the pinned
pcrecomp checkout. The test uses synthetic PCM and the SDL dummy device.

## Remaining stability and performance limits

This patch fixes measured memory-lifetime and format-input defects. It does
not optimize AI turns, establish a general FPS increase, prove every sound
effect is indistinguishable from Windows, or audit every SDL/FFmpeg allocation
in a full campaign. The broader single-player matrix is in
[macos-singleplayer-coverage.md](macos-singleplayer-coverage.md).
