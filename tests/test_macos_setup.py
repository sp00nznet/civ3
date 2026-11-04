"""Setup tests for path handling, version rejection and the Finder launcher."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("setup_macos", Path(__file__).resolve().parents[1] / "tools/setup_macos.py")
setup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(setup)


class MacSetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="civ3 setup ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "Sid Meier's Civilization III Complete"
        (self.game / "Conquests").mkdir(parents=True)
        for name in ("Civ3Conquests.exe", "jgl.dll", "sound.dll"):
            (self.game / "Conquests" / name).write_bytes(b"synthetic test only")

    def test_accepts_install_or_conquests_folder(self):
        self.assertEqual(setup.find_game(str(self.game)), self.game)
        self.assertEqual(setup.find_game(str(self.game / "Conquests")), self.game)

    def test_missing_dll_is_rejected_before_build(self):
        (self.game / "Conquests/sound.dll").unlink()
        with self.assertRaisesRegex(RuntimeError, "Complete install"):
            setup.find_game(str(self.game))

    def test_unrecognized_executable_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "unsupported Civ3Conquests.exe"):
            setup.validate_game(self.game)

    def test_launcher_handles_quoted_paths_and_moved_checkout(self):
        repo = self.root / "Civ3's $folder; harmless"
        binary = repo / "build-macos/civ3"
        binary.parent.mkdir(parents=True)
        binary.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
        binary.chmod(0o755)
        with patch.object(setup, "REPO", repo):
            setup.write_launcher()
        moved = self.root / "Moved Civ3's $folder"
        repo.rename(moved)
        result = subprocess.run([str(moved / "Play-Civilization-III.command"),
                                 "--headless", "two words"], cwd=self.root,
                                check=True, text=True, stdout=subprocess.PIPE)
        self.assertEqual(result.stdout.splitlines(), ["--run", "--headless", "two words"])


if __name__ == "__main__":
    unittest.main()
