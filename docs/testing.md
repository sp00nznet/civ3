# Testing

Two tools, both headless, both leaving their evidence in `work/tests/<case>/`
(`run.log`, `run.mp4`, `sheet.png`, and the game folder the case played in,
autosaves included).

## Play tests: `tools/playtest.py`

Named cases, each a script of `--move` / `--click` / `--open` / `--key` /
`--wait` events timed from the moment the main menu opens. They run in
parallel, each in a private game folder of hard links (docs/bringup.md, 10)
with its own copy of `conquests.ini`.

    py -3 tools/playtest.py --list
    py -3 tools/playtest.py 'game-*' --jobs 3
    py -3 tools/playtest.py --jobs 3      # everything

Keep `--jobs` low. With eight or nine at once (plus other work), the host
machine became unresponsive and its RDP session stopped reconnecting.

| Cases | What they do | Pass means |
|---|---|---|
| `menu-*` | Each main-menu item, opened | ran clean |
| `back-*` | Each main-menu screen, opened, then Esc, then Exit on the main menu | the game exited with 0 (so the menu came back and works) |
| `civ-*` | New Game as each of the 31 civilizations | ran clean; the sheet shows the leader |
| `scen-*` | Each Conquests! scenario, then the autopilot | ran clean |
| `game-*` | Load the oracle save, wait for it, open one in-game screen by its hotkey or button, close it, then play | ran clean and at least 3 turns were autosaved after it |
| `quick-autopilot` | Quick Start and 30 minutes of turns | ran clean |

"Ran clean" means no fault, no not-lifted report, no message box (except the
expected "Steam must be running" under `--nosteam`), and exit 4 (the
watchdog's normal end of a timed run) or 0 (Exit, Quit). The summary prints
`pass` or `FAIL` per case and the script exits 1 if any failed. The verdict
cannot tell that the *right* screen opened. The contact sheet shows that,
so look at the sheets for new cases.

Each case's `Saves/Auto` is emptied first: a previous run's autosaves once
counted as turns of a run that never reached the main menu.

### What the scripts can't assume

- **Fixed times.** Parallel runs load slower: loading the oracle save took
  26 s with eight at once and over 60 s with nine, so a key sent at a fixed
  time went to the Load dialog or the "Saved Game" popup. `--wait VA@s` holds
  the script until the dword at VA is nonzero and moves every later event by
  the wait. `0xA74EA4` is the turn number (0 at the menu, 2 at 3900 BC, one
  more each turn; found by diffing `--dump`s against the autosaves, with four
  more copies 8 KB apart), so `--wait 0xA74EA4@17` means "the save has loaded".
- **One click or two.** A main-menu item activates on the first click if the
  menu has already taken the hover, and on the second if not: a race. A click
  too many lands on the next screen. `--open x,y@s` clicks until 5% of the
  picture changes (Load Game's dialog is 17%, a highlight under 1%), up to
  five times, 2 s apart.
- **Window messages only.** The main menu you come back to (Esc from a
  screen) reads the button with `GetAsyncKeyState(VK_LBUTTON)`, not from
  messages. A scripted click holds that button down until the game has read
  it down, then up (input.c, `click`); a fixed 250 ms hold was missed under
  load.
- **Modifiers.** `--key` takes them: `c+0x53` is Ctrl-S, `s+0x47` Shift-G,
  `cs+0x51` Ctrl-Shift-Q. The game reads them with `GetKeyState` and
  `GetAsyncKeyState`, which a scripted run answers from the script, so the
  console's own keyboard never leaks into a test.

### The environment

- **`conquests.ini`** was hard-linked like everything else, so every case
  and the real install shared one file, which the game rewrites in place.
  Each case now gets a copy, without `Latest Save` and `Latest Scenario`.
- **Bink and sound over RDP.** The intro movie draws through DirectDraw on
  the real display even headless. Over RDP it faulted in binkw32's
  `BinkBufferOpen` (0x300016AD) in waves: from one minute on, every boot of a
  batch failed. Exit blocked in sound.dll the same way. The case copies set
  `PlayIntro=0` and `NoSound=1`. Not yet verified: the machine locked up
  before the run that would have checked them. Wonder movies are Bink too,
  so headless Bink needs a proper answer.
- **The recorder.** It used to write each frame into ffmpeg's pipe while
  holding the lock the game's blit hook takes; with eight encoders behind,
  Exit stalled in jgl's shutdown (0/8 exited). It now copies the frame under
  that lock and writes outside it (8/8 exited).

## Differential test: `tools/oracle.py`

The same host runs the shipping `Civ3Conquests.exe`'s own machine code with
`--original` (src/runtime/oracle.c): .text made executable, the imports bound
to real functions plus the same shims. The two builds then play the same
script: load `game/Conquests/Saves/0000 oracle.SAV` (named to sort first, so
the Load Game dialog preselects it; any save works), then the autopilot ends
turn after turn while the AI moves every civilization. Each turn's autosave
is the whole game state, so the builds are compared year by year.

    py -3 tools/oracle.py                 # 600 s, four runs at once (two per build)
    py -3 tools/oracle.py --compare       # re-read the last runs

The saves are not byte-identical, and need not be. Two runs of one build
already differ in a GUID in the header and in values that look like timing
(GAME+0x1CB9, GAME+0x1F0D, two ints in each LEAD record). The save also
stores raw pointers: heap addresses, which differ between the builds, and an
uninitialized buffer at GAME+0x2B7x, which holds the original's return
addresses (0x005CD16D, ...) and zeros in the recomp, whose guest stack holds
different leftovers. So each build runs twice. A byte counts as a divergence
only if both runs of each build agree on it and it is not part of a dword that
reads as zero or an address on both sides.

First result (3900 to 2950 BC, 19 turns, two runs of each build): the same
game state every turn; 42 to 58 bytes differed per save, all of them noise.
