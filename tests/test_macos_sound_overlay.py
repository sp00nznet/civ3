"""Synthetic call sites verify the loader's refill and stack behavior."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import macos_sound_overlay as overlay


class SoundOverlayTests(unittest.TestCase):
    def test_unread_frame_survives_and_call_arguments_are_removed(self):
        transformed, count = overlay.transform("    " + overlay.CALL + "\n")
        self.assertEqual(count, 1)
        source = """#include <stdint.h>
static uint32_t state[16], ebp, esp, calls;
#define MEM32(a) state[(a)/4]
#define RECOMP_ICALL(a) do { ++calls; esp += 12; } while (0)
static void prime(void) {
BODY
}
int main(void) {
    state[0x28/4] = 28; state[0x2c/4] = 32; esp = 100;
    prime();
    if (calls || esp != 112 || state[0x28/4] != 28) return 1;
    state[0x28/4] = 32; esp = 100;
    prime();
    return calls != 1 || esp != 112;
}
""".replace("BODY", transformed)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "probe.c").write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", str(root / "probe.c"),
                            "-o", str(root / "probe")], check=True)
            subprocess.run([str(root / "probe")], check=True)

    def test_rejects_changed_instruction_and_already_patched_input(self):
        with self.assertRaises(ValueError):
            overlay.transform(overlay.CALL.replace("MEM32", "CHANGED"))
        transformed, _ = overlay.transform(overlay.CALL)
        with self.assertRaises(ValueError):
            overlay.transform(transformed)

    def test_stages_all_aliases_without_modifying_originals(self):
        with tempfile.TemporaryDirectory(prefix="sound overlay ") as directory:
            root = Path(directory)
            source, output = root / "input", root / "build"
            source.mkdir()
            original = source / "recomp_0001.c"
            text = (overlay.CALL + "\n") * 4
            original.write_text(text)
            image = root / "synthetic.dll"
            image.write_bytes(b"synthetic image")
            with patch.object(overlay, "SUPPORTED_SHA256", hashlib.sha256(image.read_bytes()).hexdigest()):
                self.assertEqual(overlay.stage(source, output, image), 4)
                self.assertEqual(original.read_text(), text)
                self.assertEqual((output / original.name).read_text().count("civ3_preserve_unread_pcm"), 4)
                self.assertIn("DLL_SOURCES", (output / "sources.cmake").read_text())
                with self.assertRaises(ValueError):
                    overlay.stage(source, source / "bad", image)
                original.write_text(overlay.CALL)
                with self.assertRaises(ValueError):
                    overlay.stage(source, root / "incomplete", image)
                self.assertFalse((root / "incomplete").exists())
            with self.assertRaises(ValueError):
                overlay.stage(source, output, image)


if __name__ == "__main__":
    unittest.main()
