# PCM buffer corruption in native macOS audio

The native host now gives the sound engine a separate MMIO read buffer at
the current file position. WAV input and effects output both default to unity
gain. Original WAV files and saves remain unchanged.

The previous `mmioSetBuffer` stub returned success but exposed the whole file
through `pchBuffer`. The sound engine sought past the WAV header, requested a
buffer, and then read its base. It mixed RIFF header bytes as signed PCM.
Eight synthetic constant-500 voices produced a saturated peak of 32768.
Their first output samples matched the WAV header words multiplied by eight.
With a real buffer, the same probe produces the expected peak of 4000.

The WinMM overlay owns or borrows the requested storage, tracks the valid
window separately from its disk refill position, and preserves an empty valid
range at EOF. It applies to a build-directory copy of the pinned toolkit.
An asset-free test checks payload bytes, buffer capacity, refill, seek,
cursor commits, EOF, and allocation ownership.

Real buffer boundaries also exposed a loader error for streaming WAVs larger
than 128 KiB. The loader primes a cache with four bytes of headroom, then
unconditionally advances its source buffer. That extra refill discards four
unread bytes. The stream reaches EOF while its remaining-byte counter is four,
so the voice never finishes. The macOS sound overlay skips that refill when
the current window still contains unread bytes. It patches all four aliased
copies in build-directory C, checks the supported DLL hash, and rejects an
unexpected lift. The source DLL and original generated C remain untouched.

The actual sound-DLL probe now finishes a long stereo WAV and clears its
output to silence. A synthetic compiled-call test checks both refill branches
and the guest stack balance. Short mono and stereo probes retain nonzero,
bounded output. These checks capture digital samples through silent devices.
They do not establish subjective speaker or headphone sound quality.

The final normal-volume executable passed 29 native tests. A separate build
without commercial assets passed the same 29 tests. All 100 macOS Python
tests passed, and the MMIO buffer test passed AddressSanitizer and
UndefinedBehaviorSanitizer checks.

An isolated first-turn game reached turn 3 without an emergency guard event.
Its final 20 seconds contained nonzero audio and zero hard-clipped samples.
A separate 195-second campaign progressed from turn 111 to 121 and produced
five autosaves without a guard event. Its digital capture contained 153
hard-clipped samples, approximately 0.001% of the capture. These results
support removal of the earlier severe volume reduction, but do not prove
perfect fidelity in every scene. All 8,989 original game files matched their
pre-test checksums. Both disposable game copies were removed after testing.

The previous volume reduction is no longer the default. The effects limiter
and emergency clipping guard remain available as backstops. Earlier findings
and mitigated runs are historical evidence in
[WAV gain mitigation](macos-gameplay-wav-gain.md) and
[timer scheduling](macos-audio-timers.md).

Run local checks with:

```sh
ctest --test-dir build-macos --output-on-failure
python3 -m unittest discover -s tests -p 'test_macos*.py'
```

GitHub hosted workflows remain disabled to preserve the owner's quota.
