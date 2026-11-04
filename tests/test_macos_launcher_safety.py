"""Launcher lifecycle tests use disposable files/processes, never the actual game."""
from contextlib import redirect_stderr, redirect_stdout
import fcntl
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import macos_launch as launch


class LauncherSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='civ3 launch safety ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.game = self.root / 'game/Conquests'
        self.game.mkdir(parents=True)

    def test_competing_launch_is_rejected_and_owner_remains_held(self):
        path = self.root / 'launch.lock'
        with launch.exclusive_lock(path):
            with self.assertRaisesRegex(RuntimeError, 'no process was stopped'):
                with launch.exclusive_lock(path):
                    self.fail('A duplicate acquired the lock')
            with self.assertRaises(RuntimeError):
                with launch.exclusive_lock(path):
                    self.fail('Failed launch released another owner')
        with launch.exclusive_lock(path):
            pass

    def test_stale_pid_text_is_not_a_signal_target(self):
        path = self.root / 'launch.lock'
        path.write_text(str(os.getpid()))
        with launch.exclusive_lock(path):
            self.assertEqual(path.read_text(), str(os.getpid()))

    def test_lock_released_on_exception(self):
        path = self.root / 'launch.lock'
        with self.assertRaises(ValueError):
            with launch.exclusive_lock(path):
                raise ValueError('test')
        with launch.exclusive_lock(path):
            pass

    def test_lock_symlink_refused_without_modifying_target(self):
        target = self.root / 'keep'
        target.write_text('unchanged')
        lock = self.root / 'launch.lock'
        lock.symlink_to(target)
        with self.assertRaises(OSError):
            with launch.exclusive_lock(lock):
                pass
        self.assertEqual(target.read_text(), 'unchanged')

    def test_game_lock_blocks_without_truncation(self):
        path = self.game / '.civ3-native.lock'
        path.write_text('existing session')
        with path.open('r+') as f:
            fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaisesRegex(RuntimeError, 'already running'):
                launch.check_game_idle(self.game)
        self.assertEqual(path.read_text(), 'existing session')
        launch.check_game_idle(self.game)

    def test_missing_game_lock_is_not_created_by_preflight(self):
        launch.check_game_idle(self.game)
        self.assertFalse((self.game / '.civ3-native.lock').exists())

    def test_child_exit_code_is_preserved(self):
        code = launch.child_result([sys.executable, '-c', 'raise SystemExit(7)'], root=self.root, environment=dict(os.environ))
        self.assertEqual(code, 7)

    def test_help_does_not_build_or_inspect_installation(self):
        with patch.object(launch, 'child_result') as run, patch.object(launch.macos_preflight, 'main') as preflight:
            with redirect_stdout(io.StringIO()):
                self.assertEqual(launch.main(['--help']), 0)
            run.assert_not_called(); preflight.assert_not_called()

    def test_explicit_game_and_conquests_paths_resolve_identically(self):
        self.assertEqual(launch.game_directory(['--game', str(self.game.parent)], self.root), self.game)
        self.assertEqual(launch.game_directory(['--game', str(self.game)], self.root), self.game)

    def test_same_lock_survives_closed_process(self):
        path = self.root / 'launch.lock'
        code = 'import fcntl,sys;f=open(sys.argv[1],"w");fcntl.flock(f,fcntl.LOCK_EX);print("locked",flush=True);input()'
        p = subprocess.Popen([sys.executable, '-c', code, str(path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(p.stdout.readline().strip(), 'locked')
            with self.assertRaises(RuntimeError):
                with launch.exclusive_lock(path):
                    self.fail('Cross-process lock bypass')
            p.stdin.write('\n'); p.stdin.flush(); p.wait(timeout=5)
            with launch.exclusive_lock(path):
                pass
        finally:
            if p.poll() is None:
                p.kill(); p.wait()
            p.stdin.close(); p.stdout.close()

    def test_log_paths_are_not_overwritten(self):
        path = self.root / 'previous.log'
        path.write_text('preserve')
        with self.assertRaises(FileExistsError):
            launch.child_result([sys.executable, '-c', 'print("new")'], root=self.root, environment=dict(os.environ), log=path)
        self.assertEqual(path.read_text(), 'preserve')


if __name__ == '__main__':
    unittest.main()
