# The host

`build/civ3.exe` is the program the lifted C runs inside: a **32-bit** process
built with MSVC x86 on pcrecomp's `runtime/native32`, the same model The Movies
and Gunman Chronicles use. The Civ3-specific part is three files:

| File | What it owns |
|---|---|
| `src/runtime/host.c` | Command line, where the image goes, the guest's shimmed imports, fault report, watchdog |
| `src/runtime/record.c` | Everything done to jgl.dll: the 16-bit colour shim, jgl's half of `--headless`, and `--record` |
| `src/runtime/input.c` | `--move` / `--click` / `--key` scripts for headless runs, the `[game]` milestones the harness reads, and `--peek` (widget tree and UI globals, a bring-up tool) |

## Why 32-bit

Only `Civ3Conquests.exe` is lifted. Its renderer (`jgl.dll`), audio
(`sound.dll`, `mss32.dll`), movies (`binkw32.dll`), Steam and the force-feedback
mouse (`IFC23.dll`) are ordinary 32-bit DLLs. In a 32-bit host they load and run
as themselves, exactly as the Windows loader would bind them; a 64-bit host
would need a hand-written shim for every one of their 290-odd entry points,
plus every call they make back into the game.

The guest image is mapped 1:1 at `0x00400000` (the host links at `0x60000000`
to keep that range free), so a pointer the game holds is a pointer the DLLs
can use. Calls into a DLL go through native32's bridge; calls back from a DLL
into the game (jgl's window procedure calling the game's input handlers, the
idle callback, timer procs) fault on the non-executable guest `.text` and land
on the lifted function.

## Shimmed imports

`native32_bind` fills the IAT with the real functions, except for these:

| Import | Why |
|---|---|
| `GetModuleFileNameA`, `GetModuleHandleA(NULL)`, `GetCommandLineA` | The game is `game\Conquests\Civ3Conquests.exe`, not `build\civ3.exe`. It finds `..\Art`, `Text\` and the `.biq` rules relative to its own path, and its `hInstance` must be the guest image. The folder is passed in 8.3 form because the game's path buffers are fixed-size ([bringup.md, 6](bringup.md#6-from-a-deep-folder-loads-draws-nothing-spins)) |
| `LoadLibraryA` | `jgl.dll` and `sound.dll` are loaded by name at run time; each one is passed to `record_hook_module` as it loads |
| `CreateWindowExA` | Records the top-level window; strips `WS_VISIBLE` under `--headless` |

Headless adds `MessageBoxA` (to stderr) and `ShowWindow` (no-op).

## The 16-bit colour shim (every run)

Civ3 asks jgl for 1024x768 at **16 bpp**, and jgl looks that mode up in the
`EnumDisplaySettings` list. Windows 8 and later list no 16-bit modes, so the
lookup fails, jgl's set-mode returns 1, and WinMain exits quietly with code 0.
The shipping `Civ3Conquests.exe` only survives this because Windows'
app-compat database applies a 16-bit-colour shim to that file name. The host
is a different file, so it does the same job itself, in jgl's import table:

- `EnumDisplaySettingsA` lists every mode twice, as itself and as 16 bpp.
- `ChangeDisplaySettingsA` turns a request for fewer than 32 bpp into the
  desktop's own depth. jgl renders into its own DIB sections and presents with
  GDI, which converts.

The run that found it, with `--native-trace`, ended:

```
[native] ?  (00000400 00000300 00000010 00000000) from sub_005786F0 @09B3B4A0 -> 00000001
[native] KERNEL32.dll!ReleaseMutex (0000035C ...) from sub_005786F0 -> 00000001
...
[native] KERNEL32.dll!ExitProcess  (00000000 ...) from sub_00668939
```

`sub_005786F0` is WinMain; `jgl+0x3B4A0` is the mode lookup.

## Headless and `--record`

jgl, not the game, creates the window, switches the display mode and presents
frames, so `--headless` has to reach into jgl's imports too
(`record_hook_module`):

- `CreateWindowExA` without `WS_VISIBLE`, `ShowWindow` a no-op,
  `ChangeDisplaySettingsA` logged and ignored, `MessageBoxA` to stderr.
- `BitBlt` / `StretchBlt`: a blit to the game window also lands in a shadow
  DIB of the client size. The window is hidden, so the real blit is clipped
  away; the shadow is the only copy of the frame.

`--record out.mp4` then writes the shadow to ffmpeg as raw BGRX at `--fps`
(default 15) from a host thread, and `--frames N` stops after N frames. It
needs `ffmpeg` on `PATH`. The Bink intro is not captured: binkw32 presents on
its own path.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | The game exited normally, or `--frames` was reached |
| 1 | Could not map or bind the image, or enter the game folder |
| 2 | A call reached a function that was not lifted (`[not-lifted]`) |
| 3 | A fault in lifted code (`=== fault ...` report) |
| 4 | `--watchdog` expired (the normal end of a timed headless run) |
