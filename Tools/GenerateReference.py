#!/usr/bin/env python3
r"""Generate GameLogic/ReferenceImage.h and GameLogic/ReferenceImage.cpp from ELITES.EXE and Design/Symbols.tsv (D3).

The port does not read ELITES.EXE at runtime (D23): what ExeLoader places is these generated tables, the
original's bytes at their original offsets (D19). This reads the reference, refuses it unless its SHA-256
is REFERENCE_SHA256 in GameLogic/Reference.h (ADR-007), and writes two files:

  ReferenceImage.h    what the MZ header says that the load uses -- the entry CS:IP and SS:SP, the minimum
                      and maximum allocation, the image's size and the relocation table -- and the image's
                      declaration
  ReferenceImage.cpp  the load image: the code segment, CS:0000 up to CODE_SEGMENT_BYTES, then the data
                      segment's image from DS:0000, which starts at image paragraph DATA_SEGMENT_PARAGRAPH
                      (both from Reference.h); and the description ExeLoader places

The image is the file's, unpatched: the host writes D5's byte after the load (ADR-001). It is generated
whole, code included, because native code reads parts of the code segment and WipeProgram writes most of
it, and paced time counts a write only where it changes a byte (ADR-018 item 7).

The image is laid out to be read: eight bytes a row, each row with its address in a comment, and a row
broken wherever a row of the symbol table begins, so that the symbol's comment names it there:

  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, // DS:25D8
  0x20,                                           // DS:25E0
  0xFF,                                           // DS:25E1 protectionQuestion

A text row's comment shows its text, as much as fits, between quotes: printable ASCII as it is, any other
byte escaped as in C, and a run of NULs at its end as one. A sized row's text is its size; an unsized
row's runs to the next row.

The layout is clang-format's (Build/CheckFormat.py), so --check and the format check agree. A comment
ending every row is what makes it cheap to check. clang-format 18 lays out a braced list whose rows each
end in a comment as columns: under a second and 190 MiB for this image. The same bytes bin-packed between
comments on lines of their own are searched for their best line breaks instead, which takes it 40 s and
7 GiB (measured 2026-10-10). The list also opens with a comment on a line of its own: without it the
column layout takes 34 s, because clang-format looks back through the list for where a comment section
starts at every line comment, and a comment right after the brace ends that look. Every comment fits at
the comment column within the 140-column limit, which keeps the rows aligned and the columns eight wide.

The files are generated, never edited: run this again. With --check it writes nothing and fails if a
file in the tree is not what it would write. Standard library only.

    python Tools/GenerateReference.py [--check]
"""

import argparse
import csv
import hashlib
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXECUTABLE = ROOT / "ELITES.EXE"
SYMBOLS = ROOT / "Design" / "Symbols.tsv"
REFERENCE = ROOT / "GameLogic" / "Reference.h"
HEADER = ROOT / "GameLogic" / "ReferenceImage.h"
SOURCE = ROOT / "GameLogic" / "ReferenceImage.cpp"

PARAGRAPH_BYTES = 16
PAGE_BYTES = 512
COLUMN_LIMIT = 140
INDENT = "  "
ROW_BYTES = 8
COMMENT_COLUMN = len(INDENT) + ROW_BYTES * len("0x00, ")
MOST_COMMENT_CHARACTERS = COLUMN_LIMIT - COMMENT_COLUMN
ESCAPES = {0x00: "\\0", 0x09: "\\t", 0x0A: "\\n", 0x0D: "\\r", 0x22: '\\"', 0x5C: "\\\\"}


def fail(_message: str) -> None:
  sys.exit(f"GenerateReference: {_message}")


def reference_constant(_text: str, _name: str) -> str:
  """The value of `inline constexpr <type> _name = <value>;` in Reference.h."""
  match = re.search(rf"^inline constexpr [\w:<>]+ {_name} = (.+);$", _text, re.M)
  if match is None:
    fail(f"{REFERENCE.relative_to(ROOT)} has no {_name}")
  return match.group(1).strip()


@dataclass
class Executable:
  """What ExeLoader needs from an MZ file: the header's facts, the image and the relocations."""
  initial_cs: int
  initial_ip: int
  initial_ss: int
  initial_sp: int
  min_allocation: int
  max_allocation: int
  image: bytes
  relocations: list[tuple[int, int]]


def parse(_file: bytes) -> Executable:
  """The MZ header, read as ExeLoader::Parse reads it."""
  if len(_file) < 0x1C:
    fail("the reference is too short for an MZ header")
  (signature, last_page_bytes, pages, relocation_count, header_paragraphs, min_allocation, max_allocation, initial_ss,
   initial_sp, _checksum, initial_ip, initial_cs, relocation_table) = struct.unpack_from("<13H", _file)
  if signature not in (0x5A4D, 0x4D5A) or pages == 0 or last_page_bytes > PAGE_BYTES:
    fail("the reference is not an MZ executable")
  file_bytes = (pages - 1) * PAGE_BYTES + (last_page_bytes or PAGE_BYTES)
  header_bytes = header_paragraphs * PARAGRAPH_BYTES
  if file_bytes > len(_file) or relocation_table + relocation_count * 4 > len(_file):
    fail("the reference is truncated")
  if header_bytes < 0x1C or header_bytes > file_bytes:
    fail("the reference's header does not fit it")
  relocations = [struct.unpack_from("<2H", _file, relocation_table + index * 4) for index in range(relocation_count)]
  return Executable(initial_cs, initial_ip, initial_ss, initial_sp, min_allocation, max_allocation,
                    _file[header_bytes:file_bytes], relocations)


@dataclass
class Segment:
  """One of the image's two segments: its prefix, where it starts in the image and where it ends."""
  prefix: str
  start: int
  end: int


@dataclass
class Symbol:
  """The rows of the symbol table that begin at one byte of the image, and where a text row's text lies."""
  names: list[str] = field(default_factory=list)
  text: tuple[int, int] | None = None


def read_symbols(_segments: list[Segment]) -> dict[int, Symbol]:
  """Every row that begins inside the image, by image offset."""
  with SYMBOLS.open(encoding="utf-8", newline="") as file:
    lines = [line for line in file if not line.startswith("#")]
  rows = []
  for row in csv.DictReader(lines, delimiter="\t"):
    prefix, _, offset = row["address"].strip().partition(":")
    segment = next((segment for segment in _segments if segment.prefix == prefix), None)
    if segment is None:
      fail(f"{row['address']} is in neither segment of the image")
    at = segment.start + int(offset, 16)
    if at < segment.end:
      rows.append((at, segment, row))
  rows.sort(key=lambda _item: _item[0])
  starts = sorted({at for at, _, _ in rows})
  symbols: dict[int, Symbol] = {}
  for at, segment, row in rows:
    symbol = symbols.setdefault(at, Symbol())
    symbol.names.append(row["name"].strip())
    if row["kind"] == "text" and symbol.text is None:
      size = row["size"].strip()
      following = next((start for start in starts if start > at), segment.end)
      symbol.text = (at, min(at + int(size) if size else following, segment.end))
  return symbols


def show_text(_bytes: bytes, _most: int) -> str:
  """_bytes between quotes, in at most _most characters: a run of NULs at the end as one, and "... after the quote
  when the rest does not fit."""
  data = _bytes.rstrip(b"\0") + (b"\0" if _bytes.endswith(b"\0") else b"")
  units = [ESCAPES.get(byte, chr(byte) if 0x20 <= byte < 0x7F else f"\\x{byte:02X}") for byte in data]
  whole = '"' + "".join(units) + '"'
  if len(whole) <= _most:
    return whole
  shown = '"'
  for unit in units:
    if len(shown) + len(unit) + len('"...') > _most:
      break
    shown += unit
  return shown + '"...'


def row_comment(_segment: Segment, _at: int, _symbol: Symbol | None, _image: bytes) -> str:
  comment = f"// {_segment.prefix}:{_at - _segment.start:04X}"
  if _symbol is None:
    return comment
  comment += " " + ", ".join(_symbol.names)
  if len(comment) > MOST_COMMENT_CHARACTERS:
    fail(f"the comment '{comment}' is longer than {MOST_COMMENT_CHARACTERS} characters")
  if _symbol.text is not None:
    start, end = _symbol.text
    comment += " " + show_text(_image[start:end], MOST_COMMENT_CHARACTERS - len(comment) - 1)
  return comment


def image_rows(_image: bytes, _segments: list[Segment], _symbols: dict[int, Symbol]) -> list[str]:
  """The image's rows: up to ROW_BYTES bytes from an address that is a multiple of ROW_BYTES in its segment, broken
  where a symbol begins, each with its comment at the comment column."""
  rows = []
  for segment in _segments:
    at = segment.start
    while at < segment.end:
      end = min(segment.end, segment.start + ((at - segment.start) // ROW_BYTES + 1) * ROW_BYTES)
      end = next((start for start in range(at + 1, end) if start in _symbols), end)
      body = INDENT + ", ".join(f"0x{byte:02X}" for byte in _image[at:end]) + ","
      rows.append(body.ljust(COMMENT_COLUMN) + row_comment(segment, at, _symbols.get(at), _image))
      at = end
  return rows


def containing_name(_symbols: dict[int, Symbol], _at: int) -> str:
  start = max((start for start in _symbols if start <= _at), default=None)
  return f" in {_symbols[start].names[0]}" if start is not None else ""


GENERATED = ("// Generated by Tools/GenerateReference.py from ELITES.EXE and Design/Symbols.tsv. Do not edit: run the tool "
             "again (D3).")


def offsets(_segment: Segment) -> str:
  """A segment's offsets in the image, as CS:0000-8F3F."""
  return f"{_segment.prefix}:0000-{_segment.end - _segment.start - 1:04X}"


def generate_header(_executable: Executable, _segments: list[Segment], _symbols: dict[int, Symbol]) -> str:
  code, data = _segments
  image_bytes = f"{len(_executable.image):,}".replace(",", "'")
  out = [
    "// GameLogic/ReferenceImage.h",
    GENERATED,
    "#pragma once",
    "",
    '#include "ExeLoader.h"',
    "",
    "#include <array>",
    "#include <cstddef>",
    "#include <cstdint>",
    "",
    "namespace Elite",
    "{",
    "",
    "// What ELITES.EXE's MZ header says that the load uses (ADR-006 item 4): where the program starts and where its",
    "// stack is, relative to the load segment, and the paragraphs past the image it needs and asks for.",
    f"inline constexpr std::uint16_t REFERENCE_INITIAL_CS = 0x{_executable.initial_cs:04X};",
    f"inline constexpr std::uint16_t REFERENCE_INITIAL_IP = 0x{_executable.initial_ip:04X};",
    f"inline constexpr std::uint16_t REFERENCE_INITIAL_SS = 0x{_executable.initial_ss:04X};",
    f"inline constexpr std::uint16_t REFERENCE_INITIAL_SP = 0x{_executable.initial_sp:04X};",
    f"inline constexpr std::uint16_t REFERENCE_MIN_ALLOCATION_PARAGRAPHS = 0x{_executable.min_allocation:04X};",
    f"inline constexpr std::uint16_t REFERENCE_MAX_ALLOCATION_PARAGRAPHS = 0x{_executable.max_allocation:04X};",
    "",
    f"/// The load image's size: the code segment, {offsets(code)}, then the data segment's image, {offsets(data)}.",
    f"inline constexpr std::size_t REFERENCE_IMAGE_BYTES = {image_bytes};",
    "",
    "/// The relocation table, in the file's order: each entry the offset and segment, from the image's start, of a word",
    "/// the load adds the load segment to. Each comment says where it is and what the word holds.",
    f"inline constexpr std::array<Machine::Relocation, {len(_executable.relocations)}> REFERENCE_RELOCATIONS = {{{{",
  ]
  entries = []
  for offset, segment in _executable.relocations:
    at = segment * PARAGRAPH_BYTES + offset
    place = next(item for item in _segments if item.start <= at < item.end)
    word = struct.unpack_from("<H", _executable.image, at)[0]
    entries.append((f"{INDENT}{{0x{offset:04X}, 0x{segment:04X}}},",
                    f"// {place.prefix}:{at - place.start:04X}{containing_name(_symbols, at)}: {word:04X}h"))
  width = max(len(entry) for entry, _ in entries) + 1
  out += [entry.ljust(width) + comment for entry, comment in entries]
  out += [
    "}};",
    "",
    "/// The load image, byte for byte as ELITES.EXE holds it: DS:25E4 holds 00, and the host writes D5's byte after the",
    "/// load (ADR-001). Defined in ReferenceImage.cpp, so that its bytes are compiled once.",
    "extern const std::array<std::uint8_t, REFERENCE_IMAGE_BYTES> REFERENCE_IMAGE;",
    "",
    "/// The reference as ExeLoader places it: the header's facts, the image and the relocations above.",
    "[[nodiscard]] Machine::Executable ReferenceExecutable();",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def generate_source(_executable: Executable, _segments: list[Segment], _symbols: dict[int, Symbol]) -> str:
  code, data = _segments
  out = [
    "// GameLogic/ReferenceImage.cpp",
    GENERATED,
    '#include "pch.h"',
    "",
    '#include "ReferenceImage.h"',
    "",
    "namespace Elite",
    "{",
    "",
    "// Eight bytes a row, each row with the address of its first. A row ends where a row of the symbol table begins, and",
    "// the next row names it; a text row's name is followed by its text between quotes, as much as fits: printable ASCII",
    "// as it is, any other byte escaped as in C.",
    "const std::array<std::uint8_t, REFERENCE_IMAGE_BYTES> REFERENCE_IMAGE = {",
    f"{INDENT}// The code segment, {offsets(code)}, then the data segment's image, {offsets(data)}.",
    *image_rows(_executable.image, _segments, _symbols),
    "};",
    "",
    "Machine::Executable ReferenceExecutable()",
    "{",
    f"{INDENT}Machine::Executable executable;",
    f"{INDENT}executable.initialCs = REFERENCE_INITIAL_CS;",
    f"{INDENT}executable.initialIp = REFERENCE_INITIAL_IP;",
    f"{INDENT}executable.initialSs = REFERENCE_INITIAL_SS;",
    f"{INDENT}executable.initialSp = REFERENCE_INITIAL_SP;",
    f"{INDENT}executable.minAllocationParagraphs = REFERENCE_MIN_ALLOCATION_PARAGRAPHS;",
    f"{INDENT}executable.maxAllocationParagraphs = REFERENCE_MAX_ALLOCATION_PARAGRAPHS;",
    f"{INDENT}executable.image = REFERENCE_IMAGE;",
    f"{INDENT}executable.relocations.assign(REFERENCE_RELOCATIONS.begin(), REFERENCE_RELOCATIONS.end());",
    f"{INDENT}return executable;",
    "}",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--check", action="store_true", help="fail if a generated file in the tree is out of date")
  arguments = parser.parse_args()

  reference = REFERENCE.read_text(encoding="utf-8")
  expected = reference_constant(reference, "REFERENCE_SHA256").strip('"')
  code_bytes = int(reference_constant(reference, "CODE_SEGMENT_BYTES"), 0)
  data_paragraph = int(reference_constant(reference, "DATA_SEGMENT_PARAGRAPH"), 0)
  if not EXECUTABLE.is_file():
    fail(f"{EXECUTABLE.relative_to(ROOT)} is not there; it is the reference these files are generated from. It is not in "
         "the tree (D28, ADR-020): put a local copy at the root, which .gitignore keeps out of git")
  file = EXECUTABLE.read_bytes()
  digest = hashlib.sha256(file).hexdigest()
  if digest != expected:
    fail(f"{EXECUTABLE.relative_to(ROOT)} has SHA-256 {digest}, not the reference's {expected} (ADR-007)")

  executable = parse(file)
  if data_paragraph * PARAGRAPH_BYTES != code_bytes or code_bytes >= len(executable.image):
    fail("the code segment does not end where the data segment begins (Reference.h)")
  segments = [Segment("CS", 0, code_bytes), Segment("DS", code_bytes, len(executable.image))]
  symbols = read_symbols(segments)
  outputs = {HEADER: generate_header(executable, segments, symbols),
             SOURCE: generate_source(executable, segments, symbols)}
  names = sum(len(symbol.names) for symbol in symbols.values())
  summary = (f"{len(executable.image):,} bytes, {len(executable.relocations)} relocations, {names} symbols named, "
             f"{sum(1 for symbol in symbols.values() if symbol.text is not None)} texts shown")
  if arguments.check:
    stale = [path for path, text in outputs.items()
             if (path.read_bytes().decode("utf-8") if path.exists() else "") != text]
    for path in stale:
      print(f"GenerateReference: {path.relative_to(ROOT)} is out of date; run python Tools/GenerateReference.py")
    if stale:
      return 1
    print(f"GenerateReference: up to date ({summary})")
    return 0
  for path, text in outputs.items():
    path.write_bytes(text.encode("utf-8"))
    print(f"GenerateReference: wrote {path.relative_to(ROOT)}")
  print(f"GenerateReference: {summary}")
  return 0


if __name__ == "__main__":
  sys.exit(main())
