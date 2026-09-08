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
for it at Tools-ext/beebasm/out/, under either the MSVC name (beebasm.exe) or the one every
other toolchain produces (beebasm); build it there from github.com/stardot/beebasm.
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
# BeebAsm's own build produces `beebasm.exe` under MSVC and `beebasm` everywhere else, and both
# are wanted: the Windows CI job builds it with cl, the Linux one with g++, and they assemble the
# same game from the same pinned commit. Neither name is preferred -- the first that exists wins.
BEEBASM_CANDIDATES = [
    REPO / "Tools-ext" / "beebasm" / "out" / "beebasm.exe",
    REPO / "Tools-ext" / "beebasm" / "out" / "beebasm",
]


# The generated header (Design/Modernize.md §1 R-g, slice M6-b-2).
#
# The tests have to find their routines by name after M6-f deletes `Upstream/`, so the ~1,900
# addresses become a committed C++ header rather than a file read at run time. It carries NAMES AND
# NUMBERS ONLY -- the assembled image is the original's code and stays out of the tree, which is
# what keeps ADR-001 §5 true through the phase.
HEADER_OUT = REPO / "Tests" / "GameLogicTests" / "OracleLabels.h"


def find_beebasm() -> Path | None:
    for candidate in BEEBASM_CANDIDATES:
        if candidate.is_file():
            return candidate
    return None

# Two assemblies, because the game and its data are built separately and the oracle needs both:
# the code blocks hold the routines and the logarithm tables, while the data build holds the
# token tables, the sine and arctangent tables, the font and the ship blueprints.
ASSEMBLIES = [
    ("source", "versions/c64/1-source-files/main-sources/elite-source.asm"),
    ("data", "versions/c64/1-source-files/main-sources/elite-data.asm"),
]

# And a third, which is assembled and then deliberately KEPT OUT of the oracle image.
#
# The loader holds two things the game reads and no assembly of the game produces: `sdump` and
# `cdump`, the screen and colour RAM the dashboard is coloured through. Its code is not ported
# (the plan's inventory says so of every one of its seven parts), but its DATA is needed, and the
# only honest place to get it is the same assembler that builds everything else.
#
# It is a separate pair of reference files rather than two more rows in `Labels.txt` and
# `Binaries.txt` because of where it loads: `CODE% = &4000`, which is the game's SCREEN BITMAP.
# Adding COMLOD to the oracle image would start every drawing test on a screen full of loader
# code, and the golden comparisons -- which read the oracle's own bitmap -- would be comparing
# against that. So the loader gets its own image, which only the table extractor and the table
# test ever load.
LOADER_ASSEMBLY = ("loader", "versions/c64/1-source-files/main-sources/elite-loader.asm")

# And a FOURTH, kept out of the oracle image for the same reason and a different address.
#
# `elite-sprites.asm` is 84 lines and produces 448 bytes: `spritp`, the seven sprite definitions
# -- four laser sights, the explosion cloud, and two Trumbles facing opposite ways. The game never
# assembles them; the LOADER copies them into place at `SPRITELOC%`, which is `SCBASE + &2800` and
# therefore one byte past the end of everything `Canvas` models.
#
# It gets its own reference pair because `CODE% = &7C3A`, which is nowhere near either of the
# other two images and has no business in the oracle. What it buys is that the seven definitions
# become a generated table `extract_tables.py --check` compares byte for byte against the
# assembler's own output -- which is the ONE part of the sprite overlay that can be verified
# against the original at all (ADR-005 section 1).
SPRITE_ASSEMBLY = ("sprites", "versions/c64/1-source-files/main-sources/elite-sprites.asm")

# Where the oracle loads blocks from, and what `Binaries.txt`'s first column is relative to.
ASSEMBLED = UPSTREAM / "versions" / "c64" / "3-assembled-output"

LABELS_OUT = REFERENCE / "Labels.txt"
BINARIES_OUT = REFERENCE / "Binaries.txt"

# The same two tables for the loader, read by tools/extract_tables.py.
LOADER_LABELS_OUT = REFERENCE / "LoaderLabels.txt"
LOADER_BINARIES_OUT = REFERENCE / "LoaderBinaries.txt"

# The same again for the sprite definitions.
SPRITE_LABELS_OUT = REFERENCE / "SpriteLabels.txt"
SPRITE_BINARIES_OUT = REFERENCE / "SpriteBinaries.txt"

# 'NAME':1234L  --  the trailing L is Python 2 long syntax and is not always present.
LABEL_RE = re.compile(r"'([^']+)'\s*:\s*(\d+)L?")

# The load address is on the S.<name> line and the actual filename on the line after it, and
# the two do not agree ("S.C.WORDS" saves "WORDS.bin"), so they are read as a pair rather than
# by guessing a filename from the printed name.
#
# THE SECOND ADDRESS IS OPTIONAL, because the four masters do not all print the same thing. Most
# print start, end, exec and reload -- "S.C.COMLOD &4000  &865B  &4000  &4000" -- while
# `elite-sprites.asm` prints the start and then a literal length: "S.C.SPRITE &7C3A  +1C0".
# Requiring two addresses matched nothing at all for that one, and the miss surfaced three steps
# later as "no saved blocks found in the compile logs", which names neither the file nor the line.
SAVE_RE = re.compile(r"^S\.\S+\s+&([0-9A-Fa-f]+)(?:\s+&[0-9A-Fa-f]+)?.*\r?\n\s*Saving file '([^']+)'", re.MULTILINE)


def raw_labels_path(_which: str) -> Path:
    return REFERENCE / f"labels-{_which}.txt"


def compile_log_path(_which: str) -> Path:
    return REFERENCE / f"compile-{_which}.txt"


def assemble() -> int:
    beebasm = find_beebasm()
    if beebasm is None:
        print("error: BeebAsm not found. Looked for:")
        for candidate in BEEBASM_CANDIDATES:
            print(f"       {candidate}")
        print("       Clone github.com/stardot/beebasm and build it there, then retry.")
        return 1
    if not UPSTREAM.is_dir():
        print(f"error: upstream tree not found at {UPSTREAM}")
        return 1

    REFERENCE.mkdir(parents=True, exist_ok=True)

    for which, source in ASSEMBLIES + [LOADER_ASSEMBLY, SPRITE_ASSEMBLY]:
        command = [str(beebasm), "-i", source, "-v", "-d", "-labels", str(raw_labels_path(which))]
        print(f"assembling {which}: {source}")
        completed = subprocess.run(command, cwd=UPSTREAM, capture_output=True, text=True)
        compile_log_path(which).write_text(completed.stdout + completed.stderr, encoding="utf-8")
        if completed.returncode != 0:
            print(f"FAIL  BeebAsm exited {completed.returncode}; see {compile_log_path(which)}")
            return completed.returncode

    print("OK    all four assemblies written")
    return 0


def parse_labels(_assemblies: list[tuple[str, str]]) -> dict[str, int]:
    """Labels from the given assemblies. The code build wins a collision, since that is the map
    the running game uses; disagreements are reported rather than silently resolved."""
    merged: dict[str, int] = {}
    collisions: list[str] = []

    for which, _source in _assemblies:
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


def parse_binaries(_assemblies: list[tuple[str, str]], _runtime: bool) -> list[tuple[str, int]]:
    """Every block the given assemblies saved, as (filename, load address)."""
    found: list[tuple[str, int]] = []

    for which, _source in _assemblies:
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

    return found + (loaded_at_runtime() if _runtime else [])


# ---- blocks the assembly does not produce ------------------------------------------------------
#
# The dashboard bitmap. `wantdials` copies 2,240 bytes from `DSTORE%` into `DLOC%`, and nothing
# the modern build assembles goes anywhere near `DSTORE%`: the image ships inside `COMLOD.bin` and
# the C64's own loader puts it there at run time. Without this row the oracle's memory at
# `DSTORE%` is zero, and a comparison of `wantdials` copies blanks on both sides, agrees, and
# proves nothing (plan §6.78).
#
# `DSTORE% = SCBASE + &AF90` with `SCBASE = &4000`, from elite-source.asm under `_GMA_RELEASE`,
# which is the variant this build is.
#
# AND THE FILE GOES &18 BYTES ABOVE IT, which is what the original build script says and what
# §6.78 talked itself out of: `S.COMLODS.txt` line 1000 is `OSCLI("L.:2.C.CODIALS "+STR$~(O%+&18))`.
# The reasoning that dropped the &18 was that the file's first byte "renders as a dashboard with
# its labels aligned to the cell grid" -- and it does, because three cells of shift still looks
# like a dashboard when there is nothing to line it up against. The COLOUR MAP is the thing to
# line it up against, and it is unambiguous: with the file at `DSTORE%` 18.5% of the picture's lit
# pixels come out black, and at `DSTORE% + &18` that falls to 8.4%, the lowest of any shift from
# -8 to +8 cells. At that offset the labels sit in their boxes -- FS, AS, FU, CT, LT and AL down
# the left and SP, RL, DC and 1 to 4 down the right -- and the three cells either side that the
# offset costs are the ones `sdump` colours black on black, so nothing is lost that could be seen.
# The file is 2,240 bytes of picture and eight of padding, so the copy's last 24 bytes fall off
# its end into cells 37 to 39 of the bottom row, which are black as well (plan §6.105).
RUNTIME_BLOCKS = [
    ("../1-source-files/images/C.CODIALS.bin", 0x4000 + 0xAF90 + 0x18, 2248),
]


def loaded_at_runtime() -> list[tuple[str, int]]:
    """Blocks the game's loader places, which no assembly step saves."""
    rows: list[tuple[str, int]] = []
    for name, address, expected in RUNTIME_BLOCKS:
        path = ASSEMBLED / name
        if not path.is_file():
            sys.exit(f"error: {path} not found -- the upstream submodule may not be checked out")
        size = path.stat().st_size
        if size != expected:
            sys.exit(f"error: {name} is {size} bytes, expected {expected} -- the load address "
                     f"in RUNTIME_BLOCKS was derived for the expected size and needs rechecking")
        rows.append((name, address))
    return rows


def write_labels(_path: Path, _labels: dict[str, int]) -> None:
    with _path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Generated by tools/labels.py -- do not edit.\n")
        handle.write("# Label addresses from the assembled Commodore 64 build. NAME<tab>ADDRESS.\n")
        for name in sorted(_labels):
            handle.write(f"{name}\t{_labels[name]}\n")


def write_binaries(_path: Path, _binaries: list[tuple[str, int]]) -> None:
    with _path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("# Generated by tools/labels.py -- do not edit.\n")
        handle.write("# Where each assembled block loads. FILE<tab>ADDRESS.\n")
        for name, start in _binaries:
            handle.write(f"{name}\t{start}\n")


def read_table(_path: Path) -> dict[str, int]:
    """A `NAME<tab>ADDRESS` file back into a dict. Missing is empty, which the caller reports."""
    rows: dict[str, int] = {}
    if not _path.is_file():
        return rows
    for line in _path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        name, _, address = line.partition("\t")
        if address:
            rows[name] = int(address)
    return rows


def header_text(_tables: list[tuple[str, dict[str, int]]]) -> str:
    """The generated header, from the three label tables.

    Sorted by name and written a line at a time, so regenerating on another machine produces the
    same bytes -- the same property the fixture file has, and for the same reason.

    An address above &FFFF is dropped rather than truncated: BeebAsm emits a few constants that are
    not addresses at all, and `OracleImage` has always filtered them the same way.
    """
    out = [
        "#pragma once",
        "",
        "// Generated by `python tools/labels.py --header` -- do not edit.",
        "//",
        "// Where each routine of the assembled Commodore 64 build lives, so a test can call one by",
        "// name (Design/Modernize.md §1 R-g). Names and addresses only: the image itself is the",
        "// original's code and never enters the tree.",
        "//",
        "// `python tools/labels.py --check` fails if this file and the assembler disagree.",
        "",
        "#include <cstdint>",
        "",
        "namespace Elite::Testing::Labels",
        "{",
        "",
        "  struct Entry",
        "  {",
        "    const char* name;",
        "    std::uint16_t address;",
        "  };",
        "",
    ]
    for symbol, table in _tables:
        out.append(f"  inline constexpr Entry {symbol}[] = {{")
        for name in sorted(table):
            address = table[name]
            if address <= 0xFFFF:
                out.append(f'    {{"{name}", 0x{address:04X}u}},')
        out.append("  };")
        out.append("")
    out.append("} // namespace Elite::Testing::Labels")
    out.append("")
    return "\n".join(out)


def label_tables() -> list[tuple[str, dict[str, int]]]:
    """The three tables the header carries, read back from what `--assemble` wrote."""
    return [
        ("GAME", read_table(LABELS_OUT)),
        ("LOADER", read_table(LOADER_LABELS_OUT)),
        ("SPRITES", read_table(SPRITE_LABELS_OUT)),
    ]


def check_header() -> int:
    """Does the committed header still say what the assembler says?

    SKIPPED, NOT FAILED, when the reference tables are absent. That is the normal state of a fresh
    clone and it is the PERMANENT state after M6-f: the check exists to catch a header that has
    drifted from an original that is still here, not to demand one that has gone.
    """
    tables = label_tables()
    if not any(table for _symbol, table in tables):
        print("SKIP  no Design/Reference label tables -- nothing to check the header against")
        return 0

    if not HEADER_OUT.is_file():
        print(f"FAIL  {HEADER_OUT.relative_to(REPO)} is missing -- run: python tools/labels.py --header")
        return 1

    wanted = header_text(tables)
    found = HEADER_OUT.read_text(encoding="utf-8")
    if found == wanted:
        total = sum(len([1 for address in table.values() if address <= 0xFFFF]) for _symbol, table in tables)
        print(f"OK    {HEADER_OUT.relative_to(REPO)} matches the assembler: {total} labels")
        return 0

    wantedLines = wanted.splitlines()
    foundLines = found.splitlines()
    print(f"FAIL  {HEADER_OUT.relative_to(REPO)} disagrees with the assembler "
          f"({len(foundLines)} lines on disk, {len(wantedLines)} from the tables)")
    shown = 0
    for at in range(max(len(wantedLines), len(foundLines))):
        here = foundLines[at] if at < len(foundLines) else "(end of file)"
        there = wantedLines[at] if at < len(wantedLines) else "(end of file)"
        if here != there:
            print(f"      line {at + 1}: on disk {here.strip()!r}, from the tables {there.strip()!r}")
            shown += 1
            if shown == 5:
                print("      ...")
                break
    print("      run: python tools/labels.py --header")
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--assemble", action="store_true", help="run BeebAsm before parsing")
    parser.add_argument("--header", action="store_true",
                        help="write Tests/GameLogicTests/OracleLabels.h from the parsed tables and stop")
    parser.add_argument("--check", action="store_true",
                        help="fail if that header disagrees with the tables; skip if they are absent")
    args = parser.parse_args()

    if args.check:
        return check_header()

    if args.header:
        tables = label_tables()
        if not any(table for _symbol, table in tables):
            print("ERROR no Design/Reference label tables -- run: python tools/labels.py --assemble")
            return 2
        HEADER_OUT.parent.mkdir(parents=True, exist_ok=True)
        HEADER_OUT.write_text(header_text(tables), encoding="utf-8", newline="\n")
        for symbol, table in tables:
            kept = len([1 for address in table.values() if address <= 0xFFFF])
            print(f"{symbol.lower():9s} {kept} labels")
        print(f"header    -> {HEADER_OUT.relative_to(REPO)}")
        return 0

    if args.assemble:
        code = assemble()
        if code != 0:
            return code

    labels = parse_labels(ASSEMBLIES)
    binaries = parse_binaries(ASSEMBLIES, _runtime=True)

    # The loader's own map, kept apart for the reason LOADER_ASSEMBLY gives: it loads over the
    # screen bitmap, so it must never join the image the oracle tests run against.
    loaderLabels = parse_labels([LOADER_ASSEMBLY])
    loaderBinaries = parse_binaries([LOADER_ASSEMBLY], _runtime=False)

    # And the sprite definitions, apart for the same reason: CODE% = &7C3A.
    spriteLabels = parse_labels([SPRITE_ASSEMBLY])
    spriteBinaries = parse_binaries([SPRITE_ASSEMBLY], _runtime=False)

    REFERENCE.mkdir(parents=True, exist_ok=True)

    write_labels(LABELS_OUT, labels)
    write_binaries(BINARIES_OUT, binaries)
    write_labels(LOADER_LABELS_OUT, loaderLabels)
    write_binaries(LOADER_BINARIES_OUT, loaderBinaries)

    write_labels(SPRITE_LABELS_OUT, spriteLabels)
    write_binaries(SPRITE_BINARIES_OUT, spriteBinaries)

    print(f"labels    {len(labels)} -> {LABELS_OUT.relative_to(REPO)}")
    print(f"binaries  {len(binaries)} -> {BINARIES_OUT.relative_to(REPO)}")
    print(f"loader    {len(loaderLabels)} labels, {len(loaderBinaries)} block(s) -> "
          f"{LOADER_LABELS_OUT.relative_to(REPO)}")
    print(f"sprites   {len(spriteLabels)} labels, {len(spriteBinaries)} block(s) -> "
          f"{SPRITE_LABELS_OUT.relative_to(REPO)}")
    lowest = min(start for _name, start in binaries)
    highest = max(start for _name, start in binaries)
    print(f"span      first block loads at {lowest:#06x}, last at {highest:#06x}")

    for probe in ("DORND", "MULTU", "MULT1", "FMLTU", "ADD", "ARCTAN", "SNE", "ACT", "QQ18", "TKN1", "XX21", "log"):
        if probe not in labels:
            print(f"WARN  expected label {probe} is missing")
    for probe in ("sdump", "cdump"):
        if probe not in loaderLabels:
            print(f"WARN  expected loader label {probe} is missing")
    if "spritp" not in spriteLabels:
        print("WARN  expected sprite label spritp is missing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
