"""New-game verdicts require gameplay and the requested settings, not just a window."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from macos_newgame_test import verify


class NewGameVerdicts(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.image=Path(self.temp.name)/'frame.ppm'
        self.image.write_bytes(b'P6\n32 32\n255\n'+bytes(range(256))*12)
        self.log='[game] main menu open\n[game] turn 4\n'
        self.settings={'Rules':f'{0x481df:032b}','ActualWorldSize':'1','ActualCiv0':'12'}
        self.autos=[Path('fresh-fixture.SAV')]

    def check_result(self,settings=None,log=None,autos=None):
        return verify(self.log if log is None else log,4,self.image,
                      self.settings if settings is None else settings,1,12,
                      self.autos if autos is None else autos)

    def test_positive_turn_matching_map_and_civ_pass(self):
        self.assertEqual(self.check_result(),[])

    def test_wrong_map_is_not_coverage(self):
        self.assertTrue(self.check_result({**self.settings,'ActualWorldSize':'0'}))

    def test_wrong_human_is_not_coverage(self):
        self.assertTrue(self.check_result({**self.settings,'ActualCiv0':'1'}))

    def test_rules_turns_and_fresh_saves_are_required(self):
        self.assertTrue(self.check_result({**self.settings,'Rules':f'{0x4819f:032b}'}))
        self.assertTrue(self.check_result({**self.settings,'Rules':'malformed'}))
        self.assertTrue(self.check_result(log='[game] main menu open\n[game] turn 0\n'))
        self.assertTrue(self.check_result(autos=[]))


if __name__=='__main__': unittest.main()
