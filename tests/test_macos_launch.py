"""Read-only launch checks with synthetic assets and preferences only."""
from contextlib import redirect_stderr
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import macos_assets as assets
import macos_preflight as preflight


class LaunchChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='civ3 launch ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'game'
        self.work = self.root / 'work'
        self.work.mkdir()
        for paths in assets.REQUIRED_ASSETS.values():
            path = self.game / paths[0]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'synthetic asset')
        for name in ('Civ3Conquests.exe', 'jgl.dll', 'sound.dll'):
            (self.work / name).write_bytes(b'synthetic runtime input')
        self.ini = self.game / 'Conquests/conquests.ini'

    def run_check(self, game=None):
        error = io.StringIO()
        with redirect_stderr(error):
            status = preflight.main(['--game', str(game or self.game), '--work', str(self.work),
                                     '--exe', str(self.work / 'Civ3Conquests.exe'), '--watchdog', '30'])
        return status, error.getvalue()

    def test_safe_setting_is_quiet(self):
        self.ini.write_text(f'[Conquests]\nRules={assets.SAFE_DEFAULT_RULES:032b}\n')
        self.assertEqual(self.run_check(), (0, ''))

    def test_unsafe_setting_warns_without_changing_it(self):
        raw = b'[Conquests]\r\nRules=00000000000001001000000111011111\r\n'
        self.ini.write_bytes(raw)
        status, error = self.run_check()
        self.assertEqual(status, 0)
        self.assertIn('Culturally Linked', error)
        self.assertEqual(self.ini.read_bytes(), raw)

    def test_missing_settings_warn_without_creating_them(self):
        status, error = self.run_check()
        self.assertEqual(status, 0)
        self.assertIn('No explicit Rules', error)
        self.assertFalse(self.ini.exists())

    def test_malformed_rules_warn(self):
        self.ini.write_text('[Conquests]\nRules=not-a-bitset\n')
        self.assertIn('Malformed', self.run_check()[1])

    def test_first_duplicate_section_matches_runtime(self):
        self.ini.write_text(f'[conquests]\nrules={assets.SAFE_DEFAULT_RULES:032b}\n'
                            '[Conquests]\nRules=11111111111111111111111111111111\n')
        self.assertEqual(assets.settings_warnings(self.game), [])

    def test_first_section_missing_rules_does_not_read_later_duplicate(self):
        self.ini.write_text(f'[Conquests]\nKeepRes=0\n[Conquests]\nRules={assets.SAFE_DEFAULT_RULES:032b}\n')
        self.assertIn('No explicit Rules', assets.settings_warnings(self.game)[0])

    def test_missing_asset_stops_before_launch(self):
        (self.game / 'Conquests/conquests.biq').unlink()
        status, error = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn('Incomplete game assets', error)

    def test_missing_runtime_input_stops(self):
        (self.work / 'sound.dll').unlink()
        status, error = self.run_check()
        self.assertEqual(status, 1)
        self.assertIn('Missing runtime input', error)

    def test_conquests_subdirectory_is_accepted(self):
        self.ini.write_text(f'[Conquests]\nRules={assets.SAFE_DEFAULT_RULES:032b}\n')
        self.assertEqual(self.run_check(self.game / 'Conquests'), (0, ''))


if __name__ == '__main__':
    unittest.main()
