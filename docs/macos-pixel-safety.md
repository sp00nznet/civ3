# Packed pixels, palette bounds and clipped rendering

Verified locally on November 3, 2026, on the Apple Silicon Mac used in the
[validation ledger](macos-validation.md). This follow-up is on `main` and
changes the native macOS compatibility renderer, not the original game files.

## Failures reproduced before the repair

The bitmap allocation work accepted 1/4/8/16/24/32-bit DIBs, but the pixel
accessor still treated formats other than 8/16 as a 32-bit load. A 24-bit pixel
therefore read one byte beyond its three-byte storage. An isolated synthetic
24-bit surface ending exactly at a protected page reproduced **SIGBUS** with the
previous runtime. This is a proven renderer boundary bug, not a claim that the
normal campaign had already crashed at that instruction.

The safe baseline run also reported **140 failed pixel assertions** across
packed-color reads, writes, and neighbor-preservation checks. These are failed
assertions, not 140 distinct bugs. The guard-page crash is a separate process.

## Repairs

- Pixel rows now support 1-bit, 4-bit, 8-bit, 16-bit, 24-bit and 32-bit access.
  Packed indices use high-bit/high-nibble-first order, and 24-bit pixels use
  exactly three B,G,R bytes. Byte-based access also handles unaligned rows.
- Writing a packed pixel preserves the adjacent bit/nibble and row padding.
  DIB color tables are consulted for indexed sources and indexed destinations.
  Destination matching chooses the nearest RGB entry, deterministically choosing
  the first on ties; this is not comprehensive Windows palette conformance.
- Indexed copies remap differing palettes rather than treating color indices as
  RGB values. Palette changes are immediately visible without reselecting a DC.
- `SetDIBColorTable` limits work to the actual declared palette size using
  subtraction, avoiding unsigned `start + count` overflow. Out-of-table starts,
  zero-length work and null source pointers do not write entries.
- Scalar copies and stretches clip destination work before looping and use
  64-bit coordinate products. Huge off-screen requests cannot turn a two-by-two
  destination into billions of iterations. Source/destination scaling and
  clipping are checked against an independent scalar reference.

The existing fast true-color transfers remain unchanged. The new primitives
are in `src/linux/pixel_io.h`; the runtime changes are applied only to a
build-directory copy through `patches/pcrecomp-pixel-formats.patch`.
The pinned pcrecomp checkout remains unmodified.

## Exact executable and verification

Tested executable SHA-256:
`ee59ef58664837e8ff97eb954e405c9571bed410f3d0f26b7571fe680815648b`.

| Check | Observed result |
|---|---|
| Existing Python suite | **78 passed** |
| Native suite | **18 passed** |
| Dependency-free pixel tests | **311,492 assertions passed** over all six supported storage depths, unaligned rows, packed neighbors and guard pages |
| Focused AddressSanitizer + UndefinedBehaviorSanitizer | All 311,492 pixel-helper assertions passed; no SDL dependency; leak detection disabled on this Mac |
| Real HLE format tests | Top-down/bottom-up DIBs, seven odd/even widths, row padding, single-pixel writes, differing palettes, palette updates and oversized counts passed |
| Scaling oracle | **1,500 deterministic randomized cases** matched an independent reference |
| Guard-page regression | Final 24-bit reads and writes succeed at the last accessible byte; the baseline process failed with signal 10 |
| Extreme clipped rectangles | INT_MAX extents and INT_MIN offsets complete with expected pixels in the bounded native test |
| Compressed save -> exit -> reload | Restored turn 3 and continued to **turn 8 / 3600 BC**, with five fresh autosaves and the source save unchanged |
| Culture-linked Persia, small map | Reached **turn 4 / 3800 BC**, rule still enabled; normal game-copy preferences unchanged |
| Native-window menu comparison | Previous and repaired builds produced **byte-identical framebuffer files** |

The resumed Roman game and new Persian game framebuffers were visually inspected.
All game tests use separate private copies. The test tools checksum their source
save/preferences and retain logs, screenshots and binary identities under ignored
`work/macos-pixel-safety/`.

The windowed menu comparison was one pair, eight seconds at the menu per run.
CPU totals were about 1.43 seconds before and 1.33 seconds after, including startup
and exit. That small single-pair difference is **not evidence of a general
speedup**. The purpose was to reject a visual regression and exercise actual
native windows. No new whole-game FPS, AI-turn or battery-life claim is made.

## Scope and remaining limitations

This supports the shared DC pixel/blit path for the listed uncompressed DIB
formats. It does not complete all GDI behavior: raster-operation variants,
monochrome DDB foreground/background rules, arbitrary bitfield masks, general
mirrored stretches, `StretchDIBits`, raw `GetPixel` color-return semantics,
CreateBitmap initial data, mapped sections and validation of arbitrary guest
pointers remain outside this repair. Overlapping storage retains the prior
forward-copy fallback. Tests do not establish every scenario, mod or campaign.

ASan/UBSan coverage is limited to the dependency-free pixel helper, not the
complete game or the whole SDL/FFmpeg-linked runtime. Windows/Linux paths and
commercial gameplay on Intel Macs were not locally retested in this pass.

## Reproduction

```sh
python3 -m unittest discover -s tests -p 'test_macos*.py' -v
ctest --test-dir build-macos --output-on-failure
./build-macos/tests/civ3_pixel_formats_test
./build-macos/tests/civ3_pixel_io_test
clang -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Isrc/linux tests/pixel_io_selftest.c -o /tmp/civ3-pixel-io-test
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 /tmp/civ3-pixel-io-test
```

The pre-repair executable and guard-page reproducer remain local; no game code,
assets, fonts, save files, recordings or generated game C are published.

Microsoft format references:
- [Pixel packing, byte order and top-down/bottom-up rows](https://learn.microsoft.com/en-us/windows/win32/wmdm/-bitmapinfoheader)
- [DIB table length and row stride](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader)
- [SetDIBColorTable](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setdibcolortable)
