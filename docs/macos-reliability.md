# Safer launching, recording and scripted input

Verified on the Apple Silicon Mac in [the validation ledger](macos-validation.md),
November 3, 2026. This pass changes the POSIX host and the macOS launcher; it does
not change the original executable, generated game code, assets or saves.

## Launching no longer terminates an existing game

The previous launcher sent SIGTERM to a PID from its last run before rebuilding.
That could discard unsaved progress, and log/debug modes did not share the same
tracking. The new launcher never terminates an earlier session merely because it
was run again. It prints a message asking you to save and close that session.

`tools/macos_launch.py` holds a kernel lock for the build/launch lifetime across
normal, logging, debug and verification modes. A stale PID file is not used as
a signal target. The native executable also holds a separate kernel lock in the
selected writable `Conquests` directory, so direct launches cannot concurrently
reuse that installation's saves and fixed-name temporary files.

The lock files persist, but ownership is released automatically when their
process exits. **Do not delete lock files while a game is running.** These locks
coordinate updated native hosts, not unrelated Wine/Windows programs. Separate
private game installations can still run concurrently for tests.

The existing play-test harness skips the native lock when constructing its
hard-linked test farms. Other launch modes share the launcher protection; log
mode writes a new timestamped file and preserves the game's exit status.

## Shell-free video recording

The recorder starts FFmpeg with an argument vector through `posix_spawnp`, not
an interpolated shell command. Spaces, apostrophes and shell punctuation in
filenames are passed literally. Existing recordings are protected by default.

```sh
./script/build_and_run.sh --record "work/A player's game.mp4" --fps 15
# Only when replacing a previous recording is intentional:
./script/build_and_run.sh --record work/demo.mp4 --overwrite-recording
# A bounded capture, which exits after the requested frame count:
./script/build_and_run.sh --headless --record work/menu.mp4 --fps 6 --frames 12
```

Recording remains **video-only**; game audio is not captured in these files.
Missing FFmpeg, bad output paths and encoder failures are logged as failures.
A bounded recording that fails returns code 5, rather than reporting success.
On ordinary exit or watchdog completion, a failed recording also converts an
otherwise successful capture/test status to code 5.

Frame writes use a nonblocking pipe with a five-second deadline. Finalization
waits up to ten seconds, then terminates/kills and reaps a nonresponsive encoder.
A frame is copied while the guest is paused; the game lock is released before
waiting on the encoder. A display-size change ends the recording with an error
rather than silently emitting uninitialized or incorrectly sized frames.

SIGINT (Ctrl-C) and SIGTERM request shutdown through a flag; a normal thread
finalizes the video before exit. The signal handler itself performs no encoder
work. Abrupt crashes and SIGKILL still cannot guarantee a finalized MP4.

## Input and verification checks

- Frame rates must be integers from 1 to 240; frame counts must be positive.
- Watchdog/play durations and event times have finite, bounded ranges.
- Malformed numbers, NaN/infinite event times, trailing junk, invalid key codes
  and modifiers, overflowing coordinates and duplicate autopilot events fail
  before the game starts. Equal-time events retain their command-line order.
- Scripted memory waits check address range/alignment; callers must still supply
  a mapped guest address. This is not a complete guest-memory validity probe.
- `--verify --game DIR --work DIR --exe FILE` forwards the selected installation
  and runtime inputs to the smoke test instead of implicitly testing defaults.

## Completed verification

The final tested executable SHA-256 is:
`d21322e100bd341b660c2e45219559a4e797d92d00d98a4352ea203eb760f996`.

| Check | Observed result |
|---|---|
| Python tests | 69 passed |
| Native regression tests | All seven passed, including a source-only build with no commercial assets/generated game code |
| Focused safety helpers | Passed AddressSanitizer and UndefinedBehaviorSanitizer runs; the whole game was not sanitizer-tested |
| Literal punctuation in a video path | Valid 1024x768 H.264 video with six decoded frames |
| Existing output without overwrite permission | Exit 5; original output SHA-256 unchanged |
| Explicit overwrite | Exit 0; replacement decoded with seven frames |
| Missing encoder / nonexistent output directory | Both returned recording failure code 5 |
| Stalled synthetic encoder | Write deadline and bounded shutdown passed; child was reaped |
| Duplicate launcher and direct-native launch | Both rejected with code 75; original game remained alive and completed normally |
| Ctrl-C / SIGTERM while recording | Exit 130 / 143; both videos finalized and decoded (nine / eight frames in these runs) |
| Save -> fresh process -> reload | Restored turn 3, continued to turn 8 / 3600 BC; five fresh autosaves, source save unchanged; gameplay framebuffer visually inspected |
| Existing play-test exit case run twice | Both passed with exit 0; the harness explicitly overwrites only its owned per-case video |
| Verification with another game folder | Passed using the selected private installation |
| Actual native window and recording | Cocoa window registered; 46-frame 1024x768 capture decoded; menu frame visually inspected |

Local evidence is retained under `work/macos-reliability/`, including
`roundtrip-final/result.json`, `final-recording/recording-result.json`,
`final-recording/signal-results.json`, test logs, videos and framebuffers.
Private game copies were used; the normal installation's preferences were
hash-checked as unchanged during the recording/launch tests.

This is a reliability pass, not additional proof of long campaigns, multiplayer,
all scenarios, Intel compatibility, or optimized-release performance. The
culture-linked repair remains experimental as documented separately.
