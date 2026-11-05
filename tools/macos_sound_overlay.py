#!/usr/bin/env python3
"""Preserve unread PCM when the lifted sound loader primes its stream cache."""
import argparse
import hashlib
from pathlib import Path
import re
import sys

SUPPORTED_SHA256 = "407de004ab8df5293a38a0f965959316053bb2022c4fe02bdf2a9b0449c53d6e"
MARKER = "0x100B4308: call dword ptr [0x10135688]"
CALL = f"RECOMP_ICALL(MEM32(0x10135688)); /* {MARKER} */"


def transform(text):
    if "civ3_preserve_unread_pcm" in text:
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
    return modified, count


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
    pending, total = [], 0
    for path in sorted(source.glob("recomp_[0-9]*.c")):
        modified, count = transform(path.read_text())
        total += count
        if count:
            pending.append((path, output / path.name, modified))
    if total != 4:
        raise ValueError(f"Expected four aliased stream-loader calls, found {total}")
    cmake = []
    for original, destination, text in pending:
        write_changed(destination, text)
        cmake += [f"list(REMOVE_ITEM DLL_SOURCES {quote(original)})",
                  f"list(APPEND DLL_SOURCES {quote(destination)})",
                  f"set_source_files_properties({quote(destination)} PROPERTIES INCLUDE_DIRECTORIES {quote(source)})"]
    write_changed(output / "sources.cmake", "\n".join(cmake) + "\n")
    return total


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
