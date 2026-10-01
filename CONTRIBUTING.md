# Contributing to the Civilization III recomp

This repository is the game-specific half: the lift driver, the host, the
setup script and the bring-up notes. The recompiler itself is
[pcrecomp](https://github.com/sp00nznet/pcrecomp). Contributions to either are
welcome.

## Where the gaps are

| Area | What is missing | Difficulty |
|---|---|---|
| **Console play** | Every run so far was headless. Windowed and full-screen play on a real display, with the issues it finds. | Small |
| **A turn milestone** | The game's turn counter address, so `tools/conformance.py` can check that a turn ended. | Small |
| **Longer scripted games** | Several turns, a save and a load, as a conformance run. | Medium |
| **Bink in `--record`** | The intro and wonder movies present through binkw32, not jgl, so recordings miss them. | Medium |

## Ground rules

**No game files, ever.** No executables, DLLs, `.biq`s, art, movies or audio.
No lifted code either: `work/` and `src/recomp/gen/` are generated from your
own copy and gitignored, and must stay that way. Screenshots of what the
recompiled game renders are fine.

**Where your code comes from.** Contributions must be your own work or under
an MIT-compatible licence. The easy mistake is porting a fix you saw in a GPL
project (Wine, DOSBox, a GPL decompilation), which relicenses it by accident
and is very hard to untangle later. If you port anything, say where it came
from in the pull request.

**Generic fixes go upstream.** If the bug is in the lifter, the catalog or the
runtime, it belongs in pcrecomp as its own PR, and this repo only picks it up.
Game-specific values and shims stay here.

**Say what you measured.** Show the run before and after
(`py -3 tools/conformance.py`), and the log line or frame that changed. Every
wall so far has an entry in [docs/bringup.md](docs/bringup.md) with the real
output; add one for yours.

**AI-assisted contributions** are welcome, provided a human understood and
verified the change.

## Before you open a pull request

With your own copy set up (README, Getting Started):

```
py -3 run_lift.py --all
build.cmd
py -3 tools\conformance.py
```

`conformance.py` fails on a regression against `conformance.json`. Update the
baseline (`--update`) only when the change is meant to move it, and say so.
