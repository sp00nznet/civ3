from contextlib import redirect_stdout, redirect_stderr
import io
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import macos_smoke as smoke

class SmokeCLITests(unittest.TestCase):
    def test_selected_installation_and_runtime_are_forwarded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve()
            selected=root/'other game'
            work=root/'different runtime'
            executable=root/'owned.exe'
            seen=[]
            def run(command, **kwargs):
                seen.extend(command)
                kwargs['stdout'].write('[game] main menu open\n')
                image=Path(command[command.index('--shot')+1])
                image.write_bytes(b'P6\n32 32\n255\n'+bytes(range(256))*12)
                return SimpleNamespace(returncode=4)
            with patch.object(smoke, 'ROOT', root), patch.object(smoke.subprocess, 'run', side_effect=run), redirect_stdout(io.StringIO()):
                self.assertEqual(smoke.main(['--game',str(selected),'--work',str(work),'--exe',str(executable)]),0)
            for flag,value in [('--game',selected),('--work',work),('--exe',executable)]:
                self.assertEqual(seen[seen.index(flag)+1],str(value))

    def test_invalid_timeouts_fail_before_process_creation(self):
        for value in ('0','-1','9','86401'):
            with self.subTest(value=value), patch.object(smoke.subprocess,'run') as run, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):smoke.main(['--timeout',value])
                run.assert_not_called()
