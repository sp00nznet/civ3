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
its own single-instance mutex (host.c). A main-menu item takes one click or
two (docs/bringup.md, 4; a race with the hover), so menu() uses --open.
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
    """Hover, then click until the screen changes (--open): one click or two,
    depending on whether the menu took the hover first."""
    y = MENU[item]
    return ['--move', '240,%d@%g' % (y, at), '--open', '240,%d@%g' % (y, at + 2)]


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
    # (more cases are added below: civ-<name>)
    'quick-autopilot': (menu('quick') + ['--move', '786,258@28', '--click', '786,258@30',
                                         '--key', '0x42@36', '--key', '0x0D@48', '--key', '0x1B@62',
                                         '--autopilot', '3000@70'], 1800),
}


# New Game as each civilization: a Tiny world (Choose Your World), the civ's
# radio button on Player Setup, OK. The Dawn of Civilization popup names the
# leader and the civ's two traits, which is what to check on the sheet.
CIVS = [['Rome', 'Greece', 'Germany', 'China', 'Japan', 'India', 'Aztecs', 'Iroquois',
         'Mongols', 'Scandinavia', 'Celts', 'Carthage', 'Sumeria', 'Netherlands',
         'Byzantines', 'Maya'],
        ['Egypt', 'Babylon', 'Russia', 'America', 'France', 'Persia', 'Zululand',
         'England', 'Spain', 'Ottomans', 'Arabia', 'Korea', 'Hittites', 'Portugal', 'Inca']]


def new_game(civ_x, civ_y):
    return menu('new', 4) + ['--click', '229,136@18', '--click', '969,740@22',
                             '--click', '%d,%d@30' % (civ_x, civ_y), '--click', '969,740@33']


for col, names in enumerate(CIVS):
    for row, civ in enumerate(names):
        CASES['civ-' + civ.lower()] = (new_game(219 + 137 * col, 129 + 21 * row), 55)


# The Conquests! scenarios: Load Scenario lists them 30 px apart from y=127;
# OK (972,744), then the scenario's Player Setup with Random selected, OK.
# The autopilot then takes the intro popups and plays the turns.
SCENARIOS = ['mesopotamia', 'rise-of-rome', 'fall-of-rome', 'middle-ages', 'mesoamerica',
             'age-of-discovery', 'sengoku', 'napoleonic', 'wwii-pacific',
             'intro1-ancient-treasures', 'intro2-three-sisters', 'intro3-new-alliances']
for i, sc in enumerate(SCENARIOS):
    CASES['scen-' + sc] = (menu('conquests', 4) + ['--click', '200,%d@20' % (127 + 30 * i),
                                                    '--click', '972,744@24', '--click', '969,740@40',
                                                    '--autopilot', '4000@90'], 300)


# In game: load "0000 oracle.SAV" (the oracle's start, tools/oracle.py), wait
# until the save has loaded (the turn number at 0xA74EA4 turns nonzero; the
# load took 26 s with eight runs at once and over 60 s with nine, so no fixed
# time held), take the "Saved Game" popup with Enter, open one screen, close
# it (Esc, then Enter for a dialog that wants it), then the autopilot plays.
# Times after the --wait count from when it ended. A case passes when it ran
# clean and the game still played at least 3 turns after the screen: the
# autosaves count them. The sheet shows the right screen opened.
TURN = '0xA74EA4'
LOADED = menu('load', 4) + ['--click', '621,557@16', '--wait', TURN + '@17', '--key', '0x0D@21']


def in_game(*action, close=('--key', '0x1B@34', '--key', '0x0D@37')):
    return LOADED + list(action) + list(close) + ['--autopilot', '3000@46'], 240


def key(k, at=26):
    return ['--key', '%s@%g' % (k, at)]


# The hotkeys from the Civilopedia's "Hotkeys: Game Controls" page.
HOTKEYS = {'domestic': '0x70', 'trade': '0x71', 'military': '0x72', 'foreign': '0x73',
           'cultural': '0x74', 'science': '0x75', 'wonders': '0x76', 'victory': '0x77',
           'palace': '0x78', 'spaceship': '0x79', 'demographics': '0x7A',
           'prefs': 'c+0x50', 'audio': 's+0x53', 'government': 's+0x47', 'mobilization': 's+0x4D',
           'civilopedia': 'c+0x43', 'locate-city': 's+0x4C', 'grid': 'c+0x47', 'zoom': '0x5A',
           'capital': '0x48', 'clear-map': 'cs+0x4D', 'load': 'c+0x4C',
           'retire': 'c+0x51', 'new-game': 'cs+0x51', 'quit': '0x1B'}
for n, k in HOTKEYS.items():
    CASES['game-' + n] = in_game(*key(k))
# Saving: Ctrl-S, Enter takes the offered name; the save lands in Saves.
CASES['game-save'] = in_game(*key('c+0x53'), close=key('0x0D', 32))
# End Turn by its own key rather than the autopilot's Enter.
CASES['game-end-turn'] = in_game(*key('s+0x0D'), close=())
# The three buttons at the top left (Menu, Civilopedia, Advisors) and the
# seven advisor buttons along the bottom of the map.
for i, b in enumerate(['menu', 'pedia', 'advisors']):
    CASES['game-btn-' + b] = in_game('--click', '%d,27@26' % (36 + 37 * i))
for i in range(7):
    CASES['game-btn-bottom%d' % (i + 1)] = in_game('--click', '%d,733@26' % (405 + 32 * i))

# Each main-menu screen, then Esc, then Exit from the main menu it went back
# to: a clean exit (0) proves the menu came back and still works. (The menu
# flag the [game] milestones read is not set again on the way back.)
for item in ['new', 'load', 'conquests', 'content', 'hof', 'prefs', 'multi', 'credits']:
    CASES['back-' + item] = (menu(item) + ['--key', '0x1B@25'] + menu('exit', 32), 60)

# What a case must show besides running clean: turns the game autosaved, or
# that the game exited by itself.
EXPECT = {n: {'turns': 3} for n in CASES if n.startswith('game-')}
EXPECT.update({n: {'exit': 0} for n in CASES if n.startswith(('back-', 'menu-exit'))})


def farm(d):
    """A private game folder for one case, made of hard links to game/.

    The game writes into its own folder: fixed-name temp files
    (bic__in_.tmp, bic__out.tmp, save0.tmp), autosaves, conquests.ini and a
    regenerated LSANS.fot. Eight cases sharing one folder clobbered each
    other ("Could not open scenario file", "FILE NOT FOUND"). Links cost no
    space, and a file the game deletes and writes anew only changes this copy;
    the .ini files it rewrites in place are copied.
    """
    src = os.path.join(ROOT, 'game')
    for dirpath, dirs, files in os.walk(src):
        out = os.path.join(d, os.path.relpath(dirpath, src))
        os.makedirs(out, exist_ok=True)
        if os.path.normcase(os.path.relpath(dirpath, src)) == os.path.normcase(os.path.join('Conquests', 'Saves', 'Auto')):
            continue                  # a case's autosaves are its own results
        for f in files:
            t = os.path.join(out, f)
            if f.lower().endswith('.ini'):
                # Rewritten in place, not replaced: a link would share it with
                # every other case and with game/ itself. A boot that read it
                # mid-write played the intro, and Bink faulted headless.
                # Without "Latest Save" or "Latest Scenario" every case starts on the same main
                # menu: with it, a "Play Last World" item is added on top.
                if os.path.exists(t):
                    os.remove(t)
                # PlayIntro=0, NoSound=1: over RDP the intro faulted in
                # binkw32's BinkBufferOpen and Exit blocked in sound.dll, in
                # waves: from one minute on, whole batches failed at boot.
                with open(os.path.join(dirpath, f), 'rb') as src_ini, open(t, 'wb') as out_ini:
                    out_ini.write(b''.join(l for l in src_ini.readlines()
                                          if not l.startswith((b'Latest Save=', b'Latest Scenario=',
                                                               b'PlayIntro=', b'NoSound='))))
                    if f.lower() == 'conquests.ini':
                        out_ini.write(b'\r\nPlayIntro=0\r\nNoSound=1\r\n')
            elif not os.path.exists(t):
                os.link(os.path.join(dirpath, f), t)
    return os.path.join(d, 'Conquests')


def run(name, args, seconds, every):
    d = os.path.join(OUT, name)
    os.makedirs(d, exist_ok=True)
    game = farm(os.path.join(d, 'game'))
    # A previous run's autosaves would count as this run's turns.
    auto = os.path.join(game, 'Saves', 'Auto')
    for f in os.listdir(auto) if os.path.isdir(auto) else []:
        os.remove(os.path.join(auto, f))
    mp4, log = os.path.join(d, 'run.mp4'), os.path.join(d, 'run.log')
    # --nosteam: parallel runs polling the Steam client stalled before the
    # menu; the game reports the missing Steam and plays on (docs/bringup.md).
    cmd = [HOST, '--headless', '--nosteam', '--run', '--watchdog', '600', '--play', str(seconds),
           '--record', mp4, '--game', game] + args
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
    bad = [l for l in text.splitlines() if l.startswith(('===', '[not-lifted]', 'ITAIL', '[messagebox]'))
           and 'Steam must be running' not in l]
    game = [l[7:] for l in text.splitlines() if l.startswith('[game]')]
    # The game autosaves every turn as "Conquests Autosave <year>.SAV": the
    # turns a case really played, as the game itself counts them.
    auto = os.path.join(d, 'game', 'Conquests', 'Saves', 'Auto')
    years = sorted((os.path.getmtime(os.path.join(auto, f)), f[19:-4])
                   for f in os.listdir(auto) if f.startswith('Conquests Autosave')) if os.path.isdir(auto) else []
    if years:
        game.append('%d turns, %s to %s' % (len(years), years[0][1], years[-1][1]))
    exp = EXPECT.get(name, {})
    if len(years) < exp.get('turns', 0):
        bad.append('only %d turns' % len(years))
    if 'exit' in exp and code != exp['exit']:
        bad.append('expected exit %d' % exp['exit'])
    elif code not in (0, 4):
        bad.append('exit %s' % code)
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
    failed = 0
    names = [n for n in CASES if not a.cases or any(fnmatch.fnmatch(n, p) for p in a.cases)]
    with ThreadPoolExecutor(a.jobs) as ex:
        for name, code, bad, game in ex.map(lambda n: run(n, *CASES[n], a.every), names):
            print('%s %-22s exit %-7s %s%s' % ('FAIL' if bad else 'pass', name, code, '; '.join(game) or '-',
                                                   ('  !! ' + ' | '.join(bad[:3])) if bad else ''))
            failed += bool(bad)
    print('%d of %d failed' % (failed, len(names)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
