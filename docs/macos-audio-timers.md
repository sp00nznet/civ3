# Apple Silicon audio: timer deadlines and committed PCM

The user continued to hear broken gameplay audio after the WAV gain mitigation.
The following changes repair two compatibility-layer defects. They do not
establish that the audible problem is resolved.

## Timer callback bursts

The original periodic WinMM worker advanced its deadline by one period after
each callback. It only discarded deadlines when more than one second behind.
When a 30 ms sound callback waited for the guest machine or took several
periods to finish, the worker could therefore deliver overdue callbacks without
sleeping between them. A retained first-turn trace recorded 13 short-interval
callbacks; an experimental skip run recorded zero. A later full-volume run
still recorded three short intervals, so those two trace counts alone do not
prove uniform callback spacing.

`patches/pcrecomp-winmm-timer-schedule.patch` now selects a future deadline
after the callback returns. If the next nominal deadline has arrived, it
schedules one complete interval from the current monotonic time. Ordinary
callbacks retain the existing cadence. A small delay can still result in a
shorter subsequent interval; this patch prevents replaying overdue deadlines,
not all timing jitter or guest-machine contention. One-shot behavior and
cancellation remain on the existing paths.

The patch applies only to the Mac build-directory copy of `winmm.c`, after
the WAV overlay. The pinned toolkit checkout is unchanged. The pure scheduler
test covers ordinary cadence, exact deadlines, zero periods, clock rollover,
repeated stalls and 200,000 randomized cases. A separate test invokes the real
`timeSetEvent` worker with synthetic guest callbacks that deliberately take
120 ms. It fails on the old worker with two immediate catch-up callbacks and
passes with the patch. It also exercises self-cancellation and a one-shot.

## DirectSound writes and playback

The old SDL mixer read guest-writable PCM directly, concurrently with game
writes between `IDirectSoundBuffer::Lock` and `Unlock`. A separate native PCM
snapshot now becomes available to playback only when `Unlock` commits the
returned regions under the mixer mutex. A wrapped write commits both pieces
under one lock. Duplicate buffers share the snapshot and free it with the last
reference; failed allocations and buffer-slot exhaustion release it.

The synthetic test checks pre/post-Unlock visibility, wrapped writes, invalid
pointers and lengths, duplicate-buffer ownership, and 2,000 full-buffer wrapped
commits against a concurrent snapshot reader. The existing lifetime stress
test continues to cover exhausted buffer slots and repeated allocation cycles.
The snapshot reader hook is compiled only into test fixtures.

## Full-volume diagnostic and shipping settings

The earlier temporary full-volume run completed turn 3, but the existing guard
reported incoming RMS 0.728 and 32.2% clipped frames. Thus, atomic playback
snapshots and skipping overdue callbacks were insufficient to eliminate the
original-volume distortion. The 5% WAV input mitigation and existing output
limiter remain the playable defaults. No new volume reduction is introduced.

Temporary timer tracing and the ad hoc `CIV3_DIAG_ORIGINAL_WAV` bypass are
removed by regenerating the build-directory overlays. Original-volume checks
use a separately linked diagnostic executable with
`CIV3_WAV_INPUT_DIVISOR=1` and `CIV3_SFX_OUTPUT_GAIN=1`, without replacing the
playable executable. Campaign checks accept `--binary` to select that isolated
executable. SDL's dummy or disk audio device keeps these automated checks off
the physical speakers; digital results do not establish audible quality.

## Reproduction

```sh
./build-macos.sh
ctest --test-dir build-macos --output-on-failure
python3 -m unittest discover -s tests -p 'test_macos*.py'
cmake -S . -B work/audio-source-only -G Ninja -DCIV3_RUNTIME_TESTS_ONLY=ON
cmake --build work/audio-source-only --parallel 3
ctest --test-dir work/audio-source-only --output-on-failure
```

For game runs, `tools/macos_newgame_test.py` and
`tools/macos_campaign_soak.py` use private APFS copies and preserve the source
preferences and saves. Retained diagnostic logs and manifests live under
`work/sound-timer-diagnostics/` and are not distributed with source.

## November 5 verification

The production and source-only native suites each passed **28 tests**, and the
Python suite passed **97 tests**. The scheduler helper passed AddressSanitizer
and UndefinedBehaviorSanitizer. The real timer, snapshot and lifetime tests
also passed both sanitizers in a separate synthetic build, with these test-only
adjustments:

- Guest memory used `0x200000000000` because ASan's shadow memory occupies the
  normal `0x100000000000` mapping. The ordinary suites use the production base.
- `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` let the installed SDL2 compatibility
  library find SDL3 under the sanitizer loader. A minimal linked program
  reproduced the earlier startup failure before game code ran.
- The lifetime test queries ASan's live-allocation counter when instrumented;
  Darwin malloc-zone counters include sanitizer bookkeeping and gave misleading
  growth readings. Its leak thresholds remain unchanged. Ordinary builds still
  use the Darwin counters. No sanitizer checks were disabled for the passing run.

The clean original-volume first-turn run completed turn 2 but triggered the
distortion guard with RMS **0.732** and **37.7% clipped frames**. This is a sound
failure despite a passing gameplay verdict. A 130-second original-volume
campaign reached turn 114 from 111 and produced three autosaves, falling short
of the required five-turn advance; it is retained as an incomplete game check.

With the existing playable gain settings, the first-turn repeat completed
**turn 4** without a guard transition. An earlier attempt reached only the
opening tutorial at turn 0 because gameplay inputs arrived during world
generation. `--generation-wait 60` delayed those inputs and extended the run
equally, without relaxing the gameplay verdict. The separate **195-second**
playable campaign resumed turn 111 and reached **turn 121**, with five
autosaves, a safe final snapshot and no guard transition.

SDL disk output records the actual mixed stereo S16LE stream at 44,100 Hz
without using the speakers. The final 20 one-second windows of the passing
first-turn run all contained nonzero audio and had **zero hard-clipped output
samples**. These digital measurements do not establish that effects are correct,
that the sound is pleasant, or that the original defect is resolved.

The playable campaign's last 20 windows also all contained nonzero output.
Its highest one-second hard-clipped sample fraction in that tail was **0.0816%**,
so this is not a claim of perfectly unclipped output. The incoming sustained
clipping guard did not transition in either passing playable test. RSS in the
campaign peaked near **107.5 MiB** and ended near **105.9 MiB**; this bounded
observation does not establish that the game is leak-free.

A longer **195-second original-volume campaign repeat** passed from turn 111
to **turn 119**, produced five autosaves and captured a safe final frame without
a guard transition. This does not supersede the reproducible full-volume
first-turn distortion: the existing campaign exercises different audio content.

The final SHA-256 manifest comparison found **zero changes across all 8,989
original game files**, including saves and preferences. Test game copies were
removed by the QA tools; logs, screenshots and digital audio remain local.
The production executable SHA-256 is
`038ac26a47a6a9d0abe10c682c9b60b30b4f27dd22f063dc201d1190e909a52d`.

## Remaining investigation

The first-turn guest PCM still clips at original WAV amplitude. The next
debugging target is how the lifted `sound.dll` mixer starts, advances and sums
WAV voices into its gameplay ring, using the reproduced first-turn case. The
timer and snapshot corrections should remain fixed while comparing that path;
there is no evidence yet that restoring full WAV volume is safe for normal
playback. A user listening through the Mac's speakers is still needed to assess
the audible result of the mitigated build.
