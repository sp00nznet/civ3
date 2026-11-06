"""Remove only disposable game clones created by a new QA run.

Keep the new run's small logs, JSON verdicts and framebuffers, so failed
tests remain diagnosable without leaving gigabytes of duplicated game data.
"""
from __future__ import annotations

from pathlib import Path
import errno
import shutil
import time


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
            # Finder and file-provider metadata can recreate .DS_Store during
            # a large APFS clone removal; retry only transient ENOTEMPTY.
            for attempt in range(4):
                try:
                    shutil.rmtree(path)
                    break
                except OSError as error:
                    if error.errno != errno.ENOTEMPTY or attempt == 3:
                        raise
                    if path.is_symlink():
                        raise ValueError(f"Refusing a symlinked QA clone: {path}")
                    time.sleep(0.15 * (attempt + 1))
            removed.append(name)
        elif path.exists():
            raise ValueError(f"Expected a generated directory, got a file: {path}")
    return removed
