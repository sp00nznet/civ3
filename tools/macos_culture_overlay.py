#!/usr/bin/env python3
"""Apply the two culture-start load repairs to build-only copies of a full lift."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

SUPPORTED_SHA1 = "01f705b1ce6f5d1cfd5a861f88ca1413764f8019"
# Aliased entrypoints duplicate instructions in a full lift of the pinned image.
SITES = {"005AE71C": ("005AE798", 1, 4), "005AEA25": ("005AEAA1", 0, 5)}


def transform(text: str) -> tuple[str, dict[str, int]]:
    counts = {}
    if 'civ3_generation_race(' in text:
        raise ValueError("Input is already modified; regenerate or restore the original lift")
    for address, (skip, scenario, _) in SITES.items():
        marker = f"0x{address}: mov ecx, dword ptr [eax]"
        count = text.count(marker)
        pattern = re.compile(r"^(?P<indent>[ \t]*)ecx = MEM32\(eax\); /\* " + re.escape(marker) + r" \*/$", re.M)
        def replace(match: re.Match) -> str:
            indent = match["indent"]
            return (match[0] + "\n" + indent +
                    f"ecx = civ3_generation_race(esp, ebp, ecx, {scenario});\n" + indent +
                    f"if (ecx == UINT32_MAX) goto L_{skip}; /* no assigned race to compare */")
        text, replaced = pattern.subn(replace, text)
        if count != replaced:
            raise ValueError(f"Instruction shape changed at {address}; refusing a partial repair")
        counts[address] = replaced
    if any(counts.values()):
        anchor = '#include "recomp_types.h"'
        if text.count(anchor) != 1:
            raise ValueError("Expected exactly one runtime-header include in a generated chunk")
        text = text.replace(anchor, anchor + '\n#include "culture_start_fix.h"')
    return text, counts


def write_changed(path: Path, text: str) -> None:
    if path.is_file() and path.read_text() == text:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(text)
    temp.replace(path)


def quote(path: Path) -> str:
    return '"' + str(path).replace('\\', '\\\\').replace('"', '\\"').replace(';', '\\;') + '"'


def stage(source: Path, output: Path, executable: Path) -> dict:
    if hashlib.sha1(executable.read_bytes()).hexdigest() != SUPPORTED_SHA1:
        raise ValueError("Culture repair requires the exact supported Steam executable")
    source, output = source.resolve(), output.resolve()
    if source == output or output.is_relative_to(source):
        raise ValueError("Build overlay must be outside the generated input directory")
    files = sorted(source.glob("recomp_[0-9]*.c"))
    identity = {"source": str(source), "executable_sha1": SUPPORTED_SHA1,
                "transform_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "files": {str(p): [p.stat().st_size, p.stat().st_mtime_ns] for p in files}}
    cache_path = output / "cache.json"
    try:
        cache = json.loads(cache_path.read_text())
        if cache["inputs"] == identity and cache["outputs"] and all(
                hashlib.sha256((output / name).read_bytes()).hexdigest() == sha
                for name, sha in cache["outputs"].items()):
            return json.loads((output / "manifest.json").read_text())
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        pass  # Missing, changed or corrupted cache: validate the entire lift.
    totals = dict.fromkeys(SITES, 0)
    replacements = []
    pending = []
    for file in files:
        text = file.read_text()
        modified, counts = transform(text)
        for address, count in counts.items(): totals[address] += count
        if any(counts.values()):
            destination = output / file.name
            pending.append((destination, modified))
            replacements.append({"source": str(file), "output": str(destination), "counts": counts,
                                 "source_sha256": hashlib.sha256(text.encode()).hexdigest()})
    expected = {address: entry[2] for address, entry in SITES.items()}
    if totals != expected:
        raise ValueError(f"Incomplete or changed full lift: found {totals}; expected {expected}")
    # Validate the entire transformation before writing any generated overlay.
    for path, text in pending: write_changed(path, text)
    manifest = {"sites": totals, "replacements": replacements}
    write_changed(output / "manifest.json", json.dumps(manifest, indent=2) + "\n")
    cmake = ["# Generated; original game-derived sources stay untouched."]
    for item in replacements:
        original, patched = Path(item["source"]), Path(item["output"])
        includes = str(source) + ";" + str(source.parents[1] / 'linux')
        includes = includes.replace('\\', '\\\\').replace('"', '\\"')
        cmake += [f"list(REMOVE_ITEM GEN_SOURCES {quote(original)})",
                  f"list(APPEND GEN_SOURCES {quote(patched)})",
                  f'set_source_files_properties({quote(patched)} PROPERTIES INCLUDE_DIRECTORIES "{includes}")']
    write_changed(output / "sources.cmake", "\n".join(cmake) + "\n")
    products = [p for p, _ in pending] + [output / "manifest.json", output / "sources.cmake"]
    cache = {"inputs": identity, "outputs": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in products}}
    write_changed(cache_path, json.dumps(cache, indent=2) + "\n")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = stage(args.source, args.output, args.exe)
    except (OSError, ValueError) as error:
        print(f"Culture overlay failed: {error}", file=sys.stderr)
        return 1
    print("Culture-start overlay:", result["sites"], flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
