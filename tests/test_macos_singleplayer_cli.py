"""Asset-free checks for the single-player compatibility test entrypoints."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = {
    "macos_campaign_soak.py": ("--seed-save", "--output", "--binary", "--interval", "--rss-sample"),
    "macos_scenario_soak.py": ("--output", "--interval", "--rss-sample", "mesopotamia", "wwii-pacific"),
    "macos_mod_soak.py": ("--output", "--interval", "no-civ-traits", "age-of-bronze"),
}


class SingleplayerEntrypointTests(unittest.TestCase):
    def test_help_works_without_commercial_game_files(self):
        for filename, fields in SCRIPTS.items():
            with self.subTest(script=filename):
                script = ROOT / "tools" / filename
                result = subprocess.run([sys.executable, str(script), "--help"],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                for field in fields:
                    self.assertIn(field, result.stdout)

    def test_invalid_case_is_rejected_before_creating_a_copy(self):
        for filename in ("macos_scenario_soak.py", "macos_mod_soak.py"):
            with self.subTest(script=filename):
                script = ROOT / "tools" / filename
                result = subprocess.run([sys.executable, str(script), "not-a-real-case"],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2, result.stdout)
                self.assertIn("invalid choice", result.stderr)

    def test_missing_seed_does_not_start_a_long_campaign(self):
        script = ROOT / "tools" / "macos_campaign_soak.py"
        result = subprocess.run([sys.executable, str(script), "--output", "ignored"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--seed-save", result.stderr)


if __name__ == "__main__":
    unittest.main()
