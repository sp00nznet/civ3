#!/usr/bin/env python3
"""Repair PCM loading and resident render counts in build-only sound sources."""
import argparse
import hashlib
from pathlib import Path
import re
import sys

SUPPORTED_SHA256 = "407de004ab8df5293a38a0f965959316053bb2022c4fe02bdf2a9b0449c53d6e"
MARKER = "0x100B4308: call dword ptr [0x10135688]"
CALL = f"RECOMP_ICALL(MEM32(0x10135688)); /* {MARKER} */"
COUNT_SITES = ("100B93B1", "100B93DA", "100B9416", "100B943C")
RENDER_CONTEXTS = (("100B939F", "100B93A1"), ("100B93C8", "100B93CA"),
                   ("100B9404", "100B9406"), ("100B942A", "100B942C"))
EXPECTED = {"stream_prime": 4, **dict.fromkeys(COUNT_SITES, 1),
            **dict.fromkeys((site for pair in RENDER_CONTEXTS for site in pair), 1),
            "resident_add8": 1}
ADD8_MARKER = "0x100B9434: call 0x1008225c"
ADD8_CALL = f"RECOMP_CALL(sub_1008225C); /* {ADD8_MARKER} */"


def transform(text):
    if "civ3_preserve_unread_pcm" in text or "civ3_rendered_count" in text:
        raise ValueError("Expected an unmodified sound lift")
    pattern = re.compile(r"^(?P<indent>[ \t]*)" + re.escape(CALL) + r"$", re.M)

    def replace(match):
        indent = match["indent"]
        return (indent + "/* civ3_preserve_unread_pcm: cache priming leaves four bytes unread. */\n" +
                indent + "if (MEM32(ebp + 0x28) == MEM32(ebp + 0x2C)) {\n" +
                indent + "    " + CALL + "\n" + indent + "} else {\n" +
                indent + "    esp += 12;\n" + indent + "}\n")

    modified, count = pattern.subn(replace, text)
    if text.count(MARKER) != count:
        raise ValueError("Sound stream instruction shape changed")
    counts = {"stream_prime": count}
    for address in COUNT_SITES:
        marker = f"0x{address}: mov dword ptr [ebx], eax"
        original = f"MEM32(ebx) = eax; /* {marker} */"
        found = modified.count(original)
        if modified.count(marker) != found:
            raise ValueError(f"Resident count instruction shape changed at {address}")
        modified = modified.replace(original,
            f"MEM32(esp + 0x18) = eax; /* civ3_rendered_count: {address} */")
        counts[address] = found
    for mode, context in RENDER_CONTEXTS:
        for address, before, after, instruction in (
                (mode, "1", "0", "push 1"), (context, "edi", "esi", "push edi")):
            marker = f"0x{address}: {instruction}"
            original = f"PUSH32(esp, {before}); /* {marker} */"
            found = modified.count(original)
            if modified.count(marker) != found:
                raise ValueError(f"Resident render context changed at {address}")
            modified = modified.replace(original,
                f"PUSH32(esp, {after}); /* civ3_rendered_count: voice context {address} */")
            counts[address] = found
    found = modified.count(ADD8_CALL)
    if modified.count(ADD8_MARKER) != found:
        raise ValueError("Resident additive 8-bit call shape changed")
    modified = modified.replace(ADD8_CALL,
        "RECOMP_CALL(sub_100826B7); /* civ3_rendered_count: add the 8-bit voice */")
    counts["resident_add8"] = found
    return modified, counts


def write_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


def quote(path):
    return '"' + str(path).replace('\\', '\\\\').replace('"', '\\"').replace(';', '\\;') + '"'


def stage(source, output, image):
    source, output = source.resolve(), output.resolve()
    if source == output or output.is_relative_to(source):
        raise ValueError("Sound overlay must be outside its input directory")
    if hashlib.sha256(image.read_bytes()).hexdigest() != SUPPORTED_SHA256:
        raise ValueError("Sound overlay requires the supported rebased sound DLL")
    pending, totals = [], dict.fromkeys(EXPECTED, 0)
    for path in sorted(source.glob("recomp_[0-9]*.c")):
        modified, counts = transform(path.read_text())
        for site, count in counts.items():
            totals[site] += count
        if any(counts.values()):
            pending.append((path, output / path.name, modified))
    if totals != EXPECTED:
        raise ValueError(f"Sound repair sites changed: found {totals}; expected {EXPECTED}")
    cmake = []
    for original, destination, text in pending:
        write_changed(destination, text)
        cmake += [f"list(REMOVE_ITEM DLL_SOURCES {quote(original)})",
                  f"list(APPEND DLL_SOURCES {quote(destination)})",
                  f"set_source_files_properties({quote(destination)} PROPERTIES INCLUDE_DIRECTORIES {quote(source)})"]
    write_changed(output / "sources.cmake", "\n".join(cmake) + "\n")
    return totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "output", "image"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        print("Sound stream overlay:", stage(args.source, args.output, args.image))
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
