#!/usr/bin/env python3
"""Exercise culture-linked Quick Start in isolated copies; never alter the source game."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
from macos_assets import fresh_settings, validate_assets
from macos_qa_cleanup import remove_clones
from macos_roundtrip import BAD, digest, turns
from macos_smoke import assess

ROOT = Path(__file__).resolve().parents[1]
CASES = {'random-tiny': (0, -1), 'persian-small': (1, 12), 'random-standard': (2, -1)}
ACTIONS = ['--move', '240,383@1', '--open', '240,383@2', '--move', '786,258@15',
           '--click', '786,258@17', '--key', '0x42@23', '--key', '0x0D@31',
           '--key', '0x1B@40', '--autopilot', '3000@45']


def verify(log: str, code: int, image: Path, settings: dict[str, str],
           size: int, faction: int, autosaves: list[Path]) -> list[str]:
    failures = assess(code, log, image)
    failures += [f'runtime failure: {m}' for m in BAD if m in log]
    final = turns(log)[1]
    if final is None or final < 1:
        failures.append('No completed game turn')
    try:
        enabled = bool(int(settings.get('Rules', '0'), 2) & 0x40)
    except ValueError:
        enabled = False
    if not enabled:
        failures.append('Culture-linked starts were not enabled')
    if settings.get('ActualWorldSize') != str(size):
        failures.append('Requested map size was not used')
    if faction >= 0 and settings.get('ActualCiv0') != str(faction):
        failures.append('Requested civilization was not used')
    if not autosaves:
        failures.append('No fresh autosaves')
    return failures


def run_case(name: str, source: Path, binary: Path, output: Path) -> dict:
    size, faction = CASES[name]
    out = output / name
    out.mkdir()
    game = out / 'game'
    subprocess.run(['/bin/cp', '-cR', str(source), str(game)], check=True)
    saves = game / 'Conquests/Saves'
    if saves.exists(): saves.rename(out / 'inherited-saves')
    saves.mkdir()
    fresh_settings(game, out)  # Preserve the copied preferences before test settings.
    ini = game / 'Conquests/conquests.ini'
    ini.write_text(f'[Conquests]\nPlayIntro=0\nKeepRes=0\nVoice Quality=0\n'
                   f'Rules={0x481df:032b}\nWorldSize={size}\nFaction0={faction}\n')
    image, log_path = out / 'final.ppm', out / 'run.log'
    command = [str(binary), '--headless', '--run', '--game', str(game), '--work', str(ROOT/'work'),
               '--exe', str(ROOT/'work/Civ3Conquests.exe'), '--watchdog', '120', '--play', '70',
               '--shot', str(image)] + ACTIONS
    print(f'Running culture-linked {name}; log: {log_path}', flush=True)
    with log_path.open('w') as stream:
        try:
            code = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                  timeout=200).returncode
        except subprocess.TimeoutExpired:
            code = -999
    log = log_path.read_text(errors='replace')
    settings = dict(line.split('=', 1) for line in ini.read_text(encoding='latin-1').splitlines() if '=' in line)
    failures = verify(log, code, image, settings, size, faction, list((saves/'Auto').glob('*.SAV')))
    record = {'success': not failures, 'returncode': code, 'final_turn': turns(log)[1],
              'failures': failures, 'settings': {k: v for k, v in settings.items()
                  if k in ('Rules', 'ActualWorldSize', 'WorldSeed') or k.startswith('ActualCiv')},
              'binary_sha256': digest(binary)}
    (out/'result.json').write_text(json.dumps(record, indent=2)+'\n')
    print(f"{name}: {'PASS' if record['success'] else 'FAIL'}, turn {record['final_turn']}", flush=True)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cases', nargs='*', choices=list(CASES))
    parser.add_argument('--game-dir', type=Path, default=ROOT/'game')
    parser.add_argument('--binary', type=Path, default=ROOT/'build-macos/civ3')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--keep-test-games', action='store_true',
                        help='Keep disposable game copies and saves for debugging (off by default)')
    args = parser.parse_args()
    source, binary = (p.expanduser().resolve() for p in (args.game_dir, args.binary))
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    output = (args.output or ROOT/'work'/f'newgame-{stamp}').expanduser().resolve()
    if not binary.is_file() or not source.is_dir(): parser.error('Built executable and game directory required')
    if output.exists() or output.is_relative_to(source): parser.error('Output must be new and outside the source game')
    try: validate_assets(source)
    except (OSError, RuntimeError) as error: parser.error(str(error))
    output.mkdir(parents=True)
    original_ini = source/'Conquests/conquests.ini'
    before = digest(original_ini) if original_ini.is_file() else None
    report = {'success': False, 'cases': {}, 'source_preferences_unchanged': False}
    try:
        for name in args.cases or CASES:
            report['cases'][name] = run_case(name, source, binary, output)
        after = digest(original_ini) if original_ini.is_file() else None
        report['source_preferences_unchanged'] = before == after
        report['success'] = all(r['success'] for r in report['cases'].values()) and before == after
        return 0 if report['success'] else 1
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        report['error'] = str(error)
        print(f'FAIL: {error}', file=sys.stderr)
        return 1
    finally:
        if not args.keep_test_games:
            for name in args.cases or CASES:
                case_output = output / name
                if case_output.exists():
                    try:
                        removed = remove_clones(case_output, ("game", "inherited-saves"))
                        report["cases"].setdefault(name, {})["temporary_copies_removed"] = removed
                    except (OSError, ValueError) as error:
                        report.setdefault("cleanup_warnings", []).append(f"{name}: {error}")
                        print(f"WARNING: QA copy cleanup for {name}: {error}", file=sys.stderr)
        (output/'result.json').write_text(json.dumps(report, indent=2)+'\n')
        print(f'Evidence retained: {output}', flush=True)


if __name__ == '__main__': raise SystemExit(main())
