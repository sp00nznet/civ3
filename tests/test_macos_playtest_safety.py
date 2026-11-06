"""Keep writable runtime locks out of hard-linked QA farms."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import playtest

class PlaytestSafety(unittest.TestCase):
    def test_game_lock_never_becomes_a_shared_hard_link(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            source=root/'game/Conquests';source.mkdir(parents=True)
            lock=source/'.civ3-native.lock';lock.write_text('stale PID')
            (source/'conquests.ini').write_text('[Conquests]\nPlayIntro=0\n')
            (source/'fixture.dat').write_bytes(b'synthetic')
            destination=root/'private'
            with patch.object(playtest,'ROOT',str(root)):
                playtest.farm(str(destination))
            self.assertFalse((destination/'Conquests/.civ3-native.lock').exists())
            self.assertEqual(lock.stat().st_nlink,1)
            self.assertEqual((destination/'Conquests/fixture.dat').read_bytes(),b'synthetic')

    def test_disposable_test_game_is_cleaned_but_evidence_remains(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out = root / 'evidence'
            original = root / 'original'
            original.mkdir()
            (original / 'save.SAV').write_text('keep')
            def farm(destination):
                path = Path(destination) / 'Conquests'
                (path / 'Saves/Auto').mkdir(parents=True)
                (path / 'Saves/Auto/temporary.SAV').write_text('test')
                return str(path)
            with patch.object(playtest, 'OUT', str(out)), \
                 patch.object(playtest, 'farm', side_effect=farm), \
                 patch.object(playtest.subprocess, 'run',
                              return_value=SimpleNamespace(returncode=4)):
                playtest.run('synthetic', [], 1, 1)
            self.assertFalse((out / 'synthetic/game').exists())
            self.assertTrue((out / 'synthetic/run.log').is_file())
            self.assertEqual((original / 'save.SAV').read_text(), 'keep')

    def test_keep_test_games_is_explicit_opt_in(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            def farm(destination):
                path = Path(destination) / 'Conquests'
                (path / 'Saves/Auto').mkdir(parents=True)
                return str(path)
            with patch.object(playtest, 'OUT', str(root)), \
                 patch.object(playtest, 'farm', side_effect=farm), \
                 patch.object(playtest.subprocess, 'run',
                              return_value=SimpleNamespace(returncode=4)):
                playtest.run('synthetic-keep', [], 1, 1, keep_test_games=True)
            self.assertTrue((root / 'synthetic-keep/game/Conquests').is_dir())

    def test_only_posix_harness_requests_recording_overwrite(self):
        for platform in ('darwin','linux','win32'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as tmp:
                with patch.object(playtest,'OUT',tmp), patch.object(playtest,'farm',return_value=tmp), \
                     patch.object(playtest.sys,'platform',platform), \
                     patch.object(playtest.subprocess,'run',return_value=SimpleNamespace(returncode=0)) as run:
                    playtest.run('menu-exit',[],1,1)
                    command=run.call_args.args[0]
                self.assertEqual('--overwrite-recording' in command,platform!='win32')

if __name__=='__main__':unittest.main()
