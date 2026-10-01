# Reconnaissance

What the binary is, why this build, and what the first pipeline pass found.
Every number here is from the commands shown; rerun them on your own copy.

## Which build

Civilization III ships as three executables: the 2001 original, *Play the
World* (2002) and *Conquests* (2003). Conquests contains everything the other
two do, so it is the target. The Steam "Complete" edition (app 3910) carries a
2015 rebuild of it:

```
$ py -3 ..\tools\tools\pe\pe_analyze.py work\Civ3Conquests.exe
  Machine:       i386
  Format:        PE32 (EXE)
  Image Base:    0x00400000
  Entry Point:   0x0066B0A9 (RVA 0x0026B0A9)
  Linker:        6.00
  Subsystem:     Windows GUI
  Timestamp:     2015-03-19 03:10:23 UTC (0x550A3E1F)
  Code Range:    0x00401000 - 0x00682000 (2,625,536 bytes)
  Data Range:    0x00682000 - 0x00CF315C (6,754,652 bytes)

    .text     VA 0x00001000  VSize 0x00281000  Raw 0x00001000  RSize 0x00281000  [CODE|EXEC|READ]
    .rdata    VA 0x00282000  VSize 0x0001B000  Raw 0x00282000  RSize 0x0001B000  [IDATA|READ]
    .data     VA 0x0029D000  VSize 0x0065615C  Raw 0x0029D000  RSize 0x000BB000  [IDATA|READ|WRITE]
    .rsrc     VA 0x008F4000  VSize 0x00003000  Raw 0x00358000  RSize 0x00003000  [IDATA|READ]

$ py -3 ..\tools\tools\pe\analyze_sections.py work\Civ3Conquests.exe
  No known protection/packer indicators found.
```

SHA-1 of the file this was done against:
`01f705b1ce6f5d1cfd5a861f88ca1413764f8019`.

Why the Steam build and not a retail disc:

- **No DRM.** The retail CDs carry disc copy protection; the Steam rebuild has
  no packer and no encrypted sections, so nothing has to be dumped or unpacked
  (The Movies' Steam build, by contrast, needed `emu_unpack.py`).
- **VC6, unrelocatable, file-aligned at 0x1000.** Raw offsets equal RVAs, and
  the image sits at 0x00400000 with no reason to move.
- **6.3 MB of `.data` is BSS** (765 KB on disk). The map, units and cities
  live in fixed global arrays, which a 1:1 mapped image gets for free.

The 2001 original and *Play the World* zips in the project folder are not used.

## Imports: 245 functions from 13 DLLs

| DLL | Count | Notes |
|---|---|---|
| `KERNEL32` | 118 | Statically linked VC6 CRT underneath |
| `USER32` | 33 | |
| `WSOCK32` | 20 | Multiplayer |
| `OPENGL32` | 17 | Only the map-overview / line drawing path; the main renderer is jgl.dll |
| `binkw32` | 16 | Intro and wonder movies |
| `steam_api` | 13 | Init, callbacks, lobbies. `steam_appid.txt` (3910) ships in the folder |
| `WINMM` | 10 | |
| `IFC23` | 7 | Immersion force-feedback mouse (TouchSense). Harmless without the hardware |
| `ADVAPI32`, `GDI32` | 3 each | |
| `comdlg32`, `ole32` | 2 each | |
| `SHELL32` | 1 | |

Loaded later by name (`LoadLibraryA` strings in the image): `jgl.dll`,
`.\sound.dll`, `DPLAYX.dll`, `MSVFW32.dll`.

**The renderer is not in the executable.** `jgl.dll` is Firaxis' 2D library:
it creates the game window, switches display modes, renders into DIB sections
and presents them with GDI `BitBlt`/`StretchBlt`. It imports no DirectDraw at
all. Because the host is 32-bit (`native32`), jgl runs as the real DLL and is
not lifted. That decides how headless capture works ([host.md](host.md)).

## C++ without RTTI

```
$ py -3 ..\tools\tools\cpp\rtti.py work\Civ3Conquests.exe -o work\rtti.json --seeds work\rtti_seeds.json
[*] classes          : 2
[*] virtual methods  : 3

$ py -3 ..\tools\tools\cpp\vtable_scan.py work\Civ3Conquests.exe --seeds work\vtable_seeds.json
[*] vtable candidates : 135  (.data: 6, .rdata: 129)
[*] distinct methods  : 1,799
[*] slots per vtable  : min 3, median 79, max 330
```

RTTI was off; the two classes it finds are the CRT's. The vtables are still
there as anonymous runs of code pointers, and those 1,799 methods seed the
catalog (a method called only through a vtable is invisible to a call scan).
A median of 79 slots per vtable is Firaxis' UI widget hierarchy.

## The catalog

```
$ py -3 ..\tools\tools\disasm\disasm32.py work\Civ3Conquests.exe -o work\functions.json --seed-functions work\vtable_seeds.json
[*] Total unique function candidates: 8756
[*] Data scan: probed 3368 pointer targets, rejected 2 that do not decode as code and 1048 that land inside an instruction
[*] Successfully disassembled 15576 functions (6 discovery rounds)
[*] Dropped 267 entries that are not instruction boundaries
[*] Clamped 3714 function extents to the next function start
[*] Functions: 15308  (thunks=31, leaves=4279)
[*] Instructions: 3,317,493
[*] Byte coverage: 2,500,807 / 2,625,536 (95.2% of code range)
```

About 10 minutes on this machine.

## The lift

```
$ py -3 run_lift.py --all
[*] catalog: 15308 functions inside .text
[*] vtable slots not in the catalog: 4 injected
[*] split entries (the middle of the function before them): 210, 34s
[*] branch and call targets outside the catalog, added as entries: 617
============================================================
  lifted 15929   not-lifted stubs 0   errors 0   no terminator 59
  6,299,192 lines of C in 40 files, 180.9s
============================================================
```
