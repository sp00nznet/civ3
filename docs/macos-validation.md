# macOS port validation record

## Tested in the implementation session

Host: Linux x86-64, GCC 13.3.0, CMake 3.28.3, SDL2 2.30.0,
SDL2_ttf 2.22.0, FFmpeg 6.1 libraries. The build used the same **64-bit based
guest-memory model** selected for macOS, with a fixed guest-memory base at
`0x100000000000`. Runtime revision:
`ef3f0da6e08a8eb7a3f869273f3df071ca8d7373`.

| Check | Observed result |
| --- | --- |
| CMake configure, synthetic-test mode | Passed |
| Build of the actual POSIX host and full win32hle runtime | Passed |
| `civ3_host_smoke` | Passed: executable startup, both DLLs, import binding, deferred initialization, named/ordinal exports, calls, headless and dummy window paths |
| `hle_win32hle` | Passed |
| `hle_pe_loader` | Passed |
| `hle_host` | Passed |
| `hle_gdi` | Passed |
| Four Python setup tests | Passed: root/Conquests paths, missing DLL rejection, unknown executable rejection, launcher after moving a folder containing spaces and shell metacharacters |
| Python compilation, shell syntax, `git diff --check` | Passed |

Commands:

```sh
cmake -S . -B build-check -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCIV3_BUILD_GAME=OFF -DCIV3_BUILD_TESTS=ON
cmake --build build-check --parallel 4
ctest --test-dir build-check --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py' -v
python3 -m py_compile run_lift.py tools/setup_macos.py tests/host_smoke.py
sh -n setup-macos.sh build-macos.sh Setup-macos.command
git diff --check
```

CTest's completed run reported **5/5 passed**, zero failures. The build emits
existing possible-path-truncation warnings in `src/linux/main.c`; they are
not hidden by the new build.

## Not yet verified

- Apple Clang compilation/linking on either Apple Silicon or Intel macOS.
- The Homebrew installation and full catalog/lift/build sequence on macOS.
- A Cocoa/Retina window, real audio output, fullscreen or physical input.
- Booting or playing the commercial game, including save/load and scenarios.

The Mac was unavailable during the implementation session, and no commercial
game installation was supplied. The synthetic tests contain original test
code and generated test-only PE headers/data; they contain no Civ3 binary
code or assets. The native Mac CI jobs are defined but were not run in this
session. A green Linux test run cannot replace the Mac and gameplay checks
listed in [macos.md](macos.md).
