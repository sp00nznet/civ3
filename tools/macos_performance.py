#!/usr/bin/env python3
"""A/B idle-menu CPU measurements with private game copies and identical framebuffers.

This measures CPU consumed by a fixed-duration menu run, including startup and
capture overhead. It is not a gameplay FPS, AI-turn-time or battery-life benchmark.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import time
from macos_roundtrip import digest, private_game
from macos_qa_cleanup import remove_clones
from macos_smoke import assess
ROOT = Path(__file__).resolve().parents[1]


def summary(records: list[dict]) -> dict:
    if not records or any(r.get('failures') for r in records):
        raise ValueError('All benchmark runs must pass their runtime checks')
    image_hashes = {r.get('framebuffer_sha256') for r in records}
    if None in image_hashes or len(image_hashes) != 1:
        raise ValueError('Baseline/candidate menu pixels differ; do not claim a like-for-like speedup')
    result = {}
    for variant in ('baseline', 'candidate'):
        rows = [r for r in records if r['variant'] == variant]
        if not rows or any(r['cpu_seconds'] <= 0 for r in rows):
            raise ValueError('Both variants need positive measured CPU times')
        result[variant] = {'runs': len(rows), 'median_cpu_seconds': statistics.median(r['cpu_seconds'] for r in rows),
                           'median_elapsed_seconds': statistics.median(r['elapsed_seconds'] for r in rows),
                           'max_peak_rss_bytes': max(r['peak_rss_bytes'] for r in rows)}
    if result['baseline']['runs'] != result['candidate']['runs']:
        raise ValueError('Unequal run counts')
    result['cpu_reduction_percent'] = 100 * (1 - result['candidate']['median_cpu_seconds'] / result['baseline']['median_cpu_seconds'])
    result['framebuffers_identical'] = True
    return result


def wait_with_usage(process: subprocess.Popen, timeout: float):
    """Reap exactly this child, retaining its own CPU/RSS rather than cumulative siblings."""
    deadline = time.monotonic() + timeout
    terminating = killed = False
    while True:
        pid, status, usage = os.wait4(process.pid, os.WNOHANG)
        if pid:
            process.returncode = os.waitstatus_to_exitcode(status)
            return process.returncode, usage, terminating
        if time.monotonic() >= deadline:
            if not terminating:
                process.terminate(); terminating = True; deadline = time.monotonic() + 15
            elif not killed:
                process.kill(); killed = True; deadline = time.monotonic() + 15
        time.sleep(.02)


def run_case(binary: Path, game: Path, output: Path, variant: str, index: int, seconds: int, windowed: bool = False) -> dict:
    stem = f'{index:02d}-{variant}'
    image, log_path = output / (stem + '.ppm'), output / (stem + '.log')
    command = [str(binary), '--run', '--watchdog', '60', '--play', str(seconds),
               '--game', str(game), '--exe', str(ROOT / 'work/Civ3Conquests.exe'),
               '--work', str(ROOT / 'work'), '--shot', str(image)]
    if not windowed: command.append('--headless')
    started = time.monotonic()
    with log_path.open('x') as stream:
        process = subprocess.Popen(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
        try:
            code, usage, timed_out = wait_with_usage(process, seconds + 75)
        except BaseException:
            if process.returncode is None:
                process.terminate()
                try: process.wait(timeout=15)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
            raise
    log = log_path.read_text(errors='replace')
    failures = assess(code, log, image)
    if timed_out: failures.append('External deadline exceeded')
    return {'variant': variant, 'exit': code, 'elapsed_seconds': time.monotonic() - started,
            'cpu_seconds': usage.ru_utime + usage.ru_stime, 'user_seconds': usage.ru_utime,
            'system_seconds': usage.ru_stime, 'peak_rss_bytes': usage.ru_maxrss,
            'framebuffer_sha256': digest(image) if image.is_file() else None, 'failures': failures,
            'log': log_path.name, 'framebuffer': image.name}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, default=ROOT / 'build-macos/civ3')
    parser.add_argument('--seed-save', type=Path, required=True)
    parser.add_argument('--game-dir', type=Path, default=ROOT / 'game')
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--seconds', type=int, default=12)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--keep-test-games', action='store_true',
                        help='Keep cloned game copies and test saves for debugging (off by default)')
    parser.add_argument('--windowed', action='store_true', help='Use real native windows; otherwise headless')
    args = parser.parse_args(argv)
    if sys.platform != 'darwin': parser.error('This benchmark reports macOS wait4 RSS units and uses APFS cloning')
    if not 1 <= args.repeats <= 7 or not 8 <= args.seconds <= 60: parser.error('Use repeats 1..7 and seconds 8..60')
    baseline, candidate, seed, source = [p.expanduser().resolve() for p in (args.baseline, args.candidate, args.seed_save, args.game_dir)]
    if not all(p.is_file() for p in (baseline, candidate, seed)) or not (source/'Conquests').is_dir():
        parser.error('Two real native executables, an owned save and an installed game are required')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = (args.output or ROOT/'work'/f'performance-{stamp}').expanduser().resolve()
    if output.exists() or output.is_relative_to(source): parser.error('Output must be a new directory outside the source game')
    output.mkdir(parents=True)
    preferences = source/'Conquests/conquests.ini'
    before = digest(preferences) if preferences.is_file() else None
    seed_hash = digest(seed)
    report = {'success': False, 'baseline_sha256': digest(baseline), 'candidate_sha256': digest(candidate),
              'seconds_at_menu': args.seconds, 'windowed': args.windowed, 'runs': []}
    try:
        games = {name: private_game(source,output/(name+'-game'),seed,'0000 CPU benchmark.SAV') for name in ('baseline','candidate')}
        binaries = {'baseline': baseline, 'candidate': candidate}
        for repeat in range(args.repeats):
            order = ('baseline','candidate') if repeat%2==0 else ('candidate','baseline')
            for name in order:
                print(f'Measuring {name} {repeat+1}/{args.repeats}',flush=True)
                row = run_case(binaries[name],games[name],output,name,len(report['runs']),args.seconds,args.windowed)
                report['runs'].append(row)
                (output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
                print(f"{name}: CPU {row['cpu_seconds']:.3f}s; exit {row['exit']}; failures {row['failures']}",flush=True)
        report['summary'] = summary(report['runs'])
        after = digest(preferences) if preferences.is_file() else None
        if before != after or seed_hash != digest(seed): raise RuntimeError('Source preferences/save changed')
        if report['baseline_sha256'] != digest(baseline) or report['candidate_sha256'] != digest(candidate):
            raise RuntimeError('An executable changed during measurement')
        report.update(success=True, source_unchanged=True)
        print(json.dumps(report['summary'],indent=2),flush=True)
        return 0
    except (OSError,RuntimeError,ValueError,subprocess.SubprocessError) as error:
        report['error'] = str(error);print(f'FAIL: {error}',file=sys.stderr);return 1
    finally:
        if not args.keep_test_games:
            try:
                removed = remove_clones(output, ('baseline-game', 'candidate-game',
                                                 'baseline-game-inherited-saves',
                                                 'candidate-game-inherited-saves'))
                report['temporary_copies_removed'] = removed
            except (OSError, ValueError) as error:
                report['cleanup_warning'] = str(error)
                print(f'WARNING: QA copy cleanup: {error}', file=sys.stderr)
        (output/'result.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__':
    raise SystemExit(main())
