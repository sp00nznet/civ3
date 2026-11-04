#!/usr/bin/env python3
"""Validate native save -> process exit -> reload -> continued turns in private copies."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from macos_smoke import assess
from macos_qa_cleanup import remove_clones

ROOT = Path(__file__).resolve().parents[1]
LOAD = ["--move", "240,425@1", "--open", "240,425@2", "--click", "621,557@10",
        "--wait", "0xA74EA4@11", "--key", "0x0D@14"]
BAD = ("Fourcc mismatch", "DataIO Error", "[not-lifted]", "=== fault:",
       "[win32hle] FATAL", "ICALL: unresolved", "ITAIL: unresolved", "uncaught exception")


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def turns(log: str) -> tuple[int | None, int | None]:
    loaded = re.findall(r"waited [\d.]+s for \[0xA74EA4\] = (\d+)", log)
    final = re.findall(r"\[game\] turn (\d+)", log)
    return (int(loaded[-1]) if loaded else None, int(final[-1]) if final else None)


def verdict(log: str, code: int, image: Path, expected: int | None = None,
            minimum_advance: int = 0) -> list[str]:
    failures = assess(code, log, image)
    failures += [f"runtime failure: {marker}" for marker in BAD if marker in log]
    loaded, final = turns(log)
    if loaded is None or loaded <= 0:
        failures.append("saved game did not reach a positive turn")
    elif expected is not None and loaded != expected:
        failures.append(f"restored turn {loaded}, expected {expected}")
    if final is None or (loaded is not None and final < loaded + minimum_advance):
        failures.append("not enough completed turns after loading")
    return failures


def select_save(game: Path, name: str) -> None:
    """Only for a freshly cloned QA copy: make the game's remembered name agree."""
    if Path(name).name != name or any(c in name for c in "\\\r\n") or not name.lower().endswith(".sav"):
        raise ValueError("Expected a single .SAV filename")
    ini = game / "Conquests/conquests.ini"
    text = ini.read_text(encoding="latin-1") if ini.exists() else "[Conquests]\n"
    lines = [line for line in text.splitlines()
             if not line.lower().startswith(("latest save=", "latest scenario="))]
    value = "Latest Save=C:\\Conquests\\Saves\\" + name[:-4]
    for i, line in enumerate(lines):
        if line.lower() == "[conquests]":
            lines.insert(i + 1, value)
            break
    else:
        lines = ["[Conquests]", value] + lines
    ini.write_text("\n".join(lines) + "\n", encoding="latin-1")


def private_game(source: Path, destination: Path, seed: Path, name: str) -> Path:
    if destination.exists() or destination.is_relative_to(source):
        raise RuntimeError("Refusing to overwrite a QA installation or clone inside its source")
    subprocess.run(["/bin/cp", "-cR", str(source), str(destination)], check=True)
    saves = destination / "Conquests/Saves"
    if saves.exists():
        saves.rename(destination.parent / (destination.name + "-inherited-saves"))
    saves.mkdir(parents=True)
    shutil.copy2(seed, saves / name)
    select_save(destination, name)
    return destination


def stage(binary: Path, game: Path, output: Path, name: str, actions: list[str],
          seconds: int, expected: int | None = None, advance: int = 0) -> dict:
    log_path, image = output / f"{name}.log", output / f"{name}.ppm"
    command = [str(binary), "--run", "--headless", "--watchdog", "180", "--play", str(seconds),
               "--game", str(game), "--work", str(ROOT / "work"),
               "--exe", str(ROOT / "work/Civ3Conquests.exe"), "--shot", str(image)] + LOAD + actions
    print(f"Running {name}; log: {log_path}", flush=True)
    with log_path.open("w") as stream:
        result = subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT,
                                timeout=seconds + 200)
    log = log_path.read_text(errors="replace")
    failures = verdict(log, result.returncode, image, expected, advance)
    record = {"returncode": result.returncode, "loaded_turn": turns(log)[0],
              "final_turn": turns(log)[1], "failures": failures, "log": log_path.name,
              "framebuffer": image.name}
    (output / f"{name}.json").write_text(json.dumps(record, indent=2) + "\n")
    if failures:
        raise RuntimeError(name + ": " + "; ".join(failures))
    print(f"{name}: loaded turn {record['loaded_turn']}, ended turn {record['final_turn']}", flush=True)
    return record


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed-save", type=Path, required=True, help="Owned save at turn 1 or later")
    parser.add_argument("--game-dir", type=Path, default=ROOT / "game")
    parser.add_argument("--binary", type=Path, default=ROOT / "build-macos/civ3")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--keep-test-games", action="store_true",
                        help="Keep disposable copies and their saves for debugging (off by default)")
    args = parser.parse_args()
    seed, source, binary = (p.expanduser().resolve() for p in (args.seed_save, args.game_dir, args.binary))
    if not seed.is_file() or not binary.is_file() or not (source / "Conquests").is_dir():
        parser.error("A real seed save, built native executable, and game installation are required")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = (args.output or ROOT / "work" / f"roundtrip-{stamp}").expanduser().resolve()
    if output.exists() or output.is_relative_to(source):
        parser.error("Output must be a new directory outside the source installation")
    output.mkdir(parents=True)
    seed_hash = digest(seed)
    report = {"seed_sha256": seed_hash, "binary_sha256": digest(binary), "success": False}
    try:
        first = private_game(source, output / "save-game", seed, "0000 Mac roundtrip.SAV")
        saved = stage(binary, first, output, "save", ["--key", "c+0x53@22", "--key", "0x0D@28"], 48)
        created = [p for p in (first / "Conquests/Saves").iterdir()
                   if p.is_file() and p.suffix.lower() == ".sav" and p.name != "0000 Mac roundtrip.SAV"]
        if len(created) != 1 or created[0].stat().st_size < 1024:
            raise RuntimeError("The Save dialog did not create exactly one new, nonempty manual save")
        manual = created[0]
        with manual.open("rb") as stream:
            signature = stream.read(2)
        if len(signature) != 2 or signature[0] not in (0, 1) or signature[1] not in (4, 5, 6):
            raise RuntimeError("The new manual save does not have a PKWARE DCL header")
        second = private_game(first, output / "reload-game", manual, manual.name)
        loaded = stage(binary, second, output, "reload", ["--autopilot", "3000@22"], 75,
                       expected=saved["loaded_turn"], advance=3)
        autos = list((second / "Conquests/Saves/Auto").glob("*.SAV"))
        if len(autos) < 3 or digest(manual) != digest(second / "Conquests/Saves" / manual.name):
            raise RuntimeError("Reload did not create three fresh autosaves or changed the loaded manual save")
        if digest(seed) != seed_hash:
            raise RuntimeError("Source save unexpectedly changed")
        report.update(success=True, save=saved, reload=loaded, manual_save_name=manual.name,
                      manual_save_sha256=digest(manual), manual_save_bytes=manual.stat().st_size,
                      fresh_autosaves=sorted(p.name for p in autos))
        print(f"PASS: compressed manual save reloaded at turn {loaded['loaded_turn']} and played to "
              f"turn {loaded['final_turn']}; source untouched. Evidence: {output}", flush=True)
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        report["error"] = str(error)
        print(f"FAIL: {error}. Evidence retained: {output}", file=sys.stderr)
        return 1
    finally:
        if not args.keep_test_games:
            try:
                removed = remove_clones(output, ("save-game", "reload-game",
                                                 "save-game-inherited-saves",
                                                 "reload-game-inherited-saves"))
                report["temporary_copies_removed"] = removed
            except (OSError, ValueError) as error:
                report["cleanup_warning"] = str(error)
                print(f"WARNING: QA copy cleanup: {error}", file=sys.stderr)
        (output / "result.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
