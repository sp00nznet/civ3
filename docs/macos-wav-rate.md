# WAV timing at the effects mixer's sample rate

The native host now converts a guest-memory WAV copy to the legacy effects
ring's working rate before exposing it through MMIO. Original files and saves
remain unchanged. MP3 decoding follows its existing separate path.

The legacy software mixer interpreted 44.1 kHz samples at its 22.05 kHz
baseline. A one-second, 1 kHz WAV reported a one-second duration through the
sound API but played at 500 Hz for two seconds. Changing the pitch state
corrected the mono diagnostic but left stereo at half rate. Conversion at the
WAV boundary corrects both formats and filters frequencies that the target
rate cannot represent.

DirectSound supplies the current effects ring rate under its existing mixer
lock. It uses the same ring identity as the effects guard. Ordinary music
buffers do not select this rate. With no effects ring, or conflicting ring
rates, MMIO keeps the original image. Inputs already at the working rate are
byte-identical.

The converter validates the RIFF image before allocating a replacement. It
uses the existing libswresample dependency, flushes delayed samples, and
preserves duration within one output sample. It updates format, data, and
RIFF sizes, sampler periods, and zero-origin region lengths. Cue identifiers,
labels, opaque chunks, padding, and trailing bytes are retained. Unsupported
positioned cues or sampler loops remain unchanged. Allocation or resampler
errors fail the file open without exposing a partial replacement. Borrowed
memory files do not enter this conversion path.

Local verification on Apple Silicon covered:

- All 44 original 44.1 kHz WAVs converted successfully. A metadata-aware audit
  checked required time-field changes and preservation of all other metadata.
  Input memory remained unchanged for every file.
- The actual sound DLL played synthetic mono and stereo 1 kHz tones for
  approximately one second at the expected frequency. Channel separation,
  overlapping voices, explicit looping, and mono pitch shifts remained valid.
- A 13 kHz component was suppressed by more than 40 dB when converting to
  22.05 kHz. Short clips, odd frame counts, filter flushing, malformed inputs,
  and metadata before and after the data chunk passed synthetic tests.
- Four original unit recordings played and finished. The Privateer recording's
  active audio spanned about 2.669 seconds after conversion, matching its
  source envelope. The previous playback spanned about 5.339 seconds. These
  durations exclude silence stored in the WAV.
- All 31 native tests, 31 source-only tests, and 102 Python tests passed.
  The converter and MMIO integration tests passed AddressSanitizer and
  UndefinedBehaviorSanitizer. LeakSanitizer was unavailable on this Mac.
- The rebuilt game reached turn 4 in an isolated new game. A separate
  195-second campaign advanced from turn 111 to 121 and produced five
  autosaves. Neither run triggered emergency effects suppression. The first
  capture had no hard-clipped samples; the campaign had 363 in 16,384,000
  samples, approximately 0.002%.
- The CoreAudio driver opened at 44.1 kHz and advanced playback during a
  private probe with its master output set to zero. No microphone was used
  and no system volume setting changed.

All 8,989 original game files retained their pre-test checksums. Disposable
test game copies were removed. Digital and silent-driver checks do not
establish subjective speaker or headphone quality. User listening confirmation
remains necessary before claiming the reported audible issue is resolved.

Run the focused checks with:

```sh
ctest --test-dir build-macos -R 'macos_wav_(pcm|mixer)_rate' --output-on-failure
```
