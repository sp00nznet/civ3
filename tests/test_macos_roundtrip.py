"""Integration-test verdicts must reject a live process displaying an error dialog."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import macos_roundtrip as test


class RoundtripVerdicts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='civ3-roundtrip-unit-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.image = self.root / 'frame.ppm'
        self.image.write_bytes(b'P6\n32 32\n255\n' + bytes(range(256)) * 12)
        self.log = ('[game] main menu open\n[input] 11.0s waited 2.5s for [0xA74EA4] = 3\n'
                    '[game] turn 7\n')

    def test_restored_turn_and_progress_are_required(self):
        self.assertEqual(test.verdict(self.log, 4, self.image, 3, 3), [])
        self.assertTrue(test.verdict(self.log, 4, self.image, 4, 3))
        self.assertTrue(test.verdict(self.log, 4, self.image, 3, 5))

    def test_error_dialog_is_not_success(self):
        for marker in test.BAD:
            with self.subTest(marker=marker):
                self.assertTrue(test.verdict(self.log + marker, 4, self.image, 3, 3))

    def test_no_load_milestone_fails(self):
        self.assertTrue(test.verdict('[game] main menu open\n[game] turn 7', 4, self.image))

    def test_crash_exit_fails_even_with_old_milestones(self):
        self.assertTrue(test.verdict(self.log, 3, self.image, 3, 3))

    def test_snapshot_is_required(self):
        self.image.unlink()
        self.assertTrue(test.verdict(self.log, 4, self.image, 3, 3))

    def test_select_save_preserves_other_preferences_and_removes_old_names(self):
        game = self.root / 'game'; (game / 'Conquests').mkdir(parents=True)
        ini = game / 'Conquests/conquests.ini'
        ini.write_text('[Conquests]\nRules=1010\nLatest Save=old\nLatest Scenario=old\n'
                       '[Conquests]\nLatest Save=duplicate\n')
        test.select_save(game, 'Caesar, 3850 BC.SAV')
        content = ini.read_text()
        self.assertEqual(content.count('Latest Save='), 1)
        self.assertIn('Latest Save=C:\\Conquests\\Saves\\Caesar, 3850 BC', content)
        self.assertIn('Rules=1010', content)
        self.assertNotIn('Latest Scenario', content)

    def test_select_save_rejects_paths(self):
        for name in ('../outside.SAV', '/outside.SAV', 'no-extension', 'nested\\file.SAV', 'bad\nname.SAV'):
            with self.assertRaises(ValueError):
                test.select_save(self.root, name)

    def test_private_copy_refuses_existing_or_nested_destinations(self):
        source = self.root / 'source'; source.mkdir()
        for dest in (source, source / 'nested'):
            with self.assertRaises(RuntimeError):
                test.private_game(source, dest, self.root / 'seed', 'file.SAV')


if __name__ == '__main__':
    unittest.main()
