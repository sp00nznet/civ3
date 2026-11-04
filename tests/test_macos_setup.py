"""Synthetic fixtures only; no game binaries, generated source or assets required."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import setup_macos as setup
import macos_smoke as smoke
import macos_assets as assets


class MacOSSetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="civ3 test ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "Game with spaces"
        (self.game / "Conquests").mkdir(parents=True)
        self.fixture = b"synthetic executable fixture, not a real PE"
        self.sha = hashlib.sha1(self.fixture).hexdigest()
        self.patcher = patch.object(setup, "SUPPORTED_SHA1", self.sha)
        self.patcher.start()
        self.addCleanup(self.patcher.stop)

    def exe(self, name="Civ3Conquests.exe", content=None):
        path = self.game / "Conquests" / name
        path.write_bytes(self.fixture if content is None else content)
        return path

    def test_accepts_supported_executable(self):
        expected = self.exe()
        self.assertEqual(setup.supported_executable(self.game), expected)

    def test_uses_original_backup_when_current_image_is_patched(self):
        self.exe(content=b"patched image")
        expected = self.exe("Civ3Conquests.exe.orig")
        self.assertEqual(setup.supported_executable(self.game), expected)
        self.assertEqual((self.game / "Conquests/Civ3Conquests.exe").read_bytes(), b"patched image")

    def test_rejects_wrong_game_build(self):
        self.exe(content=b"wrong build")
        with self.assertRaises(RuntimeError):
            setup.supported_executable(self.game)

    def test_rejects_missing_executable(self):
        with self.assertRaises(RuntimeError):
            setup.supported_executable(self.game)

    def test_normalizes_conquests_directory(self):
        self.assertEqual(setup.game_root(self.game / "Conquests"), self.game.resolve())

    def test_preserves_install_root_with_spaces(self):
        self.assertEqual(setup.game_root(self.game), self.game.resolve())

    def test_finds_explicit_install_with_required_dlls(self):
        self.exe()
        for name in ("jgl", "sound"):
            (self.game / "Conquests" / f"{name}.dll").write_bytes(b"synthetic DLL")
        self.assertEqual(setup.find_game(str(self.game / "Conquests")), self.game.resolve())

    def test_rejects_missing_dlls(self):
        self.exe()
        with self.assertRaises(RuntimeError):
            setup.find_game(str(self.game))

    def test_lift_stats_must_be_complete(self):
        path = self.root / "stats.json"
        good = {"lifted": 5, "stubs": 0, "errors": 0, "no_terminator": 2}
        path.write_text(json.dumps(good))
        self.assertEqual(setup.validate_stats(path), good)
        for bad in ({**good, "errors": 1}, {**good, "stubs": 1}, {**good, "lifted": 0}, {}):
            with self.subTest(bad=bad):
                path.write_text(json.dumps(bad))
                with self.assertRaises(RuntimeError):
                    setup.validate_stats(path)

    def test_state_write_is_atomic_and_leaves_no_temp(self):
        path = self.root / "state.json"
        setup.write_state(path, {"exe": "old"})
        setup.write_state(path, {"exe": "new"})
        self.assertEqual(json.loads(path.read_text()), {"exe": "new"})
        self.assertFalse(path.with_suffix(".tmp").exists())

    def test_smoke_requires_milestone_and_current_framebuffer(self):
        image = self.root / "screen.ppm"
        self.assertTrue(smoke.assess(4, "[game] main menu open", image))
        image.write_bytes(b"P6\n32 32\n255\n" + bytes(range(256)) * 12)
        self.assertEqual(smoke.assess(4, "[game] main menu open", image), [])
        self.assertTrue(smoke.assess(3, "[game] main menu open", image))
        self.assertTrue(smoke.assess(4, "no menu", image))
        self.assertTrue(smoke.assess(4, "[game] main menu open\n=== fault:", image))

    def test_smoke_rejects_game_error_dialog_after_menu(self):
        image = self.root / "error.ppm"
        image.write_bytes(b"P6\n32 32\n255\n" + bytes(range(256))*12)
        failures = smoke.assess(4, "[game] main menu open\n[messagebox] Load Error: FILE NOT FOUND", image)
        self.assertTrue(any("unexpected game dialog" in x for x in failures))

    def test_smoke_allows_only_known_steam_initialization_notice(self):
        image = self.root / "screen.ppm"
        image.write_bytes(b"P6\n32 32\n255\n" + bytes(range(256))*12)
        known = "[messagebox] Fatal Error: Steam must be running to play this game(SteamAPI_Init() failed)."
        self.assertEqual(smoke.assess(4, "[game] main menu open\n"+known, image), [])
        self.assertTrue(smoke.assess(4, "[game] main menu open\n[messagebox] Fatal Error: other Steam error", image))

    def test_smoke_rejects_blank_or_truncated_framebuffer(self):
        image = self.root / "screen.ppm"
        for pixels in (b"\x80" * 3072, bytes(range(256))):
            image.write_bytes(b"P6\n32 32\n255\n" + pixels)
            self.assertTrue(smoke.assess(4, "[game] main menu open", image))

    def test_fingerprint_changes_when_inputs_change(self):
        self.exe()
        for name in ("jgl", "sound"):
            (self.game / "Conquests" / f"{name}.dll").write_bytes(b"DLL fixture")
        for name in ("run_lift.py", "requirements-macos.txt", ".pcrecomp-revision"):
            (self.root / name).write_text(name)
        with patch.object(setup, "ROOT", self.root):
            first = setup.fingerprint(self.game, self.root / "tools")
            self.assertEqual(first, setup.fingerprint(self.game, self.root / "tools"))
            (self.root / "run_lift.py").write_text("changed generator")
            self.assertNotEqual(first, setup.fingerprint(self.game, self.root / "tools"))


    def test_asset_preflight_accepts_case_insensitive_paths(self):
        for paths in assets.REQUIRED_ASSETS.values():
            target = self.game / paths[-1].lower()
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(b"synthetic asset")
        self.assertEqual(assets.missing_assets(self.game), [])
        assets.validate_assets(self.game)

    def test_asset_preflight_reports_missing_cursor_and_music(self):
        missing = "\n".join(assets.missing_assets(self.game))
        self.assertIn("cursor animation", missing)
        self.assertIn("AncGRFull.mp3", missing)
        with self.assertRaises(RuntimeError):
            assets.validate_assets(self.game)

    def test_asset_lookup_rejects_path_escape(self):
        with self.assertRaises(ValueError):
            assets.find_casefold(self.game, "../outside")

    def test_fresh_settings_preserve_imported_bytes(self):
        ini = self.game / "Conquests/conquests.ini"
        original = b"[Conquests]\r\nLatest Save=Z:\\old\\save\r\n"
        ini.write_bytes(original)
        backup = assets.fresh_settings(self.game, self.root / "work")
        self.assertIsNotNone(backup)
        self.assertEqual(backup.read_bytes(), original)
        self.assertIn("PlayIntro=0", ini.read_text())
        self.assertNotIn("Latest Save", ini.read_text())
        self.assertNotIn("NoSound", ini.read_text())

    def test_fresh_settings_disable_only_unsafe_cultural_starts_rule(self):
        assets.fresh_settings(self.game, self.root / "work")
        lines = (self.game / "Conquests/conquests.ini").read_text().splitlines()
        rule = next(line.split("=", 1)[1] for line in lines if line.startswith("Rules="))
        self.assertEqual(len(rule), 32)
        self.assertEqual(int(rule, 2) ^ 0x000481DF, 0x40)
        self.assertEqual(int(rule, 2) & 0x40, 0)

    def test_settings_backups_do_not_overwrite_each_other(self):
        ini = self.game / "Conquests/conquests.ini"
        ini.write_text("first preferences")
        first = assets.fresh_settings(self.game, self.root / "work")
        ini.write_text("second preferences")
        second = assets.fresh_settings(self.game, self.root / "work")
        self.assertNotEqual(first, second)
        self.assertEqual(first.read_text(), "first preferences")
        self.assertEqual(second.read_text(), "second preferences")


if __name__ == "__main__":
    unittest.main()
