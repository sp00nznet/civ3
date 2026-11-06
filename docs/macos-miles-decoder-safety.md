# Apple Silicon — Miles MP3 decoder bounds and long-session memory evidence

Verified November 4, 2026 on the native arm64 Civilization III host.

## Reproduced input-safety fault

The Miles/FFmpeg compatibility layer previously trusted the byte count
reported by the guest-provided compressed-audio fetch callback. A callback
could return **more bytes than the decoder requested**. The host then
attempted to copy that entire reported count from its fixed 64 KiB scratch
allocation into the potentially smaller FFmpeg buffer.

An expanded synthetic test reproduced this: the original runtime accepted
a stream whose fetch callback consistently over-reported its actual data.
The extra read could cross the allocated buffer boundary; it was not safe
to trust that value.

The [additional build overlay](../patches/pcrecomp-mss-bounds.patch) now
checks each callback result against the requested count before any copy.
An oversized result marks the stream invalid and returns an FFmpeg I/O
error. A stream with this fault cannot report a successful open.

## Memory lifetime and failure handling

The repair also makes the following paths explicit:

- Check guest scratch, FFmpeg I/O buffer/context, format context, codec
  frame and packet allocations before accessing them.
- Free the I/O buffer and guest allocations on partial initialization
  failures.
- Preserve a successfully allocated format context on stream-information
  failure so `avformat_close_input` can release it. The old code
  unconditionally set it to NULL, potentially leaking it on this path.
- Bound seeking to the 32-bit guest offset range.
- Calculate decoded PCM capacity with wide arithmetic and an explicit
  cap. Preserve the previous buffer if `realloc` fails rather than
  losing the pointer and passing NULL to `swr_convert`.
- Refuse processing with missing output buffers or incomplete decoder
  objects.

The baseline Miles guest-pointer patch remains intact. Both patches
apply only to a **build-directory copy** of the pinned `pcrecomp` runtime.
No copyrighted executable, game data, sound file or proprietary generated
game source is included in this commit.

## Synthetic regression results

The selftest synthesizes a short MP3 via installed FFmpeg, then verifies
decode, guest-pointer lifetimes, repeated stream opening and closing,
malformed MP3 input, and misbehaving guest callbacks.

- 100 valid MP3 opens/closes: **0 native malloc bytes in-use growth**
  measured by macOS `malloc_zone_statistics`.
- Malformed MP3 input: rejected without an unsafe handle.
- Callback returning more than the requested bytes: rejected at stream
  creation. This check failed before the repair and passed afterward.
- Full native tests: **21/21 passed**, including a separate **source-only**
  CMake build with no commercial game or generated game source.

The malloc result applies only to these exercised audio allocations,
not to all game processes or all FFmpeg behavior.

## Real-game long-session memory sampling

The source-controlled `tools/macos_campaign_soak.py` supports
`--rss-sample`. The monitor samples the single child process's resident
memory, records timestamped readings to `rss-samples.json`, preserves
small log/screenshot evidence and cleans up its private game clone and
test saves afterward. It never modifies the user's source save.

An existing Persian **330 BC / turn-111** save was loaded in a private APFS
copy, advanced **to turn 121 / 130 BC** in a bounded **195-second**
single-player run, and generated five new autosaves. The save's source
checksum was preserved.

| Metric | Observed reading |
|---|---:|
| Samples | 79 |
| RSS after initial startup (>=25 s) | ~219.7 MiB |
| Peak RSS | ~231.7 MiB |
| Final RSS | ~129.2 MiB |
| Resident-memory growth after startup | **-90.5 MiB** |

The very first sample (32 KiB) was captured before the process had mapped
its main memory, so startup-relative comparisons intentionally use the
first sample after 25 seconds. These readings show **no runaway resident
memory growth in this session**. RSS includes mapped code, disk caches,
game assets and allocator free lists; a rise or fall is not a proof about
individual allocations. Much longer campaigns and larger scenario AI turns
remain to be investigated.

This is observational memory profiling; it does **not** demonstrate that
all memory leaks are eliminated, or that game FPS or AI turns are faster.

## Reproduction

```sh
./build-macos.sh
ctest --test-dir build-macos --output-on-failure
bash tests/run_mss_guest_test.sh build-macos/civ3_mss_test "$(command -v ffmpeg)"

python3 tools/macos_campaign_soak.py \
  --seed-save "game/Conquests/Saves/Xerxes of the Persians, 330 BC.SAV" \
  --output work/persian-rss-check \
  --play 195 --interval 950 --rss-sample
```

Select a *new* output directory for each run; the testing tools reject
existing or source-game paths. The other regression and cleanup tools
remain available as documented in [single-player coverage](macos-singleplayer-coverage.md).
