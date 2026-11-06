"""Synthetic source shapes validate fail-closed, non-destructive build overlays."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import macos_culture_overlay as overlay


def chunk(address, count):
    return '#include "recomp_types.h"\n' + '\n'.join(
        f'void synthetic_{address}_{i}(void) {{\n    ecx = MEM32(eax); /* 0x{address}: mov ecx, dword ptr [eax] */\n}}'
        for i in range(count)) + '\n'


class CultureOverlayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='civ3 culture test ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'src/recomp/gen'
        self.source.mkdir(parents=True)
        self.output = self.root / 'build/overlay'
        self.exe = self.root / 'synthetic.exe'
        self.exe.write_bytes(b'synthetic fixture; not a real executable')
        self.sha = hashlib.sha1(self.exe.read_bytes()).hexdigest()
        p = patch.object(overlay, 'SUPPORTED_SHA1', self.sha)
        p.start(); self.addCleanup(p.stop)
        for index,(address, (_,_,count)) in enumerate(overlay.SITES.items()):
            (self.source / f'recomp_{index:04}.c').write_text(chunk(address,count))

    def test_transforms_all_aliases(self):
        text, counts = overlay.transform(chunk('005AEA25',5))
        self.assertEqual(counts['005AEA25'],5)
        self.assertEqual(text.count('civ3_generation_race(esp, ebp, ecx, 0)'),5)
        self.assertEqual(text.count('goto L_005AEAA1'),5)
        self.assertEqual(text.count('#include "culture_start_fix.h"'),1)

    def test_scenario_branch_selects_its_own_pending_source(self):
        text,_ = overlay.transform(chunk('005AE71C',4))
        self.assertEqual(text.count('civ3_generation_race(esp, ebp, ecx, 1)'),4)
        self.assertEqual(text.count('goto L_005AE798'),4)

    def test_other_code_unchanged(self):
        text = '#include "recomp_types.h"\nvoid unrelated(void) {}\n'
        result, counts = overlay.transform(text)
        self.assertEqual(result,text); self.assertFalse(any(counts.values()))

    def test_changed_instruction_refused(self):
        with self.assertRaises(ValueError):
            overlay.transform(chunk('005AEA25',1).replace('ecx = MEM32(eax)','ecx = MEM32(edx)'))

    def test_double_application_refused(self):
        modified,_ = overlay.transform(chunk('005AEA25',1))
        with self.assertRaises(ValueError): overlay.transform(modified)

    def test_stage_preserves_inputs_and_noop_timestamps(self):
        before = {p:p.read_bytes() for p in self.source.iterdir()}
        manifest = overlay.stage(self.source,self.output,self.exe)
        stamps = {p:p.stat().st_mtime_ns for p in self.output.iterdir()}
        self.assertEqual(overlay.stage(self.source,self.output,self.exe),manifest)
        self.assertEqual(stamps,{p:p.stat().st_mtime_ns for p in self.output.iterdir()})
        self.assertEqual(before,{p:p.read_bytes() for p in self.source.iterdir()})
        self.assertEqual(manifest['sites'],{'005AE71C':4,'005AEA25':5})
        self.assertIn('INCLUDE_DIRECTORIES',(self.output/'sources.cmake').read_text())

    def test_incomplete_lift_does_not_write_partial_overlay(self):
        (self.source/'recomp_0001.c').write_text(chunk('005AEA25',4))
        with self.assertRaises(ValueError): overlay.stage(self.source,self.output,self.exe)
        self.assertFalse(self.output.exists())

    def test_changed_input_invalidates_cache(self):
        overlay.stage(self.source,self.output,self.exe)
        original = self.source/'recomp_0001.c'
        original.write_text(chunk('005AEA25',3))
        with self.assertRaises(ValueError): overlay.stage(self.source,self.output,self.exe)

    def test_tampered_overlay_is_rebuilt_from_original(self):
        overlay.stage(self.source,self.output,self.exe)
        generated = self.output/'recomp_0001.c'
        wanted = generated.read_text()
        generated.write_text('broken output')
        overlay.stage(self.source,self.output,self.exe)
        self.assertEqual(generated.read_text(),wanted)

    def test_missing_manifest_regenerates(self):
        expected = overlay.stage(self.source,self.output,self.exe)
        (self.output/'manifest.json').unlink()
        self.assertEqual(overlay.stage(self.source,self.output,self.exe),expected)

    def test_wrong_executable_refused(self):
        self.exe.write_bytes(b'unsupported')
        with self.assertRaises(ValueError): overlay.stage(self.source,self.output,self.exe)
        self.assertFalse(self.output.exists())

    def test_output_cannot_overwrite_input(self):
        with self.assertRaises(ValueError): overlay.stage(self.source,self.source,self.exe)
        with self.assertRaises(ValueError): overlay.stage(self.source,self.source/'nested',self.exe)


if __name__ == '__main__': unittest.main()
