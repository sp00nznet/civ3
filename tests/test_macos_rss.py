"""Memory-sampling safety tests with synthetic child processes only."""
from pathlib import Path
import subprocess
import os
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from macos_rss import read_rss_kib, run_with_rss, summarize_rss


class RssMonitorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="civ3-rss-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_positive_process_memory_samples(self):
        script = "import time; data=bytearray(2*1024*1024); time.sleep(.35)"
        code, samples = run_with_rss([sys.executable, "-c", script], self.root,
                                     self.root / "child.log", timeout=4,
                                     sample_interval=.05)
        self.assertEqual(code, 0)
        self.assertGreaterEqual(len(samples), 2, samples)
        self.assertGreater(max(int(s["rss_kib"]) for s in samples), 0)
        self.assertEqual(summarize_rss(samples)["sample_count"], len(samples))

    def test_passes_explicit_child_environment_for_scenario_tracing(self):
        env = dict(os.environ, CIV3_SYNTHETIC_TRACE_FLAG="expected")
        code, samples = run_with_rss(
            [sys.executable, "-c",
             "import os; print(os.environ['CIV3_SYNTHETIC_TRACE_FLAG'])"],
            self.root, self.root / "env.log", timeout=4,
            sample_interval=.04, env=env)
        self.assertEqual(code, 0)
        self.assertEqual((self.root / "env.log").read_text().strip(), "expected")
        self.assertIsInstance(samples, list)

    def test_timeout_stops_only_its_own_child(self):
        script = "import time; time.sleep(20)"
        with self.assertRaises(subprocess.TimeoutExpired):
            run_with_rss([sys.executable, "-c", script], self.root,
                         self.root / "timeout.log", timeout=.15,
                         sample_interval=.04)
        self.assertTrue((self.root / "timeout.log").is_file())

    def test_insufficient_samples_is_reported_not_guessed(self):
        report = summarize_rss([{"seconds": 0, "rss_kib": 100}])
        self.assertIn("warning", report)
        self.assertEqual(report["sample_count"], 1)

    def test_rss_is_not_reported_as_proof_of_a_leak(self):
        report = summarize_rss([{"seconds": 0, "rss_kib": 1200},
                                {"seconds": 35, "rss_kib": 7000},
                                {"seconds": 85, "rss_kib": 8000}])
        self.assertEqual(report["growth_from_startup_kib"], 1000)
        self.assertIn("not a leak", report["note"])

    def test_bad_sampling_bounds_rejected(self):
        for seconds, interval in ((0, 1), (1, 0), (-1, 1)):
            with self.subTest(seconds=seconds, interval=interval):
                with self.assertRaises(ValueError):
                    run_with_rss(["true"], self.root,
                                 self.root / "no.log", timeout=seconds,
                                 sample_interval=interval)


if __name__ == "__main__":
    unittest.main()
