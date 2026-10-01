#!/usr/bin/env python3
"""Scripted play tests: run named cases headless, in parallel, and keep the evidence.

Each case is a list of host arguments (mostly --move/--click/--key scripts).
A run leaves, in work/tests/<case>/:

    run.log      everything the host printed
    run.mp4      the recording
    sheet.png    a contact sheet, one frame every --every seconds, for review

and the summary line says how it ended: the exit code (4 = the watchdog, the
normal end of a timed run), any fault or not-lifted report, and the [game]
milestones. Reading the sheets is the actual test: this tells you a case
crashed, not that the game did the right thing.

    py -3 tools/playtest.py --list
    py -3 tools/playtest.py menu-newgame menu-load
    py -3 tools/playtest.py 'menu-*' --jobs 4

Several instances can run at once: under --headless the host gives each one
its own single-instance mutex (host.c). Main-menu clicks come in pairs: the
first click on an item selects it, the second activates it (docs/bringup.md, 4).
"""
import argparse
import fnmatch
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, 'build', 'civ3.exe')
OUT = os.path.join(ROOT, 'work', 'tests')

# Main menu items, 1024x768 client pixels, clicked at x=240: an item's hit box
# is its text width + 64 px from x=187 (sub_00559BE0), so a short label like
# Exit ends near x=280. The list grows upward when "Play
# Last World" is present, so these hold either way.
MENU = {'last': 299, 'new': 341, 'quick': 383, 'load': 425, 'conquests': 467,
        'content': 509, 'hof': 551, 'prefs': 593, 'multi': 635, 'credits': 677,
        'exit': 719}


def menu(item, at=6):
    y = MENU[item]
    return ['--move', '240,%d@%g' % (y, at), '--click', '240,%d@%g' % (y, at + 2),
            '--click', '240,%d@%g' % (y, at + 5)]


# name: (host arguments, seconds of play after the first frame)
CASES = {
    'menu-idle':      ([], 40),
    'menu-new':       (menu('new'), 60),
    'menu-quick':     (menu('quick'), 90),
    'menu-load':      (menu('load'), 60),
    'menu-conquests': (menu('conquests'), 60),
    'menu-content':   (menu('content'), 60),
    'menu-hof':       (menu('hof'), 60),
    'menu-prefs':     (menu('prefs'), 60),
    'menu-multi':     (menu('multi'), 60),
    'menu-credits':   (menu('credits'), 90),
    'menu-exit':      (menu('exit'), 60),
    'menu-last':      (menu('last'), 90),
    # Quick Start, then let the turns roll with the AI playing its own.
    'quick-autopilot': (menu('quick') + ['--autopilot', '3000@30'], 900),
}


def run(name, args, seconds, every):
    d = os.path.join(OUT, name)
    os.makedirs(d, exist_ok=True)
    mp4, log = os.path.join(d, 'run.mp4'), os.path.join(d, 'run.log')
    cmd = [HOST, '--headless', '--run', '--play', str(seconds), '--record', mp4] + args
    with open(log, 'w', errors='replace') as f:
        try:
            code = subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT,
                                  timeout=seconds + 900).returncode
        except subprocess.TimeoutExpired:
            code = 'timeout'
    text = open(log, errors='replace').read()
    if os.path.exists(mp4):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', mp4, '-vf',
                        'fps=1/%g,scale=320:-1,tile=4x4' % max(every, seconds / 16.0), '-frames:v', '1',
                        os.path.join(d, 'sheet.png')], cwd=ROOT)
    bad = [l for l in text.splitlines() if l.startswith(('===', '[not-lifted]', 'ITAIL', '[messagebox]'))]
    game = [l[7:] for l in text.splitlines() if l.startswith('[game]')]
    return name, code, bad, game


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('cases', nargs='*', help='case names or globs (default: all)')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--every', type=float, default=5, help='contact-sheet spacing, seconds')
    a = ap.parse_args()
    if a.list:
        for n, (args, s) in CASES.items():
            print('%-18s %4ds  %s' % (n, s, ' '.join(args)))
        return 0
    if not os.path.exists(HOST):
        print('playtest: needs build/civ3.exe (README, Building from source)')
        return 0
    names = [n for n in CASES if not a.cases or any(fnmatch.fnmatch(n, p) for p in a.cases)]
    with ThreadPoolExecutor(a.jobs) as ex:
        for name, code, bad, game in ex.map(lambda n: run(n, *CASES[n], a.every), names):
            print('%-18s exit %-7s %s%s' % (name, code, '; '.join(game) or '-',
                                            ('  !! ' + ' | '.join(bad[:3])) if bad else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
