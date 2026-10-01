# Bring-up log

Each wall, what it looked like, and the fix. Newest last.

## 1. `build.cmd`: `'vswhere.exe' is not recognized`

The build script copied from The Movies had LF line endings, and `cmd.exe`
mis-parses a `for /f` with a quoted command under LF. Converted to CRLF;
`.gitattributes` pins `*.cmd` and `*.ps1` to CRLF so a checkout cannot undo it.

## 2. WinMain exits with code 0, no error

```
entering 0x0066B0A9

[load] jgl.dll -> 0BCD0000
[window] jgl CreateWindowExA("Sid Meier's Civilization III: Complete", 1920x1080) -> hidden 075E085C
Setting breakpad minidump AppID = 3910
```

and the process was gone. `--native-trace` showed Steam was fine
(`SteamAPI_RestartAppIfNecessary -> 0` in `al`, `SteamAPI_Init -> 1`), and the
last real decision was:

```
[native] ?  (00000400 00000300 00000010 00000000) from sub_005786F0 @09B3B4A0 -> 00000001
[native] KERNEL32.dll!ReleaseMutex ...
```

WinMain at `0x578D6C` calls jgl's set-mode through its vtable (`[edx+0x58]`)
and quits on non-zero. jgl's set-mode (`jgl+0x3B4A0`) walks its list of
`EnumDisplaySettings` modes for an exact width, height and **16 bpp**. Windows
8 and later list no 16-bit modes. The real exe works only because the
app-compat database gives `Civ3Conquests.exe` a 16-bit-colour shim by name.

Fix: the host does that shim in jgl's IAT (`record.c`, see
[host.md](host.md#the-16-bit-colour-shim-every-run)).

## 3. Over RDP, the same silent exit

A later run from an RDP session exited the same way. The window came up
1806x972: the phone's resolution, and the only mode an RDP display lists, so
there was no 1024x768 to find at any depth. Headless now answers
`EnumDisplaySettingsA` from a fixed list (640x480 to 1920x1080, 32 and 16 bpp);
nothing is shown, so the real display's modes do not matter.

After 2 and 3 the game loads `sound.dll`, plays the Bink intro and reaches the
main menu, 1 to 3 minutes into a headless run.

## 4. Menu clicks: two clicks, not one

Mouse hover highlighted *Quick Start*, but a click on it, or on *Exit*, did
nothing. The time went into ruling things out:

- The messages arrive. jgl's window procedure (`jgl+0x3C3D0`) turns
  `WM_LBUTTONDOWN`/`UP` into calls on the game's input object, and
  `--callbacks` showed both landing in lifted code with the right
  coordinates: `sub_006545F0 (0, 291, 383)` (the 0 means "not a double
  click") and `sub_00654720 (291, 383)`.
- The game never polls the button (`GetAsyncKeyState`/`GetKeyState` with
  `VK_LBUTTON`), and it does not care that a hidden window is never the
  foreground window. Shims for both were tried, did nothing, and were removed.
- `--peek` (a dump of the widget tree from a host thread) showed a single
  top-level widget, the map screen, with the main menu as an input-transparent
  child (style bit 2). The map screen's handlers ignore clicks while
  `[0xB64E1E]` is set, and it is set: that byte means "the main menu's modal
  loop is running".
- A trace build with a per-entry histogram (idle window vs. click window)
  listed the functions only the click ran. Among them: `sub_00559CE0`, called
  from the map screen's button-up handler.

`sub_00559CE0` is the menu's click handler, and it explains everything. It
finds the item under the mouse; if that item is the *selected* one
(`[0xA49094]`, New Game when the menu opens) it activates it, and otherwise it
only selects it, with a click sound. Hover draws a highlight but does not
select. So the first click on Quick Start selects it and the second starts
the game, which is how the original behaves too. Nothing was wrong with the
recompilation; the scripts needed a second click:

```
build\civ3.exe --headless --run --record work\turn.mp4 --watchdog 250 ^
  --move 291,383@6 --click 291,383@8 --click 291,383@11 ^
  --move 786,258@28 --click 786,258@30 --key 0x42@36 --key 0x0D@48 ^
  --key 0x1B@62 --key 0x20@70 --key 0x20@76 --key 0x20@82 --key 0x0D@90 --key 0x0D@96
```

That is Quick Start, dismiss *Dawn of Civilization*, **B** to found a city
("Name this town? Rome"), Enter, Escape out of the city screen, Space past each
unit, and Enter to end the turn. The turn ends and the year reads 3950 BC:
the AI's turn and the end-of-turn processing ran in lifted code.

## 5. Watchdog exit code `0xC0000409` with `--record`

The watchdog thread closed the ffmpeg pipe while the recorder thread was
writing to it. `record_close` now takes the recorder's lock, and a closed
recording is never reopened (reopening truncated the file).

## 6. From a deep folder: loads, draws nothing, spins

Found by testing the Quick start in a clean folder, 150 characters deep under
`%TEMP%`: boot milestones 4/5, the same `2359685 indirect calls` at the
watchdog on every run, and never a first frame. The executable, the generated
C and the game files were byte-identical to a working tree. The working build
pointed at that folder failed the same way, and the same folder through a
short junction (`C:\c3j`) reached the menu. So it is the path length: the game
keeps its folder in fixed-size buffers (the Steam install path it was tested
at is about 85 characters).

The host now hands the game the 8.3 short form of its folder (`host.c`,
`GetShortPathNameA`):

```
  guest exe C:\Users\<user>\AppData\Local\Temp\...\SCRATC~1\clean\civ3\game\CONQUE~1\Civ3Conquests.exe
[record] first blit to the game window
```

On a volume with 8.3 names turned off this does nothing; keep the repository
in a short path there.
