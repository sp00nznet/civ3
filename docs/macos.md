# Native macOS port

This branch adds a native **64-bit Apple Silicon or Intel** build of the
portable Civ3 host. It uses pcrecomp's Win32 compatibility layer, SDL2 and
FFmpeg. Windows, Wine and Rosetta are not part of this build path.

**Status: experimental port.** Synthetic tests exercise the real host using
small original test fixtures. They do not establish that Civilization III
boots or plays on macOS. Full game verification needs a Mac and the supported
Steam game files. Do not describe a successful synthetic test run as a
successful game test.

## What you need

- A 64-bit Mac with Xcode Command Line Tools and a working native Homebrew
  installation. On Apple Silicon, use `/opt/homebrew`, not an Intel Homebrew
  installation running through Rosetta.
- Python 3.10 or newer, CMake, Ninja, pkg-config, SDL2, SDL2_ttf and FFmpeg.
- Your own **Windows Steam Civilization III Complete** installation
  (Steam app 3910), including the entire install directory and its `Conquests`
  folder. The original Mac release, retail discs and GOG executables are not
  supported by the host's fixed game addresses.
- Several GB of free disk space for the runtime, generated C and build output.

The expected `Conquests/Civ3Conquests.exe` SHA-1 is
`01f705b1ce6f5d1cfd5a861f88ca1413764f8019`, as recorded by the upstream
[reconnaissance](RECON.md). Setup checks it before starting the long build.
Copy the full Steam installation from a machine where you have installed your
copy. This project does not download or distribute the game.

## First build

In Terminal, from the repository folder:

```sh
xcode-select --install
brew install python cmake ninja pkgconf sdl2 sdl2_ttf ffmpeg
./setup-macos.sh --game-dir "/path/to/Sid Meier's Civilization III Complete"
```

If Command Line Tools are already installed, skip that first command.
Alternatively, with Homebrew and Python already installed:

```sh
./setup-macos.sh --install-deps --game-dir "/path/to/Sid Meier's Civilization III Complete"
```

Setup:

1. Checks the game executable and the native compiler/libraries.
2. Clones the exact pcrecomp revision in `pcrecomp-revision.txt` into
   `.deps/pcrecomp`, keeping existing checkouts untouched. `PCRECOMP` may name
   an existing clean checkout at that revision.
3. Links `game/` to your install. **Playing writes saves and `conquests.ini`
   into that install.** Use a separate copy if you want to keep the source
   installation unchanged.
4. Installs the Python dependencies in a repository-local `.venv`.
5. Catalogs and recompiles the executable, `jgl.dll` and `sound.dll`.
6. Builds `build-macos/civ3` and creates `Play-Civilization-III.command`.

The initial catalog/lift/build can take a substantial amount of time. Long
stages log under `work/macos-*.log`. Successful stages are recorded in
`work/macos-setup.json`. Rerunning the same command resumes them. Changes to
the game binaries, pinned runtime, lifter or Python dependency versions
invalidate that record. `--force` reruns the generated stages. It never
deletes a real `game/` directory.

The build defaults to four parallel compiler jobs. To reduce peak memory:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=2 ./setup-macos.sh --game-dir "/path/to/Complete"
```

After setup, double-click `Play-Civilization-III.command`, or run:

```sh
./build-macos/civ3 --run
```

Run the binary from the repository folder. The generated launcher changes to
that folder automatically, including when its name contains spaces or quotes.
`Setup-macos.command` is a Finder entry point to setup once the game can be
found in `game/` or the usual Steam library.

## Check without a commercial game

These tests compile the real Civ3 host and the portable runtime, substituting
only a tiny original guest for the generated game. No commercial assets,
binary excerpts or generated game source are used or uploaded.

```sh
git clone https://github.com/sp00nznet/pcrecomp.git ../pcrecomp
git -C ../pcrecomp checkout --detach "$(cat pcrecomp-revision.txt)"
PCRECOMP=../pcrecomp CIV3_BUILD_TESTS_ONLY=1 BUILD_DIR=build-check ./build-macos.sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
```

Use a new directory for that clone, or an existing clean checkout at the
required revision. Tests cover:

- high host pointers and 32-bit guest address translation;
- the real executable mapping/binding/startup sequence;
- both DLL images mapped before the guest heap can claim their addresses;
- deferred DLL initialization and repeated `LoadLibraryA`;
- exported functions looked up by name and ordinal, then called;
- missing exports, missing game paths and the `--help` path;
- headless startup and SDL window creation using its dummy video driver;
- the runtime's memory/ABI, PE loader, synthetic window/message loop and GDI tests;
- setup input validation and a Finder launcher run after moving its folder.

`.github/workflows/native-host.yml` runs these on standard Apple Silicon and
Intel macOS runners, plus Linux x86-64. The workflow never builds or publishes
the commercial game. A workflow definition alone is not evidence that its
jobs have passed; inspect the actual run results.

## What changed technically

The old CMake entry point required a 32-bit host and compiled every POSIX
target with `RECOMP_FLAT_MEMORY`. On a modern Mac the host is 64-bit, while
the game's addresses remain 32-bit. The new build uses pcrecomp's existing
based memory model: the guest's 4 GB address space occupies a separate
mapping, and `hle_ptr`/`MEM32` translate addresses into it. This is a virtual
address reservation, not a requirement for 4 GB of immediately resident RAM.

The Civ3 host no longer uses Linux-only `MAP_FIXED_NOREPLACE` directly. It
maps the two lifted DLLs through the portable loader before the runtime can
allocate the guest heap. It defers import binding and `DllMain` until the
game calls `LoadLibraryA`, matching the prior initialization order without
an unmap/re-map race. Export-name comparisons now translate both guest
addresses with `hle_ptr`; casting either to a C pointer would crash a
64-bit build.

The 32-bit Linux build keeps its flat mapping and ELF linker placement.
macOS receives no `-m32` or ELF linker flags. Its SDL window remains on the
native main thread and gets a 16 MB main-thread stack for deep lifted calls.
The Windows MSVC x86 build remains available.

## Full game acceptance checks still required

On a real Mac with the supported files:

```sh
./build-macos/civ3 --headless
./build-macos/civ3 --headless --run --watchdog 120
./build-macos/civ3 --run
```

Confirm the main menu, start a game, found a city, end several turns, open the
advisors, save, quit, and load that save. Also check the actual Cocoa window,
Retina scaling, keyboard/mouse, sound/music, movies and fullscreen. The
dummy-video tests cannot verify these. Upstream multiplayer remains untested.

The product is a local build using Homebrew libraries, not a signed,
notarized, self-contained downloadable application. Do not redistribute
executables containing lifted commercial game code.
