#!/usr/bin/env python3
"""Generate the port's lookup tables from the assembled game (slice 1a, ADR-004 section 3).

The tables are extracted from the ASSEMBLED BINARIES by label and length, not by parsing the
assembler source. That is a deliberate choice and it is the cheaper one twice over: the source
builds these tables with assembly-time loops and macros, so parsing it would mean writing a
second assembler, and the bytes that actually matter are the ones the game ships with. Reading
them out of the loaded image is both simpler and closer to the truth.

It is also self-checking. `TableTests.cpp` compares every generated array against the same
address range in the oracle's image, so a stale or hand-edited table cannot pass quietly.

    python tools/extract_tables.py            # regenerate the .cpp files
    python tools/extract_tables.py --check     # verify they match without rewriting them

Needs Design/Reference/Labels.txt and Binaries.txt, and the assembled .bin files. Run
`python tools/labels.py --assemble` first if they are missing.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REFERENCE = REPO / "Design" / "Reference"
OUTPUT = REPO / "GameLogic"
ASSEMBLED = REPO / "Upstream" / "elite-source-code-library" / "versions" / "c64" / "3-assembled-output"

LABELS = REFERENCE / "Labels.txt"
BINARIES = REFERENCE / "Binaries.txt"


class Table:
    def __init__(self, _identifier: str, _label: str, _length: int, _file: str, _summary: str):
        self.identifier = _identifier
        self.label = _label
        self.length = _length
        self.file = _file
        self.summary = _summary


# What to extract. One row per array the port needs; several rows may share an output file.
TABLES = [
    Table("LOG_TABLE", "log", 256, "LogTables.cpp", "high byte of the logarithm of the index"),
    Table("LOG_LOW_TABLE", "logL", 256, "LogTables.cpp", "low byte of the same logarithm"),
    Table("ANTILOG_TABLE", "antilog", 256, "LogTables.cpp", "the inverse, for even results"),
    Table("ANTILOG_ODD_TABLE", "antilogODD", 256, "LogTables.cpp", "the inverse, for odd results"),
    Table("SINE_TABLE", "SNE", 32, "SineTable.cpp", "a quarter turn of sine, scaled to a byte"),
    Table("ARCTAN_TABLE", "ACT", 32, "ArctanTable.cpp", "arctangent, indexed by a ratio"),
    Table("RECURSIVE_TOKEN_TABLE", "QQ18", 960, "TokenTables.cpp", "zero-separated token text, obfuscated"),
    Table("TWO_LETTER_TABLE", "QQ16", 69, "TokenTables.cpp", "letter pairs, indexed by token"),
    Table("EXTENDED_TOKEN_TABLE", "TKN1", 3112, "ExtendedTokenTables.cpp", "the extended token text, obfuscated"),
    # 82 bytes rather than the 26 the next label suggests: byte values 215 to 255 all index this
    # table, so it is reachable to offset 81 and genuinely overlaps the table that follows it.
    # Sizing a table from the next label is a guess; sizing it from what can index it is not.
    Table("EXTENDED_PAIR_TABLE", "TKN2", 82, "ExtendedTokenTables.cpp", "letter pairs for the extended tokens"),
    Table("SYSTEM_TOKEN_TABLE", "RUTOK", 625, "ExtendedTokenTables.cpp", "per-system description overrides"),
    Table("VARIANT_BASE_TABLE", "MTIN", 38, "ExtendedTokenTables.cpp", "the first of each token's random variants"),
    # ---- slice 1d: the screen. Lengths come from what indexes each table, per section 6.8.
    # TWOS, TWOS2, TWFL and TWFR are all read with an index masked to AND 7, so eight entries.
    Table("PIXEL_MASK_TABLE", "TWOS", 8, "ScreenTables.cpp", "one pixel of a line, by x within the byte"),
    Table("DASH_MASK_TABLE", "TWOS2", 8, "ScreenTables.cpp", "the mark PIXEL plots, by x within the byte"),
    # CTWOS2 is read as CTWOS2,X and as CTWOS2+2,X with X masked to AND 7, so it is reachable to
    # offset 9 -- the two extra rows its callers rely on, not padding.
    Table("MULTICOLOUR_MASK_TABLE", "CTWOS2", 10, "ScreenTables.cpp", "multicolour-aligned pixel masks, two entries per pixel"),
    # Four entries and no C64 routine indexes it in this build; extracted because the ledger
    # names it and it is four bytes, not because anything reads it yet.
    Table("DASHBOARD_MASK_TABLE", "DTWOS", 4, "ScreenTables.cpp", "one multicolour pixel, by pixel number"),
    Table("LINE_RIGHT_MASK_TABLE", "TWFR", 8, "ScreenTables.cpp", "a horizontal line's first byte, filled from x rightwards"),
    Table("LINE_LEFT_MASK_TABLE", "TWFL", 8, "ScreenTables.cpp", "a horizontal line's last byte, filled leftwards to x"),
    # Indexed by a screen y and a character row, so 256 and 25.
    Table("ROW_ADDRESS_LOW", "ylookupl", 256, "ScreenTables.cpp", "low byte of the bitmap address of screen row y"),
    Table("ROW_ADDRESS_HIGH", "ylookuph", 256, "ScreenTables.cpp", "high byte of the same"),
    Table("CELL_ADDRESS_LOW", "celllookl", 25, "ScreenTables.cpp", "low byte of the colour-cell address of character row"),
    Table("CELL_ADDRESS_HIGH", "celllookh", 25, "ScreenTables.cpp", "high byte of the same"),
    # 96 characters of 8 rows, from C.FONT.bin.
    Table("FONT_DATA", "FONT", 768, "Font.cpp", "eight bytes a character, from space upwards"),
    # Four bytes, read as TENS,X with X counting 3 down to 0. The fifth and most significant byte
    # is not in the table at all -- BPRNT subtracts it as an immediate 0x17 -- so the constant is
    # 0x17_4876E800, which is ten to the eleventh.
    Table("TEN_TO_THE_ELEVENTH", "TENS", 4, "ScreenTables.cpp", "the low four bytes of 10^11, for BPRNT"),
    # ---- slice 2c: the market. Four bytes an item -- base price, economy gradient, base
    # quantity, mask -- and seventeen items, because the market screen loops QQ29 to 17. GVL
    # only fills quantities for the first sixteen, which is why Alien Items behave differently.
    Table("MARKET_TABLE", "QQ23", 68, "MarketTable.cpp", "base price, gradient, quantity and mask per item"),
]


def read_table(_path: Path) -> dict[str, int]:
    rows: dict[str, int] = {}
    if not _path.is_file():
        sys.exit(f"error: {_path} not found -- run: python tools/labels.py --assemble")
    for line in _path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line or line.startswith("#") or "\t" not in line:
            continue
        name, value = line.split("\t", 1)
        try:
            rows[name] = int(value)
        except ValueError:
            continue
    return rows


def load_image(_binaries: dict[str, int]) -> bytearray:
    """The whole 64 KB address space with every assembled block in place."""
    image = bytearray(65536)
    for filename, address in _binaries.items():
        path = ASSEMBLED / filename
        if not path.is_file():
            sys.exit(f"error: {path} not found -- run: python tools/labels.py --assemble")
        data = path.read_bytes()
        if address + len(data) > len(image):
            sys.exit(f"error: {filename} at {address:#06x} runs past the end of memory")
        image[address : address + len(data)] = data
    return image


def format_table(_table: Table, _bytes: bytes) -> str:
    lines = [
        f"// 6502: {_table.label} -- {_table.summary}.",
        f"const std::array<std::uint8_t, {_table.length}> {_table.identifier} = {{",
    ]
    for offset in range(0, len(_bytes), 12):
        chunk = ", ".join(f"0x{value:02X}" for value in _bytes[offset : offset + 12])
        lines.append(f"  {chunk},")
    lines.append("};")
    return "\n".join(lines)


def build_file(_name: str, _tables: list[tuple[Table, bytes]]) -> str:
    header = [
        "#include \"pch.h\"",
        "",
        "#include \"LookupTables.h\"",
        "",
        "/*",
        " * GENERATED by tools/extract_tables.py -- do not edit by hand.",
        " *",
        " * Extracted by label and length from the assembled Commodore 64 build. The companion",
        " * test compares every array here against the same address range in the oracle's image,",
        " * so an edit or a stale regeneration fails rather than drifting quietly.",
        " */",
        "",
        "namespace Elite",
        "{",
        "",
    ]
    body = "\n\n".join(format_table(table, data) for table, data in _tables)
    footer = ["", "", "} // namespace Elite", ""]
    return "\n".join(header) + body + "\n".join(footer)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="verify the generated files are current, do not rewrite")
    args = parser.parse_args()

    labels = read_table(LABELS)
    binaries = read_table(BINARIES)
    image = load_image(binaries)

    grouped: dict[str, list[tuple[Table, bytes]]] = {}
    for table in TABLES:
        if table.label not in labels:
            sys.exit(f"error: label '{table.label}' is not in {LABELS.name}")
        address = labels[table.label]
        data = bytes(image[address : address + table.length])
        if len(data) != table.length:
            sys.exit(f"error: {table.label} at {address:#06x} runs past the end of memory")
        grouped.setdefault(table.file, []).append((table, data))
        print(f"  {table.identifier:<20} {table.label:<12} {address:#06x}  {table.length} bytes")

    stale = 0
    for filename, tables in grouped.items():
        text = build_file(filename, tables)
        path = OUTPUT / filename
        current = path.read_text(encoding="utf-8") if path.is_file() else None

        if args.check:
            if current != text:
                print(f"STALE  {filename} differs from what the binaries say it should be")
                stale += 1
            continue

        path.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote  {path.relative_to(REPO)}")

    if args.check:
        if stale:
            print(f"\nFAIL  {stale} generated file(s) are out of date -- run without --check")
            return 1
        print("\nOK    every generated table matches the assembled binaries")

    return 0


if __name__ == "__main__":
    sys.exit(main())
