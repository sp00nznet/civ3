#!/usr/bin/env python3
"""Differential test: the recompiled game against the original machine code.

Both runs use one host (build/civ3.exe) and one script; the only difference is
--original, which runs the shipping Civ3Conquests.exe's own code instead of the
lifted C (src/runtime/oracle.c). The script loads a fixed save, then the
autopilot ends turn after turn, the AI playing every civilization's moves.
The game autosaves each turn, and the autosaves are the comparison: the save
is the whole game state (map, cities, units, AI, treasury, research, the
random seed), so every year both runs reached must match.

Not byte for byte. Two runs of the *same* build already differ in a few
places: a GUID in the header, and some values that look like timing (GAME+0x1CB9,
GAME+0x1F0D, two ints in each LEAD record). The save also holds raw pointers:
heap addresses, which differ between the two builds' heaps, and an
uninitialized buffer (GAME+0x2B7x) where the original's own return
addresses (0x005CD16D...) sit and the recomp's guest stack has zeros. So each
build runs twice, and a byte only counts as a divergence if it is the same
in both runs of each build, and it is not part of a pointer or zero on both sides.

    py -3 tools/oracle.py                 # 600 s of play, four runs at once
    py -3 tools/oracle.py --seconds 1200
    py -3 tools/oracle.py --compare       # re-read the last runs' autosaves

The start save is game/Conquests/Saves/"0000 oracle.SAV" (sorted first, so the
Load Game dialog preselects it). Any save works; this one is a 3900 BC Quick
Start game made by a scripted run (docs/testing.md).
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import playtest  # noqa: E402

START = os.path.join(playtest.ROOT, 'game', 'Conquests', 'Saves', '0000 oracle.SAV')
# Load Game, OK on the preselected "0000 oracle.SAV", wait for the load, then turns.
SCRIPT = playtest.LOADED + ['--autopilot', '3000@26']


def autosaves(case):
    d = os.path.join(playtest.OUT, case, 'game', 'Conquests', 'Saves', 'Auto')
    if not os.path.isdir(d):
        return {}
    return {f[19:-4]: os.path.join(d, f) for f in os.listdir(d) if f.startswith('Conquests Autosave')}


RUNS = {'oracle-recomp': [], 'oracle-recomp-2': [],
        'oracle-original': ['--original'], 'oracle-original-2': ['--original']}


def diffs(a, b):
    return {i for i, (x, y) in enumerate(zip(a, b)) if x != y}


def pointerish(d):
    v = int.from_bytes(d, 'little')
    return v == 0 or 0x00400000 <= v < 0x80000000


def is_pointer(a, b, i):
    """Byte i lies in a dword that differs and reads as zero or an address
    in both saves. Records are not dword-aligned in the file, so try each
    start. ponytail: a small value next to zero bytes can also read as an
    address (0x00A20000); the two-run noise mask is the main filter."""
    return any(s >= 0 and a[s:s + 4] != b[s:s + 4] and pointerish(a[s:s + 4]) and pointerish(b[s:s + 4])
               for s in range(i - 3, i + 1))


def compare(year, r, r2, o, o2):
    """Divergent offsets between the builds for one year; None if a size differs."""
    if len({len(x) for x in (r, r2, o, o2)}) != 1:
        return None
    noise = diffs(r, r2) | diffs(o, o2) | set(range(0x0E, 0x1E))     # 0x0E-0x1D: the GUID
    return sorted(i for i in diffs(r, o) - noise if not is_pointer(r, o, i))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--seconds', type=int, default=600)
    ap.add_argument('--compare', action='store_true', help='only compare the last runs')
    a = ap.parse_args()
    if not os.path.exists(START):
        print('oracle: needs %s (docs/testing.md)' % START)
        return 0
    if not a.compare:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(len(RUNS)) as ex:
            for name, code, bad, game in ex.map(lambda n: playtest.run(n, SCRIPT + RUNS[n], a.seconds, 5), RUNS):
                print('%-18s exit %-7s %s%s' % (name, code, '; '.join(game) or '-',
                                                ('  !! ' + ' | '.join(bad[:3])) if bad else ''))
    saves = {n: autosaves(n) for n in RUNS}
    common = sorted(set.intersection(*(set(s) for s in saves.values())),
                    key=lambda y: os.path.getmtime(saves['oracle-recomp'][y]))
    print('years in all four runs: %d' % len(common))
    same = 0
    for y in common:
        r, r2, o, o2 = (open(saves[n][y], 'rb').read() for n in RUNS)
        d = compare(y, r, r2, o, o2)
        noise = len(diffs(r, o)) if d is not None else 0
        if d == []:
            same += 1
            print('  %-10s same game state (%d bytes; %d differ, all noise)' % (y, len(r), noise))
        elif d is None:
            print('  %-10s DIFFERENT sizes: %s' % (y, ' / '.join(str(len(x)) for x in (r, r2, o, o2))))
        else:
            print('  %-10s DIFFERENT: %d bytes, first at 0x%X: recomp %s, original %s'
                  % (y, len(d), d[0], r[d[0]:d[0] + 8].hex(), o[d[0]:d[0] + 8].hex()))
    print('same: %d/%d' % (same, len(common)))
    return 0 if common and same == len(common) else 1


if __name__ == '__main__':
    sys.exit(main())
