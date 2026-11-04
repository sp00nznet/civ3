from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from macos_performance import summary

class PerformanceVerdicts(unittest.TestCase):
    def row(self, variant, cpu, image='same'):
        return dict(variant=variant,cpu_seconds=cpu,elapsed_seconds=12.0,peak_rss_bytes=123,framebuffer_sha256=image,failures=[])
    def test_median_cpu_not_wall_time_drives_result(self):
        rows=[self.row('baseline',10),self.row('baseline',12),self.row('candidate',1),self.row('candidate',1.2)]
        result=summary(rows)
        self.assertAlmostEqual(result['cpu_reduction_percent'],90)
        self.assertEqual(result['baseline']['median_cpu_seconds'],11)
    def test_slowdown_remains_negative(self):
        self.assertLess(summary([self.row('baseline',1),self.row('candidate',2)])['cpu_reduction_percent'],0)
    def test_different_pixels_reject_speedup(self):
        with self.assertRaises(ValueError):summary([self.row('baseline',10),self.row('candidate',1,'wrong')])
    def test_runtime_errors_reject_speedup(self):
        a=self.row('baseline',10);b=self.row('candidate',1);b['failures']=['crash']
        with self.assertRaises(ValueError):summary([a,b])
    def test_missing_invalid_or_unequal_runs_rejected(self):
        for rows in ([],[self.row('baseline',1)],[self.row('baseline',1),self.row('candidate',0)],
                     [self.row('baseline',1),self.row('baseline',2),self.row('candidate',1)],
                     [self.row('baseline',1,None),self.row('candidate',1,None)]):
            with self.subTest(rows=rows),self.assertRaises(ValueError):summary(rows)
if __name__=='__main__':unittest.main()
