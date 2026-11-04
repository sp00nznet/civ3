#!/usr/bin/env python3
"""Resumable native macOS setup. Original Steam game files stay local."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
SUPPORTED_EXE_SHA1 = "01f705b1ce6f5d1cfd5a861f88ca1413764f8019"
PACKAGES = ["cmake", "ninja", "pkgconf", "sdl2", "sdl2_ttf", "ffmpeg", "python"]


def digest(path, algorithm="sha256"):
    value = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def run(args, env=None, log=None):
    print("+ " + shlex.join(map(str, args)), flush=True)
    if log is None:
        subprocess.run(list(map(str, args)), cwd=REPO, env=env, check=True)
        return
    with log.open("w") as output:
        result = subprocess.run(list(map(str, args)), cwd=REPO, env=env,
                                stdout=output, stderr=subprocess.STDOUT)
    if result.returncode:
        print(log.read_text(errors="replace")[-6000:], file=sys.stderr)
        raise RuntimeError(f"command failed; see {log}")


def game_root(path):
    path = path.expanduser().resolve()
    if path.name.lower() == "conquests":
        path = path.parent
    if all((path / "Conquests" / name).is_file()
           for name in ("Civ3Conquests.exe", "jgl.dll", "sound.dll")):
        return path
    return None


def find_game(requested):
    if requested:
        found = game_root(Path(requested))
        if not found:
            raise RuntimeError("--game-dir needs the Complete install containing Conquests/Civ3Conquests.exe, jgl.dll and sound.dll")
        return found
    found = game_root(REPO / "game")
    if found:
        return found
    steam = Path.home() / "Library/Application Support/Steam"
    libraries = [steam]
    vdf = steam / "steamapps/libraryfolders.vdf"
    if vdf.is_file():
        libraries += [Path(p.replace("\\\\", "\\"))
                      for p in re.findall(r'"path"\s*"([^"\n]+)"', vdf.read_text(errors="replace"))]
    for library in libraries:
        found = game_root(library / "steamapps/common/Sid Meier's Civilization III Complete")
        if found:
            return found
    raise RuntimeError("game files not found. Run ./setup-macos.sh --game-dir '/path/to/Sid Meier\'s Civilization III Complete' with your Windows Steam install")


def validate_game(path):
    exe = path / "Conquests/Civ3Conquests.exe"
    actual = digest(exe, "sha1")
    if actual != SUPPORTED_EXE_SHA1:
        raise RuntimeError(f"unsupported Civ3Conquests.exe (SHA-1 {actual}). This host requires the unmodified 2015 Steam Complete executable; retail/GOG/Mac editions are not supported")


def check_tools():
    missing = [name for name in ("git", "clang", "cmake", "ninja", "pkg-config")
               if shutil.which(name) is None]
    if shutil.which("pkg-config") and subprocess.run(
        ["pkg-config", "--exists", "sdl2", "SDL2_ttf", "libavformat", "libavcodec", "libavutil", "libswresample"]
    ).returncode:
        missing.append("SDL2, SDL2_ttf or FFmpeg development libraries")
    if missing:
        raise RuntimeError("missing " + ", ".join(missing) + ". Install Xcode Command Line Tools (xcode-select --install), then brew install " + " ".join(PACKAGES))
    if subprocess.run(["xcode-select", "-p"], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode:
        raise RuntimeError("install Xcode Command Line Tools: xcode-select --install")


def prepare_toolkit(check_only):
    revision = (REPO / "pcrecomp-revision.txt").read_text().strip()
    supplied = os.environ.get("PCRECOMP")
    toolkit = Path(supplied).expanduser().resolve() if supplied else REPO / ".deps/pcrecomp"
    if not toolkit.exists():
        if supplied or check_only:
            raise RuntimeError(f"no pcrecomp checkout at {toolkit}; normal setup clones the pinned runtime automatically")
        toolkit.parent.mkdir(parents=True, exist_ok=True)
        run(["git", "clone", "--no-checkout", "https://github.com/sp00nznet/pcrecomp.git", toolkit])
        run(["git", "-C", toolkit, "checkout", "--detach", revision])
    if not (toolkit / "runtime/win32hle/guest_mem.c").is_file():
        raise RuntimeError(f"{toolkit} is too old or incomplete (no guest_mem.c)")
    actual = subprocess.check_output(["git", "-C", str(toolkit), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(toolkit), "status", "--porcelain"], text=True).strip()
    if actual != revision or dirty:
        raise RuntimeError(f"pcrecomp must be a clean checkout of {revision}; use a separate checkout via PCRECOMP. Existing work was left intact")
    return toolkit, revision


def write_launcher():
    # Resolve the repository at launch time, so moving the whole folder works.
    launcher = REPO / "Play-Civilization-III.command"
    launcher.write_text('''#!/bin/sh
set -eu
cd "$(dirname "$0")"
case "$(uname -m)" in
  arm64) PATH="/opt/homebrew/bin:$PATH" ;;
  x86_64) PATH="/usr/local/bin:$PATH" ;;
esac
export PATH
exec ./build-macos/civ3 --run "$@"
''')
    launcher.chmod(0o755)
    return launcher


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", help="Complete install, or its Conquests folder")
    parser.add_argument("--install-deps", action="store_true", help="install the required Homebrew packages")
    parser.add_argument("--check", action="store_true", help="check prerequisites without installing or building")
    parser.add_argument("--force", action="store_true", help="redo generated stages; never delete a real game directory")
    args = parser.parse_args()
    if args.check and args.install_deps:
        parser.error("--check and --install-deps cannot be combined")
    if sys.version_info < (3, 10):
        raise RuntimeError("Python 3.10+ is required: brew install python")
    if platform.system() != "Darwin":
        raise RuntimeError("this setup is for macOS; use setup.sh on Linux or Setup.cmd on Windows")
    if platform.machine() not in ("arm64", "x86_64"):
        raise RuntimeError("a 64-bit Apple Silicon or Intel Mac is required")
    translated = subprocess.run(["sysctl", "-n", "sysctl.proc_translated"], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if translated.stdout.strip() == "1":
        raise RuntimeError("open a native Terminal on Apple Silicon (disable Open using Rosetta) and use /opt/homebrew Python")
    # Validate the user's input before downloading dependencies or copying files.
    source = find_game(args.game_dir)
    validate_game(source)
    if args.install_deps:
        if not shutil.which("brew"):
            raise RuntimeError("Homebrew is not installed. Install it from https://brew.sh, then rerun with --install-deps")
        run(["brew", "install", *PACKAGES])
    check_tools()
    toolkit, revision = prepare_toolkit(args.check)
    print(f"Native {platform.machine()} | game: {source} | pcrecomp: {revision}", flush=True)
    if args.check:
        print("Prerequisites ready. No files were changed.")
        return

    game = REPO / "game"
    if game.is_symlink() and game.resolve() != source and args.force:
        game.unlink()
    if game.exists() or game.is_symlink():
        if game.resolve() != source:
            raise RuntimeError("game/ already points to a different install. Use --force to replace a symlink, or move a real game/ folder yourself")
    else:
        game.symlink_to(source, target_is_directory=True)
    print("game/ links to this install; saves and conquests.ini will be written there when you play.")

    work = REPO / "work"
    work.mkdir(exist_ok=True)
    python = REPO / ".venv/bin/python3"
    if not python.exists():
        run([sys.executable, "-m", "venv", REPO / ".venv"])
    probe = subprocess.run([str(python), "-c", "import pefile, capstone"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if probe.returncode:
        run([python, "-m", "pip", "install", "pefile>=2024.8.26", "capstone>=5,<6"])
    env = dict(os.environ, PCRECOMP=str(toolkit), BUILD_DIR=str(REPO / "build-macos"),
               CIV3_BUILD_TESTS_ONLY="0")
    versions = subprocess.check_output([str(python), "-c",
        "import pefile,capstone; print(pefile.__version__,capstone.__version__)"], text=True).strip()
    inputs = {name: digest(source / "Conquests" / name)
              for name in ("Civ3Conquests.exe", "jgl.dll", "sound.dll")}
    inputs.update(runtime=revision, lifter=digest(REPO / "run_lift.py"), packages=versions)
    state_file = work / "macos-setup.json"
    try:
        state = json.loads(state_file.read_text())
    except (OSError, ValueError):
        state = {}
    if args.force or state.get("inputs") != inputs:
        state = {"inputs": inputs, "finished": []}

    def save_state():
        temporary = state_file.with_suffix(".tmp")
        temporary.write_text(json.dumps(state, indent=2) + "\n")
        temporary.replace(state_file)

    def stage(name, outputs, commands, stats=None):
        def complete():
            if not all(path.is_file() for path in outputs):
                return False
            if stats:
                try:
                    health = json.loads(stats.read_text())
                    chunks = health["files"]
                    return (health.get("errors") == 0 and health.get("stubs") == 0
                            and isinstance(chunks, int) and chunks > 0
                            and all((outputs[0].parent / f"recomp_{i:04d}.c").is_file()
                                    for i in range(chunks)))
                except (OSError, ValueError, KeyError):
                    return False
            return True

        if name in state["finished"] and complete():
            print(f"[skip] {name}", flush=True)
            return
        # Invalidate the old completion marker before a retry: a failed command
        # may leave its previous outputs on disk, which must not count as done.
        if name in state["finished"]:
            state["finished"].remove(name)
        save_state()
        print(f"[build] {name}", flush=True)
        for index, command in enumerate(commands):
            run(command, env=env, log=work / f"macos-{name}-{index}.log")
        if not complete():
            raise RuntimeError(f"{name} has incomplete outputs or lift errors/stubs; see work/macos-{name}-*.log")
        if name not in state["finished"]:
            state["finished"].append(name)
        save_state()

    # Refresh the executable copy; catalog/lift stages are keyed by all inputs.
    shutil.copy2(source / "Conquests/Civ3Conquests.exe", work / "Civ3Conquests.exe")
    for module in ("exe", "jgl", "sound"):
        prefix = "" if module == "exe" else module + "_"
        image = work / ("Civ3Conquests.exe" if module == "exe" else module + ".dll")
        seeds = work / (prefix + "vtable_seeds.json")
        catalog = work / (prefix + "functions.json")
        commands = []
        if module != "exe":
            commands.append([python, "-c", f"import run_lift; run_lift.copy_dll('{module}')"])
        commands += [[python, toolkit / "tools/cpp/vtable_scan.py", image, "--seeds", seeds],
                     [python, toolkit / "tools/disasm/disasm32.py", image, "-o", catalog,
                      "--seed-functions", seeds]]
        stage(f"catalog-{module}", [image, seeds, catalog], commands)
        output = REPO / "src/recomp" / ("gen" if module == "exe" else "gen_" + module)
        stats = work / (prefix + "lift_stats.json")
        command = [python, REPO / "run_lift.py", "--all"]
        if module != "exe":
            command += ["--module", module]
        stage(f"lift-{module}", [output / "recomp_funcs.h", output / "recomp_dispatch.inc",
                                output / "recomp_0000.c", stats], [command], stats=stats)

    # Always let CMake/Ninja check source and dependency changes on a rerun.
    run([REPO / "build-macos.sh"], env=env)
    print(f"\nBuilt: {REPO / 'build-macos/civ3'}\nDouble-click {write_launcher().name} to play.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        print(f"\nSetup stopped: {exc}", file=sys.stderr)
        sys.exit(1)
