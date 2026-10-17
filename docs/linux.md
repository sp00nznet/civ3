# Linux, native

`build-linux/civ3` runs Civilization III: Conquests as a 32-bit Linux
program: the lifted C on pcrecomp's `runtime/win32hle`, an implementation of
the Win32 calls the game makes, on libc, SDL2 and ffmpeg. No Wine, no
Windows DLLs. Civilization III never had a Linux release (Aspyr's port was
for the Mac). Building it is in the README ([Linux, native](../README.md#linux-native)).

## What runs where

On Windows (`src/runtime/host.c`) only `Civ3Conquests.exe` is lifted; its
DLLs load and run as themselves. Linux cannot run their machine code, so:

| Module | On Linux |
|---|---|
| `Civ3Conquests.exe` | lifted (`src/recomp/gen`), mapped at 0x00400000 |
| `jgl.dll`, the 2D renderer | lifted (`run_lift.py --module jgl`, `gen_jgl`), loaded at its own 0x10000000 |
| `sound.dll`, Firaxis' audio | lifted (`--module sound`, `gen_sound`), rebased to 0x10080000 |
| `binkw32.dll` | win32hle's `bink.c` on ffmpeg, with the `BinkBuffer` calls Civ3 presents through |
| `mss32.dll` (Miles) | win32hle's `mss.c`: the file helpers and an MP3 ASI codec on ffmpeg (the music) |
| `steam_api.dll` | absent: `SteamAPI_Init` fails, as with no Steam client (the game says so, then plays) |
| `IFC23.dll` (force-feedback mouse), OpenGL | absent: every device creation fails |

`src/linux/main.c` loads the two lifted DLLs when the game asks for them
(`LoadLibraryA`): mapped at the base they were lifted at, their imports bound
to win32hle, their lifted `DllMain` run. `GetProcAddress` reads their export
tables. Their address ranges are held from the start, or a thread's stack or
a file mapping can land there first. `src/linux/dispatch.c` is one dispatch
table for all three modules.

sound.dll and jgl.dll both want 0x10000000. `run_lift.py --module sound`
moves sound.dll by applying its own relocations; pefile's `relocate_image`
also rewrote its import lookup table, which the loader then could not read.

## What win32hle needed

Each of these came from a run stopping at it; they are toolkit changes
(pcrecomp, `runtime/win32hle`), not Civ3 code.

- **A screen for a GDI game.** jgl draws into DIB sections and blits them to
  its window's DC; win32hle's window DCs drew nowhere and its DIB sections
  could not be selected into a DC. Now a host sets a *window surface*
  (`hle_gdi_window_surface`): window DCs draw into it, blits convert between
  depths (jgl's are 16-bit 5-5-5 and 8-bit with colour tables), bottom-up
  DIBs read the right way up, clip regions clip, and `screen.c` shows the
  surface when there is no DirectDraw primary.
- **The window surface follows a mode change**, for a DC jgl keeps.
- **Limits.** jgl keeps a DC per surface and Civ3 makes 37 font sizes in 8
  styles: 64 DCs and 64 fonts were not enough (the loading screen's box, and
  every italic and underlined word, fell back to the system font).
- **Text like Windows'.** The game's own Lucida Sans (`AddFontResourceA`
  through a `.fot`), read through our own file I/O (a 32-bit SDL's `stat`
  fails on a disk with 64-bit inode numbers); no smoothing where the font's
  `gasp` table says so; Windows' synthetic bold and italic for a face with one
  file; no kerning; full hinting for crisp text, so advances and line breaks
  match. `GetTextMetricsA` had its character fields a byte off (so `tmItalic`
  was set) and reported a fixed-pitch font.
- **Threads.** sound.dll's thread starts suspended, waits in
  `MsgWaitForMultipleObjects` and is posted to with `PostThreadMessageA`.
  Messages now go to their window's thread (or the thread they were posted
  to), as on Windows: sound.dll's `PeekMessage(NULL)` loop was taking the game
  window's messages.
- **The rest:** file mappings, `mmio*` (RIFF/WAV reading), DirectSound by
  `CoCreateInstance`, `CoCreateGuid`, process affinity, the clipboard,
  `GetCursor`.

## State

Plays: the Bink intro, the main menu (pixel-close to Windows, text and all),
the music and sounds, new games, loads, saves, scenarios and turns, headless
or in a window with a real mouse (checked under Xvfb with xdotool).

`tools/playtest.py` runs `build-linux/civ3` on Linux, and every case passes:
the 12 main-menu items, the 8 ways back from them, New Game as all 31
civilizations, all 12 Conquests scenarios and the 37 in-game screens with 5
turns after each (Ubuntu 24.04 under WSL2, 3 at a time). `quick-autopilot`,
the 30-minute one, was not run. The suite found the fixes below.

Not yet:

- `tools/conformance.py` and `oracle.py` drive the Windows host only;
  `--original` needs Windows.
- Multiplayer is untried on Linux.

## What the play tests found

- A file named `Com...` opened as a serial port (win32hle took any name
  starting COM or LPT for one): the credits, and WWII in the Pacific's
  Commonwealth advisor, which the scenario then faulted on.
- The Load dialog preselects the first save in directory order, which on
  Linux was another save than the test's: `FindFirstFileA` now sorts as NTFS.
- jgl keeps its window DC across the intro's mode switch, and drew into the
  old screen: black after the intro.
- With no sound card, DirectSound failed and sound.dll faulted on it: now a
  silent device.
- The MP3 stream asked sound.dll's callback for bytes past the file's end.
- The host image ended near 0x0F000000 and the heap after it reached
  jgl.dll's 0x10000000 in some runs: the host now links at 0x20000000.
