#!/usr/bin/env python3
"""Build and launch without terminating an existing or unsaved game."""
from __future__ import annotations
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys

import macos_preflight

ROOT = Path(__file__).resolve().parents[1]
MODES = ('run', '--debug', '--verify', '--logs', '--telemetry')


@contextmanager
def exclusive_lock(path: Path):
    """Persistent lock inode, process-scoped ownership; never trust stale PID text."""
    fd = os.open(path, os.O_CREAT | os.O_RDWR | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise RuntimeError(f'Refusing unsafe launch lock: {path}')
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError('A launch/build session is already active. Save and close it before relaunching; no process was stopped.') from error
        yield
    finally:
        os.close(fd)


def game_directory(arguments: list[str], root: Path) -> Path:
    parser = argparse.ArgumentParser(add_help=False, allow_abbrev=False)
    parser.add_argument('--game', type=Path, default=root / 'game')
    args, _ = parser.parse_known_args(arguments)
    game = args.game.expanduser().resolve()
    return game if game.name.casefold() == 'conquests' else game / 'Conquests'


def check_game_idle(directory: Path) -> None:
    path = directory / '.civ3-native.lock'
    try:
        fd = os.open(path, os.O_RDWR | os.O_NOFOLLOW | os.O_CLOEXEC)
    except FileNotFoundError:
        return
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as error:
        raise RuntimeError('This game installation is already running. Save and close it first; no process was stopped.') from error
    finally:
        os.close(fd)


def child_result(command: list[str], *, root: Path, environment: dict[str, str], log: Path | None = None) -> int:
    """Stream an optional log, preserve exit status, and clean up only our child."""
    process = None
    stream = log.open('xb') if log else None
    try:
        process = subprocess.Popen(command, cwd=root, env=environment,
                                   stdout=subprocess.PIPE if stream else None,
                                   stderr=subprocess.STDOUT if stream else None)
        if stream:
            print(f'Game log: {log}', flush=True)
            display = True
            for line in iter(process.stdout.readline, b''):
                stream.write(line); stream.flush()
                if display:
                    try:
                        sys.stdout.buffer.write(line); sys.stdout.buffer.flush()
                    except (BrokenPipeError, OSError):
                        display = False
        code = process.wait()
        return code if code >= 0 else 128 - code
    except KeyboardInterrupt:
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
        return 130
    except BaseException:
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
        raise
    finally:
        if stream:
            stream.close()
        if process and process.stdout:
            process.stdout.close()


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    if arguments and arguments[0] in ('--help', '-h'):
        print('Usage: ./script/build_and_run.sh [run|--logs|--debug|--verify] [game options]\n'
              'Builds and launches the native Mac game. Existing sessions are never terminated.\n'
              '--logs records output in a new timestamped work/macos-game-*.log.\n'
              '--verify runs the menu smoke test (accepts --timeout SECONDS).\n'
              'Recording: --record FILE [--fps 1..240] [--frames N] [--overwrite-recording]\n'
              'Menu CPU pacing: --menu-idle-ms 0..16 (default 2; 0 disables; gameplay is not delayed)')
        return 0
    mode = arguments.pop(0) if arguments and arguments[0] in MODES else 'run'
    root = ROOT.resolve()
    work = root / 'work'
    work.mkdir(exist_ok=True)
    environment = dict(os.environ)
    build = Path(environment.get('BUILD_DIR', str(root / 'build-macos'))).expanduser()
    if not build.is_absolute():
        build = root / build
    environment['BUILD_DIR'] = str(build.resolve())
    binary = build.resolve() / 'civ3'
    try:
        with exclusive_lock(work / 'macos-launch.lock'):
            check_game_idle(game_directory(arguments, root))
            if macos_preflight.main(arguments):
                return 1
            code = child_result([str(root / 'build-macos.sh')], root=root, environment=environment)
            if code:
                return code
            if mode == '--verify':
                command = [sys.executable, str(root / 'tools/macos_smoke.py'), '--binary', str(binary)] + arguments
            elif mode == '--debug':
                command = ['lldb', '--', str(binary), '--run'] + arguments
            else:
                command = [str(binary), '--run'] + arguments
            log = None
            if mode in ('--logs', '--telemetry'):
                stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
                log = work / f'macos-game-{stamp}.log'
            return child_result(command, root=root, environment=environment, log=log)
    except (OSError, RuntimeError) as error:
        print(f'Cannot launch: {error}', file=sys.stderr)
        return 75


if __name__ == '__main__':
    def interrupted(_signum, _frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
