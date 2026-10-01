#!/usr/bin/env python3
"""Civilization III: Conquests conformance harness (REPO_RULES section 9).

Two fixed corpora, one pass/fail count each, compared against the committed
baseline in conformance.json; a regression fails the run:

* **Boot milestones**: a headless run of build/civ3.exe, scored by the
  lines the host prints at each stage. The original game is the ground truth:
  each milestone is something it does on every start.
* **Lift health**: from the generated tree. Lift errors, bodies with no
  terminator, and RECOMP_ITAIL labels that cannot resolve at run time
  (their target is not in the dispatch table).

The game is not in the repo. Without game/ and build/civ3.exe this skips
with a message and exits 0, so it can sit in CI without the corpus.

    py -3 tools/conformance.py              # run, compare, print the table
    py -3 tools/conformance.py --update     # ...and accept the result as the baseline
"""
import argparse
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(ROOT, 'build', 'civ3.exe')
GEN = os.path.join(ROOT, 'src', 'recomp', 'gen')
BASELINE = os.path.join(ROOT, 'conformance.json')

# (name, what the host prints when it is reached). Order is boot order. The
# original game is the ground truth: it does each of these on every start.
MILESTONES = [
    ('image mapped and imports bound', r'guest exe '),
    ('entry point entered', r'entering 0x0066B0A9'),
    ('jgl.dll loaded', r'\[load\] .*jgl\.dll -> [0-9A-F]{8}'),
    ('game window created', r'\[window\] .*CreateWindowExA'),
    ('first frame presented', r'\[record\] first blit to the game window'),
    ('main menu open', r'\[game\] main menu open'),
    # The scripted Quick Start below: the main menu's first click on an item
    # only selects it, the second activates it (docs/bringup.md, 4).
    ('Quick Start taken, in a game', r'\[game\] main menu closed'),
]

# Seconds after the first frame. Two clicks on Quick Start (291,383).
SCRIPT = ['--move', '291,383@6', '--click', '291,383@8', '--click', '291,383@11']


def boot(seconds):
    try:
        p = subprocess.run([HOST, '--headless', '--run', '--watchdog', str(seconds),
                            '--record', os.path.join('work', 'conformance.mp4')] + SCRIPT,
                           cwd=ROOT, capture_output=True, text=True, errors='replace',
                           timeout=seconds + 60)
        out, code = p.stdout + p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        out, code = (e.stdout or '') + (e.stderr or ''), 'timeout'
        out = out if isinstance(out, str) else out.decode(errors='replace')
    passed = [name for name, pat in MILESTONES if re.search(pat, out)]
    last = [l for l in out.splitlines() if l.startswith(('===', '[not-lifted]', '[watchdog]'))]
    return passed, code, last[:2]


def lift_health():
    stats = json.load(open(os.path.join(ROOT, 'work', 'lift_stats.json')))
    disp = set(re.findall(r'\{ 0x([0-9A-F]{8})u,', open(os.path.join(GEN, 'recomp_dispatch.c')).read()))
    unresolved = sum(1 for fn in glob.glob(os.path.join(GEN, 'recomp_0*.c'))
                     for t in re.findall(r'L_([0-9A-F]{8}): RECOMP_ITAIL', open(fn).read())
                     if t not in disp)
    return {'lifted': stats['lifted'], 'errors': stats['errors'],
            'no_terminator': stats['no_terminator'], 'unresolved_itail': unresolved}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--update', action='store_true', help='accept this run as the baseline')
    ap.add_argument('--seconds', type=int, default=120, help='headless run length')
    args = ap.parse_args()
    if not (os.path.exists(HOST) and os.path.isdir(os.path.join(ROOT, 'game'))):
        print('conformance: skipped -- needs game/ (your copy) and build/civ3.exe '
              '(README, Building from source)')
        return 0

    passed, code, last = boot(args.seconds)
    health = lift_health()
    now = {'milestones': len(passed), 'of': len(MILESTONES), **health}
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else None

    print('boot milestones: %d/%d  (exit %s)' % (len(passed), len(MILESTONES), code))
    for name, _ in MILESTONES:
        print('  [%s] %s' % ('x' if name in passed else ' ', name))
    for l in last:
        print('  stopped: ' + l)
    print('lift: %(lifted)d functions, %(errors)d errors, %(no_terminator)d with no '
          'terminator, %(unresolved_itail)d unresolvable ITAIL labels' % health)

    worse = []
    if base:
        if now['milestones'] < base['milestones']:
            worse.append('milestones %d -> %d' % (base['milestones'], now['milestones']))
        for k in ('errors', 'no_terminator', 'unresolved_itail'):
            if now[k] > base[k]:
                worse.append('%s %d -> %d' % (k, base[k], now[k]))
    if args.update or not base:
        json.dump(now, open(BASELINE, 'w'), indent=1)
        print('baseline written to conformance.json')
    if worse:
        print('REGRESSION: ' + '; '.join(worse))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
