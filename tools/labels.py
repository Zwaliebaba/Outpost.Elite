#!/usr/bin/env python3
"""Turn a BeebAsm assembly into the two files the oracle fixture reads.

BeebAsm's -labels dump is a single line of Python-2 dict syntax with trailing "L" on every
integer, and the binary load addresses are only available as PRINT lines in the verbose log.
Neither is something a C++ test should be parsing, so this normalises both into plain
tab-separated text:

    Design/Reference/Labels.txt      NAME <tab> ADDRESS      (decimal, one per line, sorted)
    Design/Reference/Binaries.txt    FILE <tab> ADDRESS      (where each .bin loads)

Regenerate after any change to the upstream tree or the build options.  Run from the
repository root:

    python tools/labels.py                 # parse the existing capture
    python tools/labels.py --assemble      # run BeebAsm first, then parse

BeebAsm is not vendored (it is GPL, and it is a tool rather than a library).  --assemble looks
for it at Tools-ext/beebasm/out/beebasm.exe; build it there from github.com/stardot/beebasm.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UPSTREAM = REPO / "Upstream" / "elite-source-code-library"
REFERENCE = REPO / "Design" / "Reference"
BEEBASM = REPO / "Tools-ext" / "beebasm" / "out" / "beebasm.exe"

# Two assemblies, because the game and its data are built separately and the oracle needs both:
# the code blocks hold the routines and the logarithm tables, while the data build holds the
# token tables, the sine and arctangent tables, the font and the ship blueprints.
ASSEMBLIES = [
    ("source", "versions/c64/1-source-files/main-sources/elite-source.asm"),
    ("data", "versions/c64/1-source-files/main-sources/elite-data.asm"),
]

LABELS_OUT = REFERENCE / "Labels.txt"
BINARIES_OUT = REFERENCE / "Binaries.txt"

# 'NAME':1234L  --  the trailing L is Python 2 long syntax and is not always present.
LABEL_RE = re.compile(r"'([^']+)'\s*:\s*(\d+)L?")

# The load address is on the S.<name> line and the actual filename on the line after it, and
# the two do not agree ("S.C.WORDS" saves "WORDS.bin"), so they are read as a pair rather than
# by guessing a filename from the printed name.
SAVE_RE = re.compile(r"^S\.\S+\s+&([0-9A-Fa-f]+)\s+&[0-9A-Fa-f]+.*\r?\n\s*Saving file '([^']+)'", re.MULTILINE)


def raw_labels_path(_which: str) -> Path:
    return REFERENCE / f"labels-{_which}.txt"


def compile_log_path(_which: str) -> Path:
    return REFERENCE / f"compile-{_which}.txt"


def assemble() -> int:
    if not BEEBASM.is_file():
        print(f"error: BeebAsm not found at {BEEBASM}")
        print("       Clone github.com/stardot/beebasm and build it there, then retry.")
        return 1
    if not UPSTREAM.is_dir():
        print(f"error: upstream tree not found at {UPSTREAM}")
        return 1

    REFERENCE.mkdir(parents=True, exist_ok=True)

    for which, source in ASSEMBLIES:
        command = [str(BEEBASM), "-i", source, "-v", "-d", "-labels", str(raw_labels_path(which))]
        print(f"assembling {which}: {source}")
        completed = subprocess.run(command, cwd=UPSTREAM, capture_output=True, text=True)
        compile_log_path(which).write_text(completed.stdout + completed.stderr, encoding="utf-8")
        if completed.returncode != 0:
            print(f"FAIL  BeebAsm exited {completed.returncode}; see {compile_log_path(which)}")
            return completed.returncode

    print("OK    both assemblies written")
    return 0


def parse_labels() -> dict[str, int]:
    """Labels from both assemblies. The code build wins a collision, since that is the map the
    running game uses; disagreements are reported rather than silently resolved."""
    merged: dict[str, int] = {}
    collisions: list[str] = []

    for which, _source in ASSEMBLIES:
        path = raw_labels_path(which)
        if not path.is_file():
            sys.exit(f"error: {path} not found -- run with --assemble first")
        found = {name: int(value) for name, value in LABEL_RE.findall(path.read_text(encoding="utf-8", errors="replace"))}
        if not found:
            sys.exit(f"error: no labels parsed from {path}; has the dump format changed?")
        for name, value in found.items():
            if name in merged and merged[name] != value:
                collisions.append(f"{name}: {merged[name]} kept, {which} said {value}")
                continue
            merged.setdefault(name, value)

    if collisions:
        print(f"note      {len(collisions)} label(s) differ between the two assemblies; the code build wins")
        for line in collisions[:8]:
            print(f"  {line}")

    return merged


def parse_binaries() -> list[tuple[str, int]]:
    """Every block either assembly saved, as (filename, load address)."""
    found: list[tuple[str, int]] = []

    for which, _source in ASSEMBLIES:
        path = compile_log_path(which)
        if not path.is_file():
            sys.exit(f"error: {path} not found -- run with --assemble first")
        text = path.read_text(encoding="utf-8", errors="replace")
        for start, filename in SAVE_RE.findall(text):
            found.append((Path(filename).name, int(start, 16)))

    if not found:
        sys.exit("error: no saved blocks found in the compile logs; did the assembly actually run?")

    # WORDS and IANTOK sit inside LODATA, which the data build saves as one span. Loading the
    # larger block alone is the same bytes with fewer overlapping writes to reason about.
    contained = {"WORDS.bin", "IANTOK.bin"}
    if any(name == "LODATA.bin" for name, _address in found):
        found = [(name, address) for name, address in found if name not in contained]

    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--assemble", action="store_true", help="run BeebAsm before parsing")
    args = parser.parse_args()

    if args.assemble:
        code = assemble()
        if code != 0:
            return code

    labels = parse_labels()
    binaries = parse_binaries()

    REFERENCE.mkdir(parents=True, exist_ok=True)

    with LABELS_OUT.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Generated by tools/labels.py -- do not edit.\n")
        handle.write("# Label addresses from the assembled Commodore 64 build. NAME<tab>ADDRESS.\n")
        for name in sorted(labels):
            handle.write(f"{name}\t{labels[name]}\n")

    with BINARIES_OUT.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Generated by tools/labels.py -- do not edit.\n")
        handle.write("# Where each assembled block loads. FILE<tab>ADDRESS.\n")
        for name, start in binaries:
            handle.write(f"{name}\t{start}\n")

    print(f"labels    {len(labels)} -> {LABELS_OUT.relative_to(REPO)}")
    print(f"binaries  {len(binaries)} -> {BINARIES_OUT.relative_to(REPO)}")
    lowest = min(start for _name, start in binaries)
    highest = max(start for _name, start in binaries)
    print(f"span      first block loads at {lowest:#06x}, last at {highest:#06x}")

    for probe in ("DORND", "MULTU", "MULT1", "FMLTU", "ADD", "ARCTAN", "SNE", "ACT", "QQ18", "TKN1", "XX21", "log"):
        if probe not in labels:
            print(f"WARN  expected label {probe} is missing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
