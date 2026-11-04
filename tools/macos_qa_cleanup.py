"""Remove only disposable game clones created by a new QA run.

Keep the new run's small logs, JSON verdicts and framebuffers, so failed
tests remain diagnosable without leaving gigabytes of duplicated game data.
"""
from __future__ import annotations

from pathlib import Path
import shutil


def remove_clones(run_directory: Path, names: tuple[str, ...] | list[str]) -> list[str]:
    if run_directory.is_symlink() or not run_directory.is_dir():
        raise ValueError("QA output must be a real directory, not a symlink")
    removed: list[str] = []
    for name in names:
        if not name or name in {".", ".."} or Path(name).name != name or "\\" in name:
            raise ValueError(f"Invalid generated clone name: {name!r}")
        path = run_directory / name
        if path.is_symlink():
            # Never follow a link to a real user installation.
            raise ValueError(f"Refusing a symlinked QA clone: {path}")
        if path.is_dir():
            shutil.rmtree(path)
            removed.append(name)
        elif path.exists():
            raise ValueError(f"Expected a generated directory, got a file: {path}")
    return removed
