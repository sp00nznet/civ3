"""Synthetic QA copies: never remove a real game or valuable evidence."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from macos_qa_cleanup import remove_clones


class QACleanupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="civ3-qa-clean-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / "new-qa-output"
        self.output.mkdir()
        self.real_game = self.root / "valuable-game"
        (self.real_game / "Conquests/Saves").mkdir(parents=True)
        (self.real_game / "Conquests/Saves/real.SAV").write_bytes(b"preserve")

    def test_only_explicit_disposable_copies_are_removed(self):
        for folder in ("save-game", "reload-game", "save-game-inherited-saves"):
            (self.output / folder).mkdir()
            (self.output / folder / "synthetic.SAV").write_bytes(b"test")
        (self.output / "result.json").write_text('{"success": true}')
        (self.output / "reload.ppm").write_bytes(b"P6\n1 1\n255\n\x01\x02\x03")
        removed = remove_clones(self.output, ("save-game", "reload-game",
                                              "save-game-inherited-saves",
                                              "reload-game-inherited-saves"))
        self.assertEqual(removed, ["save-game", "reload-game", "save-game-inherited-saves"])
        self.assertEqual((self.output / "result.json").read_text(), '{"success": true}')
        self.assertTrue((self.output / "reload.ppm").is_file())
        self.assertEqual((self.real_game / "Conquests/Saves/real.SAV").read_bytes(), b"preserve")

    def test_symlink_to_original_game_is_never_followed_or_removed(self):
        (self.output / "save-game").symlink_to(self.real_game, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlinked"):
            remove_clones(self.output, ("save-game",))
        self.assertTrue((self.output / "save-game").is_symlink())
        self.assertTrue((self.real_game / "Conquests/Saves/real.SAV").is_file())

    def test_refuses_path_traversal_and_nonfolders(self):
        (self.output / "save-game").write_text("don't delete")
        for name in ("../valuable-game", "/tmp", ".", "..", "nested/folder",
                     "nested\\folder", "save-game"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                remove_clones(self.output, (name,))
        self.assertEqual((self.output / "save-game").read_text(), "don't delete")

    def test_refuses_a_symlink_output_directory(self):
        alias = self.root / "alias"
        alias.symlink_to(self.real_game, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "not a symlink"):
            remove_clones(alias, ("Conquests",))
        self.assertTrue((self.real_game / "Conquests/Saves/real.SAV").is_file())

    def test_all_qa_entrypoints_expose_opt_in_to_keep_copies(self):
        import subprocess
        for tool, required in (("macos_roundtrip.py", "--seed-save"),
                               ("macos_newgame_test.py", "persian-small"),
                               ("macos_performance.py", "--baseline")):
            with self.subTest(tool=tool):
                executable = Path(__file__).resolve().parents[1] / "tools" / tool
                result = subprocess.run([sys.executable, str(executable), "--help"],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("--keep-test-games", result.stdout)
                self.assertIn(required, result.stdout)

    def test_idempotent_for_absent_clones(self):
        self.assertEqual(remove_clones(self.output, ("save-game", "reload-game")), [])


if __name__ == "__main__":
    unittest.main()
