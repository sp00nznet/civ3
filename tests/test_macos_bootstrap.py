"""Finder bootstrap regression using only a disposable fake setup program."""
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FinderBootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="civ3 Finder test ")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.repo = self.base / "Civ3's $folder; literal"
        (self.repo / "tools").mkdir(parents=True)
        for name in ("Setup-macos.command", "setup-macos.sh"):
            shutil.copy2(ROOT / name, self.repo / name)
        (self.repo / "tools/setup_macos.py").write_text(
            'import json,os,sys\n'
            'print(json.dumps({"args":sys.argv[1:],"cwd":os.getcwd(),"path":os.environ["PATH"]}))\n'
            'sys.exit(int(os.environ.get("FIXTURE_EXIT", "0")))\n')
        self.env = dict(os.environ, PATH='/usr/bin:/bin', PYTHON=sys.executable)

    def invoke(self, *args, environment=None):
        return subprocess.run([str(self.repo / 'Setup-macos.command'), *args],
                              cwd=self.base, env=environment or self.env,
                              capture_output=True, text=True, timeout=10)

    def test_moved_checkout_and_quoted_arguments(self):
        moved = self.base / "Moved Civ3's folder"
        self.repo.rename(moved)
        self.repo = moved
        result = self.invoke('--game-dir', "A player's $folder; [literal]", '--check')
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = json.loads(result.stdout)
        self.assertEqual(payload['cwd'], str(moved))
        self.assertEqual(payload['args'], ['--game-dir', "A player's $folder; [literal]", '--check'])

    def test_native_homebrew_is_available_with_minimal_finder_path(self):
        result = self.invoke('--check')
        self.assertEqual(result.returncode, 0, result.stderr)
        prefix = '/opt/homebrew/bin:' if platform.machine() == 'arm64' else '/usr/local/bin:'
        self.assertTrue(json.loads(result.stdout)['path'].startswith(prefix))

    def test_setup_failure_status_is_preserved(self):
        result = self.invoke(environment=dict(self.env, FIXTURE_EXIT='23'))
        self.assertEqual(result.returncode, 23)

    def test_missing_python_has_actionable_error(self):
        result = self.invoke(environment=dict(self.env, PYTHON='__civ3_missing_python__'))
        self.assertEqual(result.returncode, 1)
        self.assertIn('Python 3.10+', result.stderr)


if __name__ == '__main__':
    unittest.main()
