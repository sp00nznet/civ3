#!/usr/bin/env python3
"""Reproducible, resumable macOS setup. Original game installs are read-only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

from macos_assets import fresh_settings, validate_assets

ROOT = Path(__file__).resolve().parents[1]
SUPPORTED_SHA1 = "01f705b1ce6f5d1cfd5a861f88ca1413764f8019"
PACKAGES = ("sdl2", "SDL2_ttf", "libavformat", "libavcodec", "libavutil", "libswresample")


def digest(path: Path, algorithm: str = "sha256") -> str:
    h = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def game_root(path: Path) -> Path:
    path = path.expanduser().resolve()
    return path.parent if path.name.lower() == "conquests" else path


def supported_executable(root: Path) -> Path:
    """Prefer the supported unmodified image, including a user's .orig backup."""
    for name in ("Civ3Conquests.exe", "Civ3Conquests.exe.orig"):
        candidate = root / "Conquests" / name
        if candidate.is_file() and digest(candidate, "sha1") == SUPPORTED_SHA1:
            return candidate
    raise RuntimeError(f"No supported Steam executable in {root}/Conquests. "
                       "Retail, GOG and patched executables are not interchangeable with this lift.")


def find_game(explicit: str | None) -> Path:
    if explicit:
        candidates = [game_root(Path(explicit))]
    else:
        home = Path.home()
        steam = home / "Library/Application Support/Steam"
        libraries = [steam]
        vdf = steam / "steamapps/libraryfolders.vdf"
        if vdf.is_file():
            libraries += [Path(p) for p in re.findall(r'"path"\s*"([^"]+)"', vdf.read_text(errors="replace"))]
        candidates = [ROOT / "game", home / "Games/Sid Meier's Civilization III Complete"]
        candidates += [p / "steamapps/common/Sid Meier's Civilization III Complete" for p in libraries]
    for root in candidates:
        try:
            supported_executable(root)
        except RuntimeError:
            continue
        if all((root / "Conquests" / f"{m}.dll").is_file() for m in ("jgl", "sound")):
            return root.resolve()
    raise RuntimeError("Supported Civilization III Complete not found. Use --game-dir DIR "
                       "with your own Steam installation, including jgl.dll and sound.dll.")


def run(command: list[str], log: Path | None = None, env: dict | None = None) -> None:
    print("+ " + " ".join(map(str, command)), flush=True)
    if log is None:
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        return
    with log.open("w") as output:
        result = subprocess.run(command, cwd=ROOT, env=env, stdout=output,
                                stderr=subprocess.STDOUT)
    if result.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-25:]), file=sys.stderr)
        raise RuntimeError(f"Step failed ({result.returncode}); see {log}")


def prerequisites(install: bool) -> None:
    missing = [x for x in ("git", "cmake", "ninja", "pkg-config", "clang") if not shutil.which(x)]
    libs_ok = shutil.which("pkg-config") and subprocess.run(
        ["pkg-config", "--exists", *PACKAGES], capture_output=True).returncode == 0
    if missing or not libs_ok:
        if not install:
            raise RuntimeError("Missing native build tools or SDL2/SDL2_ttf/FFmpeg. "
                               "Run ./setup-macos.sh --install-deps (requires Homebrew and Xcode Command Line Tools).")
        if not shutil.which("brew"):
            raise RuntimeError("Install Homebrew first; this script does not run remote shell installers.")
        env = dict(os.environ, HOMEBREW_NO_AUTO_UPDATE="1", HOMEBREW_NO_INSTALL_UPGRADE="1")
        run(["brew", "install", "cmake", "ninja", "pkgconf", "sdl2", "sdl2_ttf", "ffmpeg"], env=env)
    run(["xcrun", "--find", "clang"])


def toolkit(check_only: bool) -> Path:
    pinned = (ROOT / ".pcrecomp-revision").read_text().strip()
    path = Path(os.environ.get("PCRECOMP", str(ROOT.parent / "tools"))).expanduser().resolve()
    if not (path / ".git").exists():
        if check_only:
            raise RuntimeError(f"Missing pcrecomp checkout at {path}")
        if path.exists():
            raise RuntimeError(f"Refusing to overwrite non-Git directory {path}")
        run(["git", "clone", "https://github.com/sp00nznet/pcrecomp.git", str(path)])
        run(["git", "-C", str(path), "checkout", "--detach", pinned])
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != pinned:
        raise RuntimeError(f"pcrecomp revision differs from tested {pinned}. "
                           "Point PCRECOMP at a separate checkout of the pinned revision; existing work was not changed.")
    if subprocess.check_output(["git", "-C", str(path), "status", "--porcelain", "--untracked-files=no"], text=True).strip():
        raise RuntimeError("pcrecomp has local changes; use a clean pinned checkout for reproducible setup.")
    return path


def fingerprint(game: Path, tools: Path) -> str:
    values = [digest(supported_executable(game))]
    values += [digest(game / "Conquests" / f"{m}.dll") for m in ("jgl", "sound")]
    values += [digest(ROOT / p) for p in ("run_lift.py", "requirements-macos.txt", ".pcrecomp-revision")]
    values += [str(tools), "split=100", "macos-setup-v1"]
    return hashlib.sha256("\n".join(values).encode()).hexdigest()


def validate_stats(path: Path) -> dict:
    stats = json.loads(path.read_text())
    if stats.get("errors") != 0 or stats.get("stubs") != 0 or stats.get("lifted", 0) <= 0:
        raise RuntimeError(f"Incomplete lift: {path}: {stats}")
    return stats


def write_state(path: Path, state: dict) -> None:
    temp = path.with_suffix(".tmp")
    temp.write_text(json.dumps(state, indent=2) + "\n")
    temp.replace(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", help="Install root or its Conquests directory")
    parser.add_argument("--install-deps", action="store_true", help="Install missing Homebrew dependencies")
    parser.add_argument("--force", action="store_true", help="Re-catalog and re-lift; preserve game saves")
    parser.add_argument("--check", action="store_true", help="Read-only prerequisite, toolkit and game checks")
    parser.add_argument("--skip-build", action="store_true")
    settings = parser.add_mutually_exclusive_group()
    settings.add_argument("--keep-settings", action="store_true", help="Keep imported Windows preferences on first setup")
    settings.add_argument("--reset-settings", action="store_true", help="Back up and reset preferences in the isolated game copy")
    args = parser.parse_args(argv)
    if platform.system() != "Darwin":
        parser.error("Run this setup on macOS. Linux uses setup.sh.")
    if args.check and (args.install_deps or args.reset_settings):
        parser.error("--check is read-only and cannot install dependencies or reset preferences")
    prerequisites(args.install_deps)
    tools = toolkit(args.check)
    source = find_game(args.game_dir)
    print(f"Supported game: {source}\npcrecomp: {tools}", flush=True)
    if args.check:
        validate_assets(source)
        return 0
    work = ROOT / "work"
    work.mkdir(exist_ok=True)
    game = ROOT / "game"
    if game.is_symlink():
        raise RuntimeError("game/ is a symlink. Use a private copy to keep original saves and settings untouched.")
    new_copy = not game.exists()
    if new_copy:
        # APFS clone is copy-on-write; unlike a symlink, game writes stay private.
        with tempfile.TemporaryDirectory(prefix=".macos-stage-", dir=ROOT) as temporary:
            staged = Path(temporary) / "game"
            run(["/bin/cp", "-cR", str(source), str(staged)])
            staged.rename(game)
    elif source != game.resolve():
        print("Using existing isolated game/. Move it aside (preserving saves) before switching installations.")
    validate_assets(game)
    if args.reset_settings or (new_copy and not args.keep_settings):
        backup = fresh_settings(game, work)
        print(f"Fresh macOS preferences; imported settings backup: {backup or 'none'}", flush=True)
    original = supported_executable(game)
    target = game / "Conquests/Civ3Conquests.exe"
    if original != target:
        shutil.copy2(original, target)
    shutil.copy2(target, work / "Civ3Conquests.exe")
    python = ROOT / ".venv/bin/python"
    if not python.exists():
        run([sys.executable, "-m", "venv", str(ROOT / ".venv")])
    requirements = (ROOT / "requirements-macos.txt").read_text().splitlines()
    check = "import importlib.metadata as m; " + "; ".join(
        f"assert m.version({line.split('==')[0]!r}) == {line.split('==')[1]!r}" for line in requirements if "==" in line)
    if subprocess.run([str(python), "-c", check], capture_output=True).returncode:
        run([str(python), "-m", "pip", "install", "--disable-pip-version-check", "-r", str(ROOT / "requirements-macos.txt")])
    env = dict(os.environ, PCRECOMP=str(tools))
    state_path = work / "macos-setup-state.json"
    try:
        state = json.loads(state_path.read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        state = {}
    key = fingerprint(game, tools)
    for module in ("exe", "jgl", "sound"):
        prefix = "" if module == "exe" else module + "_"
        stats_path = work / f"{prefix}lift_stats.json"
        output = ROOT / "src/recomp" / ("gen" if module == "exe" else f"gen_{module}")
        products = [output / "recomp_dispatch.inc", output / "recomp_funcs.h", stats_path,
                    work / f"{prefix}functions.json", work / f"{prefix}vtable_seeds.json",
                    work / ("Civ3Conquests.exe" if module == "exe" else f"{module}.dll")]
        if not args.force and state.get(module) == key and all(p.is_file() for p in products) and list(output.glob("recomp_[0-9]*.c")):
            validate_stats(stats_path)
            print(f"Reusing completed {module} lift", flush=True)
            continue
        state.pop(module, None)
        write_state(state_path, state)
        if module != "exe":
            run([str(python), "-c", f"import run_lift; run_lift.copy_dll({module!r})"], env=env)
        binary = work / ("Civ3Conquests.exe" if module == "exe" else f"{module}.dll")
        seeds = work / f"{prefix}vtable_seeds.json"
        catalog = work / f"{prefix}functions.json"
        run([str(python), str(tools / "tools/cpp/vtable_scan.py"), str(binary), "--seeds", str(seeds)], env=env)
        run([str(python), "-u", str(tools / "tools/disasm/disasm32.py"), str(binary), "-o", str(catalog),
             "--seed-functions", str(seeds)], work / f"macos-catalog-{module}.log", env)
        command = [str(python), "-u", "run_lift.py", "--all", "--split", "100"]
        if module != "exe":
            command += ["--module", module]
        run(command, work / f"macos-lift-{module}.log", env)
        print(validate_stats(stats_path), flush=True)
        state[module] = key
        write_state(state_path, state)
    if not args.skip_build:
        run([str(ROOT / "build-macos.sh")], env=env)
    launcher = ROOT / "Civilization III (recomp).command"
    launcher.write_text('#!/bin/bash\nset -euo pipefail\ncd "$(dirname "$0")"\nexec ./script/build_and_run.sh "$@"\n')
    launcher.chmod(0o755)
    print("Ready: ./script/build_and_run.sh (or double-click Civilization III (recomp).command)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"Setup stopped: {error}", file=sys.stderr)
        raise SystemExit(1)
