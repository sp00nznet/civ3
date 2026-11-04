#!/usr/bin/env python3
"""Boot a local native build and require real main-menu evidence (not just a PID)."""
from __future__ import annotations

import argparse
import re
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def assess(returncode: int, log: str, image: Path) -> list[str]:
    failures = []
    if returncode != 4:
        failures.append(f"expected watchdog exit 4, got {returncode}")
    if "[game] main menu open" not in log:
        failures.append("main-menu milestone was not reached")
    for marker in ("[not-lifted]", "=== fault:", "[win32hle] FATAL", "[fatal]"):
        if marker in log:
            failures.append(f"runtime failure marker: {marker}")
    for line in log.splitlines():
        if line.startswith("[messagebox]") and not line.startswith(
                "[messagebox] Fatal Error: Steam must be running to play this game(SteamAPI_Init() failed)."):
            failures.append("unexpected game dialog: " + line)
    if not image.is_file() or image.stat().st_size < 1024:
        failures.append("no usable framebuffer snapshot")
    else:
        data = image.read_bytes()
        header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
        if not header:
            failures.append("invalid PPM framebuffer header")
        else:
            width, height = int(header[1]), int(header[2])
            pixels = data[header.end():]
            if width < 32 or height < 32 or len(pixels) != width * height * 3:
                failures.append("truncated or incorrectly sized framebuffer")
            elif len(set(pixels)) < 8:
                failures.append("framebuffer is blank or nearly uniform")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build-macos/civ3")
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--work", type=Path, default=ROOT / "work")
    parser.add_argument("--exe", type=Path, default=ROOT / "work/Civ3Conquests.exe")
    args = parser.parse_args(argv)
    if not 10 <= args.timeout <= 86400:
        parser.error("--timeout must be between 10 and 86400 seconds")
    work = ROOT / "work"
    work.mkdir(exist_ok=True)
    image, log_path = work / "macos-menu.ppm", work / "macos-smoke.log"
    # Never let an old successful screenshot make a failed run look successful.
    image.unlink(missing_ok=True)
    command = [str(args.binary.expanduser().resolve()), "--headless", "--run", "--mute",
               "--watchdog", str(args.timeout), "--play", "3", "--shot", str(image),
               "--game", str(args.game.expanduser().resolve()),
               "--work", str(args.work.expanduser().resolve()),
               "--exe", str(args.exe.expanduser().resolve())]
    try:
        with log_path.open("w") as stream:
            result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=args.timeout + 15)
    except subprocess.TimeoutExpired:
        print(f"FAIL: host did not stop; see {log_path}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    log = log_path.read_text(errors="replace")
    failures = assess(result.returncode, log, image)
    if failures:
        print("FAIL: " + "; ".join(failures), file=sys.stderr)
        print("\n".join(log.splitlines()[-30:]), file=sys.stderr)
        return 1
    print(f"PASS: native game reached its main menu; framebuffer: {image}; log: {log_path}")
    print("This verifies boot/menu rendering, not complete gameplay or multiplayer.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
