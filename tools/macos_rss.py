"""Bounded macOS resident-memory sampling of one owned subprocess.

Sampling observes resident size, not true allocations. Legitimate game assets
and caches may grow during play; a rising RSS value does NOT prove a memory leak.
"""
from __future__ import annotations

import subprocess
import time
from pathlib import Path
from typing import Callable


def read_rss_kib(pid: int) -> int | None:
    try:
        result = subprocess.run(["/bin/ps", "-o", "rss=", "-p", str(pid)],
                                capture_output=True, text=True, timeout=2, check=False)
        if result.returncode != 0:
            return None
        value = result.stdout.strip()
        kib = int(value)
        return kib if kib >= 0 else None
    except (ValueError, OSError, subprocess.TimeoutExpired):
        return None


def run_with_rss(command: list[str], cwd: Path, log: Path,
                 timeout: float, sample_interval: float = 2.5,
                 measure: Callable[[int], int | None] = read_rss_kib,
                 env: dict[str, str] | None = None
                 ) -> tuple[int, list[dict[str, float | int]]]:
    if not command or timeout <= 0 or sample_interval <= 0:
        raise ValueError("Command and positive time bounds required")
    samples: list[dict[str, float | int]] = []
    with log.open("x") as sink:
        child = subprocess.Popen(command, cwd=cwd, env=env, stdout=sink,
                                 stderr=subprocess.STDOUT)
        start = time.monotonic()
        try:
            while child.poll() is None:
                elapsed = time.monotonic() - start
                if elapsed > timeout:
                    raise subprocess.TimeoutExpired(command, timeout)
                kib = measure(child.pid)
                if kib is not None:
                    samples.append({"seconds": round(elapsed, 3), "rss_kib": kib})
                try:
                    child.wait(timeout=sample_interval)
                except subprocess.TimeoutExpired:
                    pass  # Expected: the monitored game is still running.
        except subprocess.TimeoutExpired:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            raise
        except BaseException:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()
            raise
        return child.returncode, samples


def summarize_rss(samples: list[dict[str, float | int]]) -> dict:
    if len(samples) < 2:
        return {"sample_count": len(samples), "warning": "Insufficient RSS observations"}
    first = int(samples[0]["rss_kib"])
    last = int(samples[-1]["rss_kib"])
    peak = max(int(s["rss_kib"]) for s in samples)
    after_startup = [s for s in samples if float(s["seconds"]) >= 25]
    first_settled = int(after_startup[0]["rss_kib"]) if after_startup else first
    return {
        "sample_count": len(samples),
        "initial_rss_kib": first,
        "after_startup_rss_kib": first_settled,
        "final_rss_kib": last,
        "peak_rss_kib": peak,
        "growth_from_startup_kib": last - first_settled,
        "note": "RSS includes mapped code, cache and game assets; growth alone is not a leak.",
    }
