# Apple Silicon: preserving gameplay sound without the first-turn noise

This documents the **revised** audio mitigation for the native Civilization III
port. The previous first-turn SFX guard removed the harsh noise by muting the
software-mixed effects buffer, but that also muted music carried by the same
buffer after the main-menu music stopped. The user reported that all sound went
quiet. The revised approach aims to preserve both music and gameplay effects.

## Observed cause and experiments

The menu soundtrack uses an MP3 DirectSound buffer (flags `0x10080`). During
gameplay the game's old `sound.dll` software mixer also plays WAV assets,
including short ancient-era music fragments, into a continuously looped 64 KiB
effects buffer (flags `0x10088`).

Diagnostic builds showed that the effects buffer is repeatedly refilled but
that its *incoming* PCM samples progressively build to near-full-scale
clipping. Sound clipping therefore occurs **before** CoreAudio receives the
samples; changing speaker devices or clearing the DirectSound ring did not
prevent it.

Controlled tests silencing all **96 WAV files** observed during one early game
session prevented the runaway detector from activating. Two disjoint sets of
WAV files could independently reproduce the problem, so it was not a single
corrupt WAV. The original source files are valid PCM WAVs. Separately scaling
all 96 to **50%** and **25%** of their normal sample amplitudes still produced
sustained clipping during early gameplay. At **10%** source gain, the game
reached turn 4 without triggering the detector.

These experiments suggest runaway *overlapping voice gain in the legacy
software mixer*, although the exact faulty lifted game function is still
under investigation. This is **not proof of a complete fix to the original
mixer logic**.

## Default runtime correction

A Mac-only WinMM build overlay applies `civ3_wav_reduce_pcm` to WAVs loaded
into guest memory by `mmioOpenA`, before the legacy game mixer sums them:

- **5% input gain** for valid uncompressed, mono/stereo, 8- or 16-bit PCM
  WAV data. This safer default was selected from controlled 10% and 5%
  premix-gain tests. It prevents the observed first-turn saturation from
  reaching the guest effects PCM in the checked gameplay sessions.
- The small RIFF parser checks every chunk's length before writing; all
  96 observed game WAVs were recognized, and the original file bytes and
  headers are never modified.
- **3× effects-output gain** restores some of the perceived volume once the
  safer PCM has been mixed by the game. A source-only tested, smooth
  peak-limiter softens only the effects channel above 80% full scale,
  avoiding the extra output clipping that the old hard limiter introduced.
  The separate MP3 music buffer is untouched by these adjustments.
- The original distortion safeguard remains a *last resort*. Instead of
  going entirely silent, it leaves a **15% effects gain floor** when the
  incoming signal persistently clips. It restores full effects gain after a
  sustained quiet period.

This is implemented in the Mac-only overlays
`patches/pcrecomp-wav-input-gain.patch` and
`patches/pcrecomp-sfx-runaway.patch`, backed by
`src/linux/wav_pcm_gain.h` and `src/linux/sfx_guard.h`.
The pinned `pcrecomp` toolkit and original game installation are unchanged.
The playable Desktop launcher still invokes the same native `build-macos/civ3`.

For diagnostics, an advanced user can change the effects-output multiplier
using `CIV3_SFX_OUTPUT_GAIN` from **1.0 to 3.0**. The default is 3.0.
`CIV3_WAV_DIVISOR_OVERRIDE` can reduce WAV input level further (20–30),
  but does not permit a higher-than-tested gain.
`CIV3_SFX_GUARD_LOG=1` allows a test to log guard transitions. These
settings are cached before the SDL audio callback begins.

## Verification

The pure WAV parser test covers valid stereo 16-bit and mono 8-bit PCM,
preserves RIFF metadata, refuses malformed chunk lengths, and exercises
**50,000 malformed WAV headers**. This test passed AddressSanitizer and
UndefinedBehaviorSanitizer on macOS.

A real Apple Silicon game launched from an original, unchanged game
installation (using a private APFS copy), initialized the Mac audio stack
with a silent SDL device, reached **turn 4**, and did **not** log the old
first-turn distortion/suppression event with the 10% in-memory WAV gain.
The same was observed after restoring some effects volume through the 3×
output multiplier. A second controlled test at the chosen 5%-input/3×-output
combination completed turn 3, and its end-of-turn output checks measured
**0% hard-clipped samples**, with the expected nonzero audio signal.
This is a measurement of the digital stream before physical speaker playback,
not a human assessment of sound quality.

An earlier **195-second** private campaign using 10% premix gain and
a 3× output boost (prior to the smooth limiter) progressed to **turn 121/130 BC**, generated five
autosaves and did **not** trigger the effects-channel suppression fallback.
Across 79 RSS samples, the game peaked around 158 MiB and ended around
102 MiB, showing no runaway resident-memory growth in that run. The
source save SHA-256 remained unchanged. These are bounded test results,
not proof of all-scenario, all-era sound compatibility.

### Final selected configuration: campaign verification

On the final rebuilt executable with the **default 5% WAV premix input,
3× effects output, and smooth peak limiting**, a new culture-linked
Persian game reached **turn 4** without invoking the emergency audio
guard. The separate **130-second** mid-campaign run resumed a preserved
turn-111 save and reached **turn 117**, produced five autosaves, and
captured its final framebuffer successfully. Resident size peaked near
**215 MiB** and finished near **104 MiB**, with **no audio-suppression
events**; this is not proof that all game memory is leak-free.

A preceding 195-second campaign test produced five autosaves without
an audio-suppression event, but its watchdog found the guest busy
during the final snapshot and returned a non-passing exit status.
That is a **snapshot-validation limitation** and is not counted as a
passing complete campaign test. The follow-up 130-second run passed all
checks. In all runs the source-game save checksum stayed unchanged.

These tests **cannot establish subjective audible fidelity** because the
device was silent for safety. The user should audition the updated game
at low volume and report whether music and normal effects resume without
repeating noise. The narrow backstop can still reduce effects volume if
a different scenario triggers unusual clipping.

The original game's files and save data are never edited. Temporary
test copies are removed when their scripted run completes.

## Run source-only validation

```sh
ctest --test-dir build-macos -R 'macos_wav_pcm_gain|macos_sfx_runaway_guard|macos_dsound_lifetime' --output-on-failure
```

For regular use, **double-click Civilization III.app on the Desktop**.
Its launcher opens the locally compiled game without a Terminal window.
