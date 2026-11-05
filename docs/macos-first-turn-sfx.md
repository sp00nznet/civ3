# Apple Silicon — first-turn repeating-noise protection

> **Superseded as the primary fix.** The first safeguard prevented a harsh
> repeating noise by muting a whole software-mixed effects channel, but
> that also silenced music carried through that channel during gameplay.
> The [updated in-memory WAV gain mitigation](macos-gameplay-wav-gain.md)
> keeps the effects channel active and only uses the guard as a last resort.

Status: **Mitigation verified on the synthetic-output and actual-game paths**,
not yet proof that the underlying lifted `sound.dll` mixer bug is eliminated.
Tested on macOS / Apple Silicon, November 4, 2026.

## Reported symptom

The main-menu music initially plays properly. Around the first gameplay turn,
a loud distorted sample begins repeating indefinitely. The native DirectSound
mixer previously rendered the game's software-mixed SFX ring verbatim.

## Diagnosis

A temporary *diagnostic build*, run with SDL's **dummy audio driver** so
nothing reached speakers, traced the three DirectSound buffers:

| Buffer | Size | Purpose | Observed |
|---|---:|---|---|
| Primary | 16 KiB | Output format | No PCM directly mixed |
| Software SFX, flags `0x10088` | 64 KiB | Continuously refilled effects | PCM starts silent, then develops a repeated highly clipped waveform |
| Music, flags `0x10080` | 256 KiB | Soundtrack / MP3 path | Ordinary PCM; stops as scene changes |

The SFX buffer's memory is refreshed every ~30 ms by `sound.dll`. It is *not*
simply a stopped producer replaying an old ring. Its incoming PCM grew to
20,000–30,000 RMS (16-bit full scale is 32,768), with some traced blocks
containing **over 70% clipped samples**. This establishes corruption or
runaway gain *upstream of CoreAudio output*, not a Bluetooth/speaker fault.

Controlled APFS-copy tests separately shortened the `Hawk.wav`,
`LoveTheKing.wav`, `AncMEa.wav` and `AncMEb.wav` files, silenced selected
Warrior sounds and `EnterTurn.wav`, and muted groups of 59 general sounds or
31 unit sounds. The pathological SFX PCM persisted. Clearing each SFX write
region before its refill also did not solve it. **No audio asset changes were
applied to the actual installation**, and none are included in this commit.

## Safeguard

The Mac-only `pcrecomp-sfx-runaway.patch` adds a small guard to the
DirectSound software effects buffer, backed by `src/linux/sfx_guard.h`.

- It monitors PCM *after the game's own volume gains* but before the audio
  samples are mixed into CoreAudio output.
- **Normal music (buffer `0x10080`) is not gated.** The guard is applied only
  to looping software effects buffers, identified by the `0x8` buffer flag
  and at least 32 KiB of storage.
- Short sound effects and low-level/silent audio pass unchanged.
- Persistent high-RMS, clipped effects PCM for about 750 ms triggers a
  ~100 ms fade-down of that buffer. The updated guard retains a 15%
  effects output floor rather than muting the channel completely.
  Game timing, position counters and source audio bytes are unchanged.
- Once the input remains quiet for about 2.7 seconds, it automatically
  fades the effects buffer back in over ~200 ms.
- The guard's configuration is read **once before starting SDL audio**.
  Normal realtime audio callbacks never make environment lookups or write
  diagnostics to disk. For disposable silent-device tests, set
  `CIV3_SFX_GUARD_LOG=1` to log suppression/recovery transitions.
- To disable this emergency guard for diagnostics, launch with
  environment variable `CIV3_DISABLE_SFX_GUARD=1`. This restores legacy
  effects mixing and can reproduce the harsh noise; **do not use that
  diagnostic override at high speaker volume**.

This is a bounded **audible-failure protection**, not a precise correction
to the original mixer. While a distorted waveform persists, some ordinary
game sound effects may also be inaudible. Main-menu MP3 music is not controlled by the guard, but some
in-game music uses this same effects channel. Therefore the first version
could produce complete gameplay silence. The revised in-memory WAV gain
correction is the default path, and this guard is now a fallback.

## Verification

The pure synthetic test verifies that quiet PCM and brief loud effects pass,
prolonged clipping triggers suppression once, and a sustained quiet signal
allows automatic recovery. The native game test ran in a disposable game
copy, with `SDL_AUDIODRIVER=dummy` so there was no audible speaker output.

**Actual game result:** A culture-linked Persian small-map game progressed
to **turn 4**. During the same test, with opt-in diagnostics and the
silent SDL dummy driver, the guard explicitly logged:

```text
[dsound] sustained SFX distortion suppressed; music unaffected (rms=0.668, clipped=25.8%)
```

The audio-buffer regression and synthetic source-only tests passed as well.
Game assets and all original primary saves were unchanged. No protected
audio recording or user-owned sound was committed to Git.

## How to run

Double-click **Civilization III.app** on the Mac Desktop. It launches the
updated native executable in this repository, and the SFX safeguard is
enabled by default.

To reproduce the synthetic tests without a commercial game:

```sh
ctest --test-dir build-macos -R 'macos_sfx_runaway_guard|macos_dsound_lifetime' --output-on-failure
```

**Caveat:** Since a human has not yet auditioned the updated build, the
in-game audible outcome is not claimed to be perfect. Test at low output
volume first. More work is needed to locate and correct the underlying
`sound.dll` software-mixer error without suppressing its effects output.
