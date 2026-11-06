#!/usr/bin/env python3
"""Run the real host using original, synthetic PE fixtures (no game assets)."""
import pathlib
import struct
import subprocess
import sys
import tempfile


def make_pe(path, base, entry, span, dll=False):
    data = bytearray(0x600)
    struct.pack_into("<H", data, 0, 0x5A4D)
    struct.pack_into("<I", data, 0x3C, 0x80)
    struct.pack_into("<IHHIIIHH", data, 0x80, 0x4550, 0x14C, 1, 0, 0, 0,
                     224, 0x2102 if dll else 0x102)
    opt = 0x98
    struct.pack_into("<H", data, opt, 0x10B)
    for offset, value in {16: entry, 28: base, 32: 0x1000, 36: 0x200,
                          56: span, 60: 0x200, 92: 16}.items():
        struct.pack_into("<I", data, opt + offset, value)
    section = opt + 224
    data[section:section + 8] = b".data\0\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x400, 0x1000, 0x400, 0x200)
    struct.pack_into("<I", data, section + 36, 0xC0000040)

    def put(rva, fmt, *values):
        struct.pack_into(fmt, data, rva - 0x1000 + 0x200, *values)

    def string(rva, value):
        encoded = value.encode("ascii") + b"\0"
        off = rva - 0x1000 + 0x200
        data[off:off + len(encoded)] = encoded

    struct.pack_into("<II", data, opt + 104, 0x1010, 40)
    put(0x1010, "<IIIII", 0x1040, 0, 0, 0x10A0, 0x1050)
    put(0x1040, "<II", 0x1060, 0)
    put(0x1050, "<II", 0x1060, 0)
    string(0x1062, "GetTickCount")
    string(0x10A0, "KERNEL32.dll")
    if dll:
        struct.pack_into("<II", data, opt + 96, 0x1100, 40)
        put(0x1100, "<IIHHIIIIIII", 0, 0, 0, 0, 0, 1, 1, 1,
            0x1140, 0x1150, 0x1160)
        put(0x1140, "<I", 0x1200)
        put(0x1150, "<I", 0x1170)
        put(0x1160, "<H", 0)
        string(0x1170, "FixtureExport")
    path.write_bytes(data)


def run(binary, root, *args, expected=0):
    result = subprocess.run([str(binary), *args], cwd=root, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
    if result.returncode != expected:
        raise AssertionError(f"exit {result.returncode}, expected {expected}:\n{result.stdout}")
    return result.stdout


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="civ3 Mac's path ") as temp:
        root = pathlib.Path(temp)
        (root / "game" / "Conquests").mkdir(parents=True)
        work = root / "work"
        work.mkdir()
        assert "usage:" in run(binary, root, "--help")
        assert "no game folder" in run(binary, root, "--game", "missing", expected=1)
        make_pe(work / "Civ3Conquests.exe", 0x400000, 0x26B0A9, 0x900000)
        make_pe(work / "jgl.dll", 0x10000000, 0x1000, 0x77000, dll=True)
        make_pe(work / "sound.dll", 0x10080000, 0x1000, 0xBB000, dll=True)
        assert "dry run: image mapped and bound" in run(binary, root, "--headless")
        output = run(binary, root, "--headless", "--run")
        assert "civ3 host smoke: startup, guest pointers, DLL imports and exports passed" in output, output
        print(output)
        # A second process also creates a window through SDL's dummy driver.
        assert "DLL imports and exports passed" in run(binary, root, "--run")
    print("Host smoke tests passed (synthetic guests only).")


if __name__ == "__main__":
    main()
