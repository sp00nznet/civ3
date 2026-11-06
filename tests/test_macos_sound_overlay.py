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
    def test_resident_pcm_survives_trailing_metadata_and_refills_when_consumed(self):
        initial_call = "RECOMP_ICALL(edi); /* 0x100B40AD: call edi */\n"
        resident_call = "RECOMP_ICALL(edi); /* 0x100B40DA: call edi */"
        transformed, _ = overlay.transform(resident_call)
        initial, _ = overlay.transform(initial_call)
        self.assertEqual(initial, initial_call)
        source = """#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t state[16], ebp, esp, calls;
static unsigned char file[36], buffer[16], rendered[32];
static unsigned file_size, file_offset;
#define MEM32(a) state[(a)/4]
static void advance(void) {
    unsigned size = file_size - file_offset;
    if (size > sizeof buffer) size = sizeof buffer;
    memcpy(buffer, file + file_offset, size);
    file_offset += size;
    MEM32(ebp + 0x28) = 1000;
    MEM32(ebp + 0x2c) = 1000 + size;
    ++calls;
    esp += 12;
}
#define RECOMP_ICALL(a) advance()
static void initial_fill(void) {
INITIAL
}
static void resident_advance(void) {
BODY
}
static void prepare(unsigned pcm_size) {
    for (unsigned i = 0; i < pcm_size; ++i) file[i] = i + 1;
    memcpy(file + pcm_size, "smpl", 4);
    file_size = pcm_size + 4;
    file_offset = calls = 0;
    esp = 100;
    initial_fill();
}
int main(void) {
    prepare(16);
    resident_advance();
    MEM32(ebp + 0x2c) = 1016;
    memcpy(rendered, buffer, 16);
    if (memcmp(rendered, file, 16)) {
        fputs("unread PCM overwritten by trailing metadata\\n", stderr);
        return 1;
    }
    if (calls != 1 || esp != 124 || file_offset != 16 ||
            MEM32(ebp + 0x28) != 1000) return 2;
    prepare(32);
    memcpy(rendered, buffer, 16);
    MEM32(ebp + 0x28) = MEM32(ebp + 0x2c);
    resident_advance();
    memcpy(rendered + 16, buffer, 16);
    if (memcmp(rendered, file, 32)) {
        fputs("consumed PCM window did not refill without sample loss\\n", stderr);
        return 3;
    }
    return calls != 2 || esp != 124 || file_offset != 32 ||
        MEM32(ebp + 0x28) != 1000 || MEM32(ebp + 0x2c) != 1016;
}
""".replace("INITIAL", initial).replace("BODY", transformed)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "probe.c").write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", str(root / "probe.c"),
                            "-o", str(root / "probe")], check=True)
            subprocess.run([str(root / "probe")], check=True)

    def test_unread_frame_survives_and_call_arguments_are_removed(self):
        transformed, counts = overlay.transform("    " + overlay.CALL + "\n")
        self.assertEqual(counts["stream_prime"], 1)
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
        resident_call = "RECOMP_ICALL(edi); /* 0x100B40DA: call edi */"
        with self.assertRaises(ValueError):
            overlay.transform(resident_call.replace("RECOMP_ICALL", "CHANGED"))
        transformed, _ = overlay.transform(resident_call)
        with self.assertRaises(ValueError):
            overlay.transform(transformed)
        for address in overlay.COUNT_SITES:
            with self.assertRaises(ValueError):
                overlay.transform(f"CHANGED(); /* 0x{address}: mov dword ptr [ebx], eax */")

    def test_resident_render_count_reaches_the_audio_queue(self):
        functions = []
        for index, address in enumerate(overlay.COUNT_SITES):
            mode, context = overlay.RENDER_CONTEXTS[index]
            original = (f"PUSH32(esp, 1); /* 0x{mode}: push 1 */\n"
                        f"PUSH32(esp, edi); /* 0x{context}: push edi */\n"
                        f"MEM32(ebx) = eax; /* 0x{address}: mov dword ptr [ebx], eax */")
            transformed, counts = overlay.transform(original)
            self.assertEqual(counts[address], 1)
            pushes, store = transformed.rsplit("\n", 1)
            functions.append(f"""static int branch_{index}(void) {{
    unsigned esp = 128, ebx = 160, eax = 512, esi = 64;
    MEM32(esp + 0x18) = 0;
    MEM32(ebx) = 999;
    {pushes}
    if (MEM32(esp) != esi || MEM32(esp + 4) != 0) return 1;
    esp += 8;
    {store}
    eax = MEM32(esp + 0x18);
    MEM32(ebx) = eax;
    return MEM32(ebx) != 512;
}}
""")
        source = ("static unsigned memory[64];\n#define MEM32(a) memory[(a)/4]\n"
                  "#define PUSH32(sp, value) do { (sp) -= 4; MEM32(sp) = (value); } while (0)\n")
        source += "\n".join(functions)
        source += "int main(void) { return " + " || ".join(f"branch_{i}()" for i in range(4)) + "; }\n"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "probe.c").write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", str(root / "probe.c"),
                            "-o", str(root / "probe")], check=True)
            subprocess.run([str(root / "probe")], check=True)

    def test_additive_8bit_voice_preserves_existing_audio(self):
        transformed, counts = overlay.transform(overlay.ADD8_CALL)
        self.assertEqual(counts["resident_add8"], 1)
        source = """static int mixed = 200;
void sub_1008225C(void) { mixed = 100; }
void sub_100826B7(void) { mixed += 100; }
#define RECOMP_CALL(fn) fn()
int main(void) {
BODY
    return mixed != 300;
}
""".replace("BODY", transformed)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "probe.c").write_text(source)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", str(root / "probe.c"),
                            "-o", str(root / "probe")], check=True)
            subprocess.run([str(root / "probe")], check=True)
        with self.assertRaises(ValueError):
            overlay.transform(overlay.ADD8_CALL.replace("RECOMP_CALL", "CHANGED"))

    def test_stages_all_aliases_without_modifying_originals(self):
        with tempfile.TemporaryDirectory(prefix="sound overlay ") as directory:
            root = Path(directory)
            source, output = root / "input", root / "build"
            source.mkdir()
            original = source / "recomp_0001.c"
            resident_call = "RECOMP_ICALL(edi); /* 0x100B40DA: call edi */"
            text = ((overlay.CALL + "\n") * 4 + (resident_call + "\n") * 4 + "\n".join(
                f"MEM32(ebx) = eax; /* 0x{address}: mov dword ptr [ebx], eax */"
                for address in overlay.COUNT_SITES))
            text += "\n" + "\n".join(
                f"PUSH32(esp, 1); /* 0x{mode}: push 1 */\n"
                f"PUSH32(esp, edi); /* 0x{context}: push edi */"
                for mode, context in overlay.RENDER_CONTEXTS)
            text += "\n" + overlay.ADD8_CALL
            original.write_text(text)
            image = root / "synthetic.dll"
            image.write_bytes(b"synthetic image")
            with patch.object(overlay, "SUPPORTED_SHA256", hashlib.sha256(image.read_bytes()).hexdigest()):
                self.assertEqual(overlay.stage(source, output, image), overlay.EXPECTED)
                self.assertEqual(original.read_text(), text)
                self.assertEqual((output / original.name).read_text().count("civ3_preserve_unread_pcm"), 8)
                self.assertIn("DLL_SOURCES", (output / "sources.cmake").read_text())
                with self.assertRaises(ValueError):
                    overlay.stage(source, source / "bad", image)
                original.write_text(text.replace(resident_call + "\n", "", 1))
                with self.assertRaises(ValueError):
                    overlay.stage(source, root / "missing_alias", image)
                self.assertFalse((root / "missing_alias").exists())
                original.write_text(overlay.CALL)
                with self.assertRaises(ValueError):
                    overlay.stage(source, root / "incomplete", image)
                self.assertFalse((root / "incomplete").exists())
            with self.assertRaises(ValueError):
                overlay.stage(source, output, image)


if __name__ == "__main__":
    unittest.main()
