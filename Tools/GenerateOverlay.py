#!/usr/bin/env python3
"""Generate GameLogic/DataOverlay.h and GameLogic/GameData.h from Design/Symbols.tsv (ADR-010, D19).

Native code names the reference's data by the symbol table's names (plan §3): it reads
`guest.Get(DS.playerDocked)`, not a byte at 0x75xx. Every DS row of kind data, table or text becomes
one field of `Elite::DataOverlay`, typed from its size and, for a table, from the element type its
contract column gives:

  data, 1 byte         DataField<std::uint8_t>
  data, 2 bytes        DataField<std::uint16_t>
  data, other sizes    DataBlock<bytes>
  table                DataTable<element bytes, entries> when the element type is one this tool can
                       size ("byte", "word", "pointer", "code", "dword", "N-byte ...", "N bytes",
                       "N words", "N characters", "byte pair", "word pair", or a record of words and
                       bytes such as "word sin, word cos"), else DataAt
  text                 DataBlock<bytes> with a size, else DataAt

The same rows make `Elite::GameData`, the data segment as one packed struct of exactly 64 KiB that the
GameState holds (D19), every row a member at its offset:

  data, 1 byte         std::uint8_t
  data, 2 bytes        std::uint16_t
  table of words       std::uint16_t[entries], for an element of "word", "pointer" or "code"
  table, other         std::uint8_t[entries][element bytes], or std::uint8_t[entries] for bytes
  anything else        std::uint8_t[bytes]

A row with no size, or a table whose element this tool cannot size, runs to the next row, or to the
end of the row it lies in. A row that lies inside another is its child:

  * a word with children (a byte the symbol table names inside it) becomes a union of the word and a
    struct of its bytes, `<word>Bytes`;
  * any other row with children becomes a nested struct named for it, holding the children.

Bytes no row names become byte arrays named for their offset, `gapXXXX`. A row that starts inside
another and ends past it is an error. Every member's offset is checked by a static_assert, and so is
every struct's size; DataOverlay.h checks each of its fields against its member in GameData, so the
two cannot drift.

The headers are generated, never edited: change the symbol table and run this again. With --check it
writes nothing and fails if a header in the tree is not what it would write. Standard library only.

    python Tools/GenerateOverlay.py [--check]
"""

import argparse
import csv
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYMBOLS = ROOT / "Design" / "Symbols.tsv"
HEADER = ROOT / "GameLogic" / "DataOverlay.h"
GAME_DATA = ROOT / "GameLogic" / "GameData.h"
IDENTIFIER = re.compile(r"[a-z][A-Za-z0-9]*")
SEGMENT_BYTES = 0x10000
FIELD_BYTES = {"word": 2, "byte": 1}


def record_bytes(_text: str) -> int | None:
  """The size of a record the contract spells as fields of words and bytes ("word sin, word cos"), if it is one."""
  parts = [part.strip() for part in _text.split(",")]
  if len(parts) < 2 or ":" in parts[0]:
    return None
  sizes = [FIELD_BYTES.get(part.split(" ")[0]) for part in parts]
  return sum(sizes) if all(sizes) else None


def element_bytes(_contract: str) -> int | None:
  """The size of one table entry from the element type a table row's contract gives, if it can tell."""
  text = _contract.strip().lower()
  if "zero-terminated" in text:
    return None
  for pattern, scale in ((r"(\d+)-byte\b", 1), (r"(\d+) bytes\b", 1), (r"(\d+) words\b", 2), (r"(\d+) characters\b", 1)):
    match = re.match(pattern, text)
    if match:
      return int(match.group(1)) * scale
  record = record_bytes(text)
  if record:
    return record
  if text.startswith("byte pair"):
    return 2
  if text.startswith("word pair") or text.startswith("dword"):
    return 4
  if re.match(r"(byte)\b", text):
    return 1
  if re.match(r"(word|pointer|code)\b", text):
    return 2
  return None


def is_word_element(_contract: str) -> bool:
  """Whether a table's element is one word: a word, a pointer or a code address."""
  text = _contract.strip().lower()
  return element_bytes(_contract) == 2 and re.match(r"(word|pointer|code)\b", text) is not None


def field_type(_kind: str, _size: str, _contract: str) -> str:
  size = int(_size) if _size.strip() else None
  if _kind == "data":
    if size == 1:
      return "DataField<std::uint8_t>"
    if size == 2:
      return "DataField<std::uint16_t>"
    return f"DataBlock<{size}>" if size else "DataAt"
  if _kind == "table":
    element = element_bytes(_contract)
    return f"DataTable<{element}, {size}>" if element and size else "DataAt"
  return f"DataBlock<{size}>" if size else "DataAt"


ASCII = {"—": "-", "–": "-", "±": "+-", "×": "x", "·": ".", "‘": "'", "’": "'",
         "“": '"', "”": '"', "…": "...", "→": "->", "≥": ">=", "≤": "<=", "°": " degrees"}
MOST_COMMENT_CHARACTERS = 132
COLUMN_LIMIT = 140


def comment(_row: dict[str, str], _most: int = MOST_COMMENT_CHARACTERS) -> str:
  """The row's notes (and a table's element type) as one line of ASCII of at most _most characters."""
  parts = [part for part in (_row["contract"].strip() if _row["kind"] == "table" else "", _row["notes"].strip()) if part]
  text = re.sub(r"\s+", " ", "; ".join(parts))
  text = "".join(ASCII.get(character, character) for character in text)
  text = text.encode("ascii", "replace").decode("ascii")
  return text if len(text) <= _most else text[: _most - 3].rstrip() + "..."


def read_rows() -> list[dict[str, str]]:
  """Every DS row of kind data, table or text, in offset order, with its offset as `offset`."""
  rows = []
  with SYMBOLS.open(encoding="utf-8", newline="") as file:
    lines = [line for line in file if not line.startswith("#")]
  for row in csv.DictReader(lines, delimiter="\t"):
    address = row["address"].strip()
    if not address.startswith("DS:") or row["kind"] not in ("data", "table", "text"):
      continue
    name = row["name"].strip()
    if not IDENTIFIER.fullmatch(name):
      sys.exit(f"GenerateOverlay: {address} is named '{name}', which is not a camelCase identifier")
    row["name"] = name
    row["offset"] = int(address[3:], 16)
    rows.append(row)
  rows.sort(key=lambda _row: _row["offset"])
  names = [row["name"] for row in rows]
  if len(names) != len(set(names)):
    sys.exit("GenerateOverlay: two data rows share a name")
  offsets = [row["offset"] for row in rows]
  if len(offsets) != len(set(offsets)):
    sys.exit("GenerateOverlay: two data rows share an offset")
  return rows


def generate(_rows: list[dict[str, str]], _members: list["Member"]) -> str:
  out = [
    "// GameLogic/DataOverlay.h",
    "// Generated by Tools/GenerateOverlay.py from Design/Symbols.tsv. Do not edit: change the symbol table",
    "// and run the tool again (ADR-010).",
    "#pragma once",
    "",
    '#include "DataField.h"',
    '#include "GameData.h"',
    "",
    "#include <cstddef>",
    "#include <cstdint>",
    "",
    "namespace Elite",
    "{",
    "",
    "/// The reference's data segment by name (plan §3, ADR-010): one field for every data, table and text",
    "/// row of the symbol table, at its offset. Native code reads and writes through Guest with these.",
    "struct DataOverlay",
    "{",
  ]
  for row in _rows:
    note = comment(row)
    if note:
      out.append(f"  /// {note}")
    out.append(f"  {field_type(row['kind'], row['size'], row['contract'])} {row['name']}{{0x{row['offset']:04X}}};")
  out += [
    "};",
    "",
    "/// The overlay native code uses: `DS.playerDocked`.",
    "inline constexpr DataOverlay DS{};",
    "",
    "// Every field at its member's offset in GameData, the struct the GameState holds (D19), so the two cannot drift.",
  ]
  paths = {member.name: member.path for member in walk(_members) if member.row is not None}
  for row in _rows:
    out.append(f"static_assert(DS.{row['name']}.offset == GAME_DATA_OFFSET({paths[row['name']]}));")
  out += [
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


@dataclass
class Member:
  """One member of GameData or of a struct nested in it: a row of the symbol table, or a gap no row names."""
  offset: int
  size: int
  name: str
  row: dict[str, str] | None = None
  path: str = ""
  children: list["Member"] = field(default_factory=list)

  @property
  def end(self) -> int:
    return self.offset + self.size

  def is_word(self) -> bool:
    return self.row is not None and self.row["kind"] == "data" and self.size == 2


def byte_size(_row: dict[str, str]) -> int | None:
  """A row's size in bytes, if the symbol table gives it: a table's is its entries times its element's."""
  size = int(_row["size"]) if _row["size"].strip() else None
  if size is None or _row["kind"] != "table":
    return size
  element = element_bytes(_row["contract"])
  return element * size if element else None


def build_members(_rows: list[dict[str, str]]) -> list[Member]:
  """The rows as a tree of members, each row the child of the row it lies in, with the gaps between them."""
  root = Member(0, SEGMENT_BYTES, "GameData")
  open_members = [root]
  for index, row in enumerate(_rows):
    while row["offset"] >= open_members[-1].end:
      open_members.pop()
    parent = open_members[-1]
    size = byte_size(row)
    if size is None:
      following = _rows[index + 1]["offset"] if index + 1 < len(_rows) else SEGMENT_BYTES
      size = min(following, parent.end) - row["offset"]
    member = Member(row["offset"], size, row["name"], row)
    if size <= 0 or member.end > parent.end:
      sys.exit(f"GenerateOverlay: {row['name']} at DS:{row['offset']:04X} ({size} bytes) does not fit inside "
               f"{parent.name}")
    parent.children.append(member)
    open_members.append(member)
  fill_gaps(root)
  return root.children


def fill_gaps(_member: Member) -> None:
  """Puts a gap member wherever no child of _member names the bytes, and gives every member its path from GameData."""
  members = []
  at = _member.offset
  for child in _member.children + [Member(_member.end, 0, "")]:
    if child.offset > at:
      members.append(Member(at, child.offset - at, f"gap{at:04X}"))
    if child.size > 0:
      members.append(child)
    at = max(at, child.end)
  _member.children = members
  for child in members:
    if _member.row is None:
      child.path = child.name
    elif _member.is_word():
      child.path = f"{_member.path}Bytes.{child.name}"
    else:
      child.path = f"{_member.path}.{child.name}"
    if child.children:
      fill_gaps(child)


def walk(_members: list[Member]):
  """Every member, depth first."""
  for member in _members:
    yield member
    yield from walk(member.children)


def type_name(_member: Member) -> str:
  return _member.name[0].upper() + _member.name[1:]


def declaration(_member: Member) -> str:
  """The declaration of a member that has no children."""
  row = _member.row
  if row is None:
    return f"std::uint8_t {_member.name}[{_member.size}];"
  if row["kind"] == "data" and _member.size in (1, 2):
    return f"{'std::uint8_t' if _member.size == 1 else 'std::uint16_t'} {_member.name};"
  if row["kind"] == "table" and byte_size(row) is not None:
    element = element_bytes(row["contract"])
    entries = int(row["size"])
    if is_word_element(row["contract"]):
      return f"std::uint16_t {_member.name}[{entries}];"
    if element == 1:
      return f"std::uint8_t {_member.name}[{entries}];"
    return f"std::uint8_t {_member.name}[{entries}][{element}];"
  return f"std::uint8_t {_member.name}[{_member.size}];"


def emit_members(_members: list[Member], _indent: str, _out: list[str]) -> None:
  for member in _members:
    note = comment(member.row, COLUMN_LIMIT - len(_indent) - 4) if member.row is not None else ""
    if not member.children:
      if note:
        _out.append(f"{_indent}/// {note}")
      _out.append(f"{_indent}{declaration(member)}")
      continue
    inner = _indent + "  "
    if member.is_word():
      _out.append(f"{_indent}/// {member.name}'s two bytes, as the symbol table names them (DS:{member.offset:04X}).")
      _out.append(f"{_indent}struct {type_name(member)}Bytes")
      _out.append(f"{_indent}{{")
      emit_members(member.children, inner, _out)
      _out.append(f"{_indent}}};")
      _out.append(f"{_indent}union")
      _out.append(f"{_indent}{{")
      if note:
        _out.append(f"{inner}/// {comment(member.row, COLUMN_LIMIT - len(inner) - 4)}")
      _out.append(f"{inner}std::uint16_t {member.name};")
      _out.append(f"{inner}{type_name(member)}Bytes {member.name}Bytes;")
      _out.append(f"{_indent}}};")
      continue
    _out.append(f"{_indent}/// {member.name}'s fields, DS:{member.offset:04X}-{member.end - 1:04X}.")
    _out.append(f"{_indent}struct {type_name(member)}")
    _out.append(f"{_indent}{{")
    emit_members(member.children, inner, _out)
    _out.append(f"{_indent}}};")
    if note:
      _out.append(f"{_indent}/// {note}")
    _out.append(f"{_indent}{type_name(member)} {member.name};")


def struct_types(_members: list[Member], _scope: str) -> list[tuple[str, int]]:
  """Every struct nested in GameData, as its qualified name and its size, depth first."""
  types = []
  for member in _members:
    if not member.children:
      continue
    name = f"{_scope}::{type_name(member)}{'Bytes' if member.is_word() else ''}"
    types.append((name, member.size))
    types += struct_types(member.children, name)
  return types


def offset_paths(_members: list[Member]) -> list[tuple[str, int]]:
  """Every member's path from GameData and its offset, depth first; a word's bytes struct among them."""
  paths = []
  for member in _members:
    paths.append((member.path, member.offset))
    if member.is_word() and member.children:
      paths.append((f"{member.path}Bytes", member.offset))
    paths += offset_paths(member.children)
  return paths


def generate_game_data(_members: list[Member]) -> str:
  out = [
    "// GameLogic/GameData.h",
    "// Generated by Tools/GenerateOverlay.py from Design/Symbols.tsv. Do not edit: change the symbol table",
    "// and run the tool again (D19).",
    "#pragma once",
    "",
    "#include <cstddef>",
    "#include <cstdint>",
    "",
    "// A member's offset in GameData, as a constant expression wherever this header is read. Clang reading the MSVC headers,",
    "// as clang-tidy does, gets vcruntime.h's offsetof, a reinterpret_cast that is not one, so under Clang it is the builtin",
    "// that offsetof is everywhere else.",
    "#if defined(__clang__)",
    "#   define GAME_DATA_OFFSET(member) __builtin_offsetof(::Elite::GameData, member)",
    "#else",
    "#   define GAME_DATA_OFFSET(member) offsetof(::Elite::GameData, member)",
    "#endif",
    "",
    "namespace Elite",
    "{",
    "",
    "#pragma pack(push, 1)",
    "",
    "/// The reference's data segment as one struct (D19): every data, table and text row of the symbol",
    "/// table a member at its 1987 offset, packed, 64 KiB in all, which the GameState holds. A row inside",
    "/// another is a member of a struct nested for it, or, inside a word, of a union with it; bytes no row",
    "/// names are gaps named for their offset. Native code reads and writes these bytes through the",
    "/// GameState's accessors and DataOverlay.h's fields, never through references to these members, which",
    "/// are unaligned.",
    "struct GameData",
    "{",
  ]
  emit_members(_members, "  ", out)
  out += [
    "};",
    "",
    "#pragma pack(pop)",
    "",
    "// Every member at its offset in the data segment, every struct at its size, and the segment's 64 KiB.",
  ]
  for path, offset in offset_paths(_members):
    out.append(f"static_assert(GAME_DATA_OFFSET({path}) == 0x{offset:04X});")
  for name, size in struct_types(_members, "GameData"):
    out.append(f"static_assert(sizeof({name}) == {size});")
  out += [
    f"static_assert(sizeof(GameData) == 0x{SEGMENT_BYTES:X});",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def measure(_members: list[Member]) -> str:
  """What the struct holds, for the record (D19)."""
  members = list(walk(_members))
  named = sum(1 for member in members if member.row is not None)
  gaps = [member for member in members if member.row is None]
  unions = sum(1 for member in members if member.is_word() and member.children)
  nested = sum(1 for member in members if member.children and not member.is_word())
  return (f"{named} named members, {len(gaps)} gaps of {sum(gap.size for gap in gaps)} bytes, "
          f"{nested} nested structs, {unions} unions")


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--check", action="store_true", help="fail if a header in the tree is out of date")
  arguments = parser.parse_args()
  rows = read_rows()
  members = build_members(rows)
  outputs = {HEADER: generate(rows, members), GAME_DATA: generate_game_data(members)}
  if arguments.check:
    stale = [path for path, text in outputs.items()
             if (path.read_bytes().decode("utf-8") if path.exists() else "") != text]
    for path in stale:
      print(f"GenerateOverlay: {path.relative_to(ROOT)} is out of date; run python Tools/GenerateOverlay.py")
    if stale:
      return 1
    print(f"GenerateOverlay: up to date ({measure(members)})")
    return 0
  for path, text in outputs.items():
    path.write_bytes(text.encode("utf-8"))
    print(f"GenerateOverlay: wrote {path.relative_to(ROOT)}")
  print(f"GenerateOverlay: {measure(members)}")
  return 0


if __name__ == "__main__":
  sys.exit(main())
