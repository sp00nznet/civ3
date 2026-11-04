"""Small preflight for startup-critical assets; not a complete installation verifier."""
from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
import shutil

# Steam defaults with only culturally linked starts (rule bit 0x40) disabled.
# That optional path reads an unset civilization (-1) during Quick Start and
# can underflow the rules allocation on the native based-memory host.
SAFE_DEFAULT_RULES = 0x000481DF & ~0x40

# Alternative paths follow the game's Conquests -> PTW -> base search order.
REQUIRED_ASSETS = {
    "Conquests rules": ("Conquests/conquests.biq",),
    "UI labels": ("Conquests/Text/labels.txt",),
    "UI messages": ("Conquests/Text/script.txt",),
    "Lucida Sans font": ("Conquests/LSANS.TTF", "LSANS.TTF"),
    "cursor animation settings": tuple(f"{p}Art/Animations/Cursor/Cursor.INI" for p in ("Conquests/", "civ3PTW/", "")),
    "cursor animation frames": tuple(f"{p}Art/Animations/Cursor/Cursor.flc" for p in ("Conquests/", "civ3PTW/", "")),
    "ancient Greco-Roman music": tuple(f"{p}Sounds/build/ancient/AncGRFull.mp3" for p in ("Conquests/", "civ3PTW/", "")),
}


def find_casefold(root: Path, relative: str) -> Path | None:
    """Check Windows-style asset names even on case-sensitive APFS."""
    current = root
    for part in Path(relative).parts:
        if part in ("..", "/"):
            raise ValueError("Asset paths must stay within the installation")
        if not current.is_dir():
            return None
        exact = current / part
        if exact.exists():
            current = exact
            continue
        matches = [p for p in current.iterdir() if p.name.casefold() == part.casefold()]
        if len(matches) != 1:
            return None
        current = matches[0]
    return current if current.is_file() else None


def missing_assets(game: Path) -> list[str]:
    return [f"{label}: {paths[0]} (or its PTW/base counterpart)"
            for label, paths in REQUIRED_ASSETS.items()
            if not any(find_casefold(game, path) for path in paths)]


def validate_assets(game: Path) -> None:
    missing = missing_assets(game)
    if missing:
        raise RuntimeError("Incomplete game assets; repair from your own complete installation before building:\n  "
                           + "\n  ".join(missing)
                           + "\nThe supported executable alone is not a complete game installation.")


def fresh_settings(game: Path, work: Path) -> Path | None:
    """Back up private-copy preferences before starting with portable defaults."""
    ini = game / "Conquests/conquests.ini"
    work.mkdir(parents=True, exist_ok=True)
    backup = None
    if ini.exists():
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
        backup = work / f"conquests-settings-{stamp}.ini"
        shutil.copy2(ini, backup)
    ini.parent.mkdir(parents=True, exist_ok=True)
    ini.write_text(f"[Conquests]\nPlayIntro=0\nKeepRes=0\nVoice Quality=0\nRules={SAFE_DEFAULT_RULES:032b}\n", encoding="ascii")
    return backup
