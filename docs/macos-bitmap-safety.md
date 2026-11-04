# Bitmap allocation safety on main

Verified locally on November 3, 2026. These changes follow the merge of every
existing fork branch into `main`; work no longer requires a separate port branch.

## Reproduced defects and repairs

The new synthetic regression reproduced six failures against the pre-repair
runtime: zero-width and zero-height DIBs, short headers, multiple planes and
compressed sections were accepted, and exhausting the graphics-object table
returned a fabricated bitmap handle instead of failure.

The repair validates header size, dimensions, planes, supported allocation
formats, compression and palette count before allocating. Stride and total-size
arithmetic uses 64-bit intermediates. An explicit per-bitmap **256 MiB storage
budget** includes the existing guard DWORD; this is a host safety policy, not a
Windows limit. Invalid requests return NULL and clear the output pixel pointer.

If allocation or object registration fails, temporary pixels and palette storage
are released. A bitmap is never returned without a selectable object and a
tracked allocation that `DeleteObject` can release. The test exhausts the object
table, checks failure without bogus success, releases it, then creates and deletes
10,000 additional DIBs to exercise reuse.

A related owned-bitmap bug allocated three bytes per pixel for 24-bit requests
while advertising and writing the shim's normalized 32-bit internal format. It
now allocates for the actual internal storage width. The test fills a 257-by-3
bitmap and verifies its last pixel through the guest-facing GDI calls.

Existing game files, executable inputs and generated game C are unchanged.
Both runtime fixes are applied to build-directory copies of the pinned toolkit.
The upstream checkout remains clean. These changes are enabled on the macOS
path; no new Linux runtime compatibility result is claimed.

## Validation

Tested executable SHA-256:
`33da7ec6f17ff3e62f5736553e3ba016db1b9105f4659bec9489c3a3b2cd57d8`.

- **78 Python tests passed**, including four Finder bootstrap tests with moved
  checkouts, quoted paths, minimal PATH, missing Python and exit-status propagation.
- **16 native tests passed**, including the actual-host synthetic PE/DLL test
  retained from the early branch and the new bitmap safety regression.
- The bitmap test covers allocation layouts for 1/4/8/16/24/32-bit DIBs, positive
  and negative heights, malformed headers, overflow dimensions, invalid palettes,
  table exhaustion, cleanup, and repeated recycling.
- Manual save -> process exit -> fresh-process reload restored turn 3 and reached
  **turn 9 / 3550 BC**. The final city screen was visually inspected, fresh
  autosaves were produced, and the source save was unchanged.
- The culture-enabled Persian small-map test passed through **turn 4** without
  changing the normal installation's preferences.

A separate full-runtime ASan/UBSan attempt stopped in an SDL compatibility-library
startup error dialog before the test ran. That process was terminated and is
**not counted as a sanitizer pass**. No whole-game sanitizer result is claimed.

## Scope and remaining limits

This is a defensive allocation/ownership repair, not complete GDI conformance.
It does not add missing CreateBitmap initial-pixel or monochrome semantics,
full palette/packed-format rendering, mapped-section behavior, arbitrary guest
pointer validation, or every raster operation. It also does not establish a
whole-game speedup, long-campaign stability, or Intel gameplay compatibility.
The previous measured menu/graphics performance improvements remain in place.

## Evidence and reproduction

```sh
python3 -m unittest discover -s tests -p 'test_macos*.py' -v
ctest --test-dir build-macos --output-on-failure
./build-macos/tests/civ3_bitmap_test
```

Local private evidence is retained in `work/main-integration/bitmap/`: baseline
failures, fixed test/build logs, the exact tested binary hash, round-trip and
new-game results, the inspected screenshot and sanitizer startup diagnostic.
No game assets, fonts, saves, proprietary binaries or generated game C are in Git.

Microsoft references:
- [CreateDIBSection return and cleanup contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection)
- [Bitmap header and DWORD-aligned stride](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapinfoheader)
- [CreateBitmap inputs and return contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createbitmap)
