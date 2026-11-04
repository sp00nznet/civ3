#!/usr/bin/env python3
"""Read-only launch checks for the selected local game copy."""
from __future__ import annotations
import argparse
from pathlib import Path
import sys
from macos_assets import settings_warnings, validate_assets

ROOT = Path(__file__).resolve().parents[1]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--game", type=Path, default=ROOT / "game")
    parser.add_argument("--work", type=Path, default=ROOT / "work")
    parser.add_argument("--exe", type=Path, default=ROOT / "work/Civ3Conquests.exe")
    args, _ = parser.parse_known_args(argv)
    game = args.game.expanduser().resolve()
    if game.name.casefold() == "conquests":
        game = game.parent
    try:
        validate_assets(game)
        for path in (args.exe, args.work / "jgl.dll", args.work / "sound.dll"):
            if not path.is_file():
                raise RuntimeError(f"Missing runtime input: {path}. Run ./setup-macos.sh first.")
        for warning in settings_warnings(game):
            print(f"WARNING: {warning}", file=sys.stderr)
    except (OSError, RuntimeError) as error:
        print(f"Cannot launch: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
