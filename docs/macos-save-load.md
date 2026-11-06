# Native save/load validation and sharing fix

Verified locally on November 3, 2026, on the Apple Silicon Mac described in
[the validation ledger](macos-validation.md).

## Result

The repeatable test in `tools/macos_roundtrip.py`:

1. Loaded a turn-3 autosave in a private APFS copy-on-write game installation.
2. Used the game's own Save dialog to create a 45,865-byte compressed manual save.
3. Ended that process and loaded the new manual save in a fresh process and
   another isolated installation.
4. Confirmed that turn 3 was restored, then played to turn 8 and produced five
   new autosaves (3800 BC through 3600 BC).
5. Verified that both the source save and the loaded manual save were unchanged
   by SHA-256 comparison. Both bounded runs ended with expected watchdog code 4.

The earlier focused repair test reached turn 7 / 3650 BC; its framebuffer was
visually inspected and showed the restored Roman city and continued gameplay.
Evidence is local under `work/macos-roundtrip-verified/` and
`work/qa-save-load/`, not distributed in the source repository.

A second compatibility check loaded a private copy of the pre-existing Persian
330 BC save at turn 111 and continued to turn 112 / 310 BC. The final
framebuffer was visually inspected. This used the final rebuilt
executable; the original save's SHA-256 remained
`6bf1f1f3b12020631d59aa12d0a07dd323a2241f35915923e243518738bdb12d`.
Evidence is under `work/macos-legacy-save-verified/`.

## The failure and diagnosis

The pre-fix host loaded uncompressed autosaves but could not reload a manual
compressed save. It displayed `DataIO Error` / `Fourcc mismatch in readData`
for `GAME` and `CNSL`, then stayed at turn zero. A watchdog exit and a valid
image of that error dialog were **not** counted as success.

The save was not corrupt. An independent PKWARE-DCL decoder (zlib 1.3.1's
`contrib/blast`, used only in ignored local diagnostics) and the game's own
lifted decompressor both produced **the exact same 1,360,365 output bytes**.

Tracing the host file calls found this sequence:

- The loader decompressed the save into `save0.tmp`, opened it with
  `GENERIC_READ` and `FILE_SHARE_READ`, and mapped the valid contents.
- While loading the embedded rules, another decompression tried to create
  `save0.tmp` with write access and `CREATE_ALWAYS`.
- The HLE ignored the first open's sharing restriction and truncated the file
  that backed the active mapping. The section headers that had contained
  `GAME` and `CNSL` no longer contained those bytes.

The repair returns `ERROR_SHARING_VIOLATION` before truncation. The game's
existing fallback then chooses `save1.tmp`, leaving its first mapping intact.
There is no save conversion, binary patch, substituted codec, or game-logic
change involved in this repair.

## Patch scope

`patches/pcrecomp-file-sharing.patch` is applied to a build-directory copy of
`kernel32_ext.c`, after the separate INI patch. The pinned upstream checkout
remains clean.

The patch tracks access/share modes of open guest file handles by device/inode,
checks compatibility in both directions, and defers `ftruncate` until those
checks pass. Closing a handle removes its restriction. Native host access modes
and sharing checks use the same requested read/write bits.

This is an **in-process CreateFileA compatibility implementation**, not a claim
of complete Windows filesystem semantics. External native processes do not
participate in its sharing table. Legacy `_lopen` calls retain their prior
permissive mode; delete/rename API behavior and mapping-only lifetime rules
are not comprehensively emulated by this patch.

The synthetic regression `tests/file_sharing_selftest.c` covers a mapped file,
failed destructive opens, a hard-link alias, compatible readers, both directions
of share incompatibility, and repeated close/reopen cycles. It uses no game data.

## References

- [Microsoft: CreateFileA](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilea)
  documents requested access, sharing compatibility, and creation dispositions.
- [Microsoft: Creating and Opening Files](https://learn.microsoft.com/en-us/windows/win32/fileio/creating-and-opening-files)
  describes the bidirectional access/share combinations.
- [zlib 1.3.1 blast reference](https://github.com/madler/zlib/tree/v1.3.1/contrib/blast)
  was used for an independent diagnostic comparison; it is not linked into this port.
