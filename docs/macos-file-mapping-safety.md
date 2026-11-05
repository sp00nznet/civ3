# Apple Silicon — mapped file lifetime, boundary checks, and large-scenario profiling

Verified on November 4, 2026, using the native 64-bit arm64 Civilization III
host. This is an unofficial compatibility layer; commercial game assets are
**not** published in this repository.

## Confirmed memory-lifetime defect and fix

The previous KERNEL32 `MapViewOfFile` shim used a fixed table of 64 view
records. Once all 64 slots were occupied, it still reserved and returned
a 65th guest address, but did **not** record that address anywhere. As a
result, `UnmapViewOfFile` could not release the mapping and returned
failure. The new synthetic test failed against the previous runtime.

The new `pcrecomp-mapping-lifetime.patch` is applied after the existing
profile and file-sharing patches, to an isolated build-directory copy of
`kernel32_ext.c`. The pinned shared pcrecomp checkout is not modified.

- Dynamically grows the active map table beyond the legacy 64 slots,
  retaining an explicit ceiling of 4,096 tracked views. If the cap or
  allocation fails, the mapping is **not** created or leaked.
- Serializes reservation, bookkeeping and unmapping with a native mutex.
- Checks guest view lengths before converting 64-bit file offsets/sizes to
  32-bit guest addresses, preventing a >4 GiB request from wrapping and
  replacing an unrelated memory region with `MAP_FIXED`.
- Rejects views exceeding the declared mapping size, unaligned file-backed
  offsets, and out-of-range 64-bit offsets.
- Refuses `FILE_MAP_WRITE` against a read-only mapping, rather than returning
  a view that can crash on the caller's first write. That defect was also
  reproduced in the synthetic test before the fix.
- Handles native descriptor duplication and mapping-metadata allocation
  failure, closing native handles that have already been duplicated.

All changes are limited to the Mac-host runtime overlays. The host keeps
bookkeeping storage available for subsequent maps in the same process;
that small reusable table is not a per-map allocation leak.

## Synthetic regression evidence

`tests/mapping_safety_selftest.c` uses an anonymous Win32 mapping. It checks
64 concurrent views, the **65th view**, every view's unmap/double-unmap
behavior, repeated table reuse, oversized and past-end mappings,
read-only access permissions, and failed writes against read-only maps.

The baseline failed on the 65th view's tracking, and on read-only write
permissions. After the patches, all cases passed.

This test is independent of proprietary Civilization III executable content
and uses the normal mock Win32 host. It is also run in a source-only CMake
build via `CIV3_RUNTIME_TESTS_ONLY=ON`.

## Real game validation

The rebuilt Apple Silicon host passed a compressed manual
save -> exit -> fresh-process reload test before the final read-only
permission hardening. The final permission change was then validated with
the full source-only native suite; the final installed executable was also
rebuilt and independently verified at the main menu. It restored **turn 2** and advanced to **turn 8**, creating fresh
autosaves. The original source-save bytes remained unchanged, and the
disposable test installations were removed.

The previously difficult **WWII in the Pacific** Conquest was tested in an
isolated copy with the corrected view tracking. It loaded its custom 1941
scenario and progressed **from December 1941 to January 1942 (turn 1)**
during a 190-second test. The final framebuffer and new January autosave
were captured.

The test recorded 77 periodic RSS observations:

| Metric | Observed |
|---|---:|
| RSS after initial startup | ~143.3 MiB |
| Peak resident memory | ~270.7 MiB |
| Final resident memory | ~163.9 MiB |

The running guest used around one CPU core in a short AI-turn profile,
and the sample stack primarily involved translated game simulation and
graphics. This is consistent with CPU-bound turn processing; it is **not
proof** of any specific AI algorithm defect.

Memory usage **does not show runaway growth in this one test**, but one
short session cannot prove the absence of longer-term leaks. The large
scenario's first-turn success cannot be conclusively attributed to the
mapping fix: earlier scenario runs experienced more concurrent CPU work
and used different test windows. No whole-game performance speedup is
claimed.

The RSS-capable single-player scenario runner now accepts:

```sh
python3 tools/macos_scenario_soak.py wwii-pacific \
  --output work/wwii-rss-fresh --play 190 --interval 1000 --rss-sample
```

The output folder must be **new**, outside `game/`; the script
uses a private APFS clone, preserves logs and RSS readings, and deletes
its test-game clone and temporary saves at completion. The primary
game and its saves are never cleanup targets.

## Tests and remaining limits

```sh
./build-macos.sh
ctest --test-dir build-macos --output-on-failure
python3 -m unittest discover -s tests -p 'test_macos*.py'

cmake -S . -B work/source-only-verification -G Ninja \
  -DCIV3_RUNTIME_TESTS_ONLY=ON -DPCRECOMP="$(cd ../tools && pwd)"
cmake --build work/source-only-verification --parallel 2
ctest --test-dir work/source-only-verification --output-on-failure
```

Although the native runtime patch addresses exhaustion and bounds, it does
not guarantee full scenario compatibility, late-game AI performance, or
leak-free behavior for every file, graphic, or decoded sound.

**Deployment safety:** if the user's desktop game is open, never rebuild
or replace the executable in that live installation. Source-only tests
can be run in a separate test build, and the rebuilt executable can be
installed only after the live game closes.
