#!/usr/bin/env python3
"""Generate the game's state, GameLogic/GameData.h, a header per group of it, and GameLogic/DataPlaces.h, from
Design/Symbols.tsv and Design/DataPartition.tsv (D19, D38).

Native code names the reference's data by the symbol table's names (plan §3): it reads
`_state.Data().flight.stardust`, not a byte at 0x27F5. The data segment is distributed (D38, change 3a): every top-level
member of the 1987 data segment, a DS row of kind data, table or text, goes where Design/DataPartition.tsv says, in its
group's struct, `<Group>State` in `GameLogic/<Group>State.h`, and `Elite::GameData`, which the GameState holds, is a struct of
the groups' structs (`GameData::flight`, `GameData::commander`, ...):

  a subsystem          what that one subsystem writes: FlightState, VideoState, ...
  a shared group       what several subsystems write: PlayerShipState, MessagesState, ...
  Commander            commanderBlock, kept whole at its 1987 layout, every member's offset asserted, as CommanderState: it is the
                       .cdr file's format, and Replays/*.cdr are files of it
  constant             what nothing writes: not in the state, but its readers' constexpr tables (Tools/GenerateTables.py),
                       which the reference's image no longer loads
  unused               what nothing reads, UnusedState, which change 3b removes

A group's members keep their names and their 1987 order. The gaps between them go, but for the ones the partition lists, which
the game uses. Each byte and word is a `Counted` (Counted.h), which counts its own changes, so that native code writes members
directly (D33's piece 3):

  data, 1 byte         Counted<std::uint8_t>
  data, 2 bytes        Counted<std::uint16_t>
  table of words       Counted<std::uint16_t>[entries], for an element of "word", "pointer" or "code"
  table, other         Counted<std::uint8_t>[entries][element bytes], or Counted<std::uint8_t>[entries] for bytes
  anything else        Counted<std::uint8_t>[bytes]

A table's element is sized from the element type its contract column gives, when it is one this tool
can size: "byte", "word", "pointer", "code", "dword", "N-byte ...", "N bytes", "N words",
"N characters", "byte pair", "word pair", or a record of words and bytes such as "word sin, word cos".

A word in WORD_TYPES, where the original stores an address and native code an index or a message (D33, piece 3), is a
Counted of the enumeration it names instead, from the header that declares it.

A container in RECORDS is an array of records of a type written by hand, which the symbol table does not describe field by
field: the ship slots are one array of SpaceObject.

A row with no size, or a table whose element this tool cannot size, runs to the next row, or to the
end of the row it lies in. A row that lies inside another is its child:

  * a word with children (a byte the symbol table names inside it) becomes a union of the word and a
    struct of its bytes, `<word>Bytes`;
  * any other row with children becomes a nested struct named for it, holding the children, and the bytes no child names
    inside it gaps named for their offset, `gapXXXX`, at their 1987 places.

A row that starts inside another and ends past it is an error. Every struct is trivially copyable and standard-layout, and its
copy constructor explicit (ADR-024).

DataPlaces.h is the map from the 1987 layout to this one: DATA_PLACES, every top-level member's name, its 1987 offset and size,
and its place in GameData, or none for a constant, which the GameState builds its view of the original's addresses from, the
reference's image loads by (ReferenceLoadImage), and the state digests' re-base checks by.

The headers are generated, never edited: change the symbol table or the partition and run this again. With --check it writes
nothing and fails if a header in the tree is not what it would write, or a header it once wrote is left over. Standard library
only.

    python Tools/GenerateGameData.py [--check]
"""

import argparse
import csv
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYMBOLS = ROOT / "Design" / "Symbols.tsv"
GAME_DATA = ROOT / "GameLogic" / "GameData.h"
IDENTIFIER = re.compile(r"[a-z][A-Za-z0-9]*")
SEGMENT_BYTES = 0x10000
FIELD_BYTES = {"word": 2, "byte": 1}
BYTE = "Counted<std::uint8_t>"
WORD = "Counted<std::uint16_t>"
# Containers of records native code reads by field: the container, the record's type, its header and its size. The slots'
# fields are not rows of the symbol table, which holds one row per address; SpaceObject.h names them, at their offsets. Each is one
# array of its records.
RECORDS = {"shipSlots": ("SpaceObject", "SpaceObject.h", 64)}
# Words that hold an enumeration where the original holds an address (D33, piece 3): the word, the enumeration and its
# header. The messages are the texts the message line shows; the slots are shipSlots' records by number.
WORD_TYPES = {
  "messageShown": ("Message", "Message.h"),
  "messagePointer": ("Message", "Message.h"),
  "warningMessage": ("Message", "Message.h"),
  "playerHitBy": ("SlotIndex", "SpaceObject.h"),
  "maskShipSlot": ("SlotIndex", "SpaceObject.h"),
  "missileTarget": ("SlotIndex", "SpaceObject.h"),
}


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


ASCII = {"—": "-", "–": "-", "±": "+-", "×": "x", "·": ".", "‘": "'", "’": "'",
         "“": '"', "”": '"', "…": "...", "→": "->", "≥": ">=", "≤": "<=", "°": " degrees"}
COLUMN_LIMIT = 140


def comment(_row: dict[str, str], _most: int) -> str:
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
      sys.exit(f"GenerateGameData: {address} is named '{name}', which is not a camelCase identifier")
    row["name"] = name
    row["offset"] = int(address[3:], 16)
    rows.append(row)
  rows.sort(key=lambda _row: _row["offset"])
  names = [row["name"] for row in rows]
  if len(names) != len(set(names)):
    sys.exit("GenerateGameData: two data rows share a name")
  offsets = [row["offset"] for row in rows]
  if len(offsets) != len(set(offsets)):
    sys.exit("GenerateGameData: two data rows share an offset")
  return rows


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
      sys.exit(f"GenerateGameData: {row['name']} at DS:{row['offset']:04X} ({size} bytes) does not fit inside "
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
    return f"{BYTE} {_member.name}[{_member.size}];"
  if row["kind"] == "data" and _member.size == 2 and _member.name in WORD_TYPES:
    return f"Counted<{WORD_TYPES[_member.name][0]}> {_member.name};"
  if row["kind"] == "data" and _member.size in (1, 2):
    return f"{BYTE if _member.size == 1 else WORD} {_member.name};"
  if row["kind"] == "table" and byte_size(row) is not None:
    element = element_bytes(row["contract"])
    entries = int(row["size"])
    if is_word_element(row["contract"]):
      return f"{WORD} {_member.name}[{entries}];"
    if element == 1:
      return f"{BYTE} {_member.name}[{entries}];"
    return f"{BYTE} {_member.name}[{entries}][{element}];"
  return f"{BYTE} {_member.name}[{_member.size}];"


def emit_copy_rule(_type: str, _indent: str, _out: list[str]) -> None:
  """A struct's constructors: trivial, so GameData stays trivially copyable, and the copy explicit. A copy is detached from
  the GameState's block, and a write to one of its Counted members would count into whatever the block's alignment
  rounds its address down to (Counted.h), so `auto x = member`, a by-value parameter and a return by value do not compile."""
  _out.append(f"{_indent}{_type}() noexcept = default;")
  _out.append(f"{_indent}explicit {_type}(const {_type}&) noexcept = default;")
  _out.append("")


def record_declaration(_member: Member) -> str:
  """The declaration of a container of RECORDS: one array of its records."""
  record, _, record_bytes = RECORDS[_member.name]
  if _member.size % record_bytes != 0:
    sys.exit(f"GenerateGameData: {_member.name} at DS:{_member.offset:04X} ({_member.size} bytes) is not whole records of {record}")
  return f"{record} {_member.name}[{_member.size // record_bytes}];"


def emit_members(_members: list[Member], _indent: str, _out: list[str]) -> None:
  for member in _members:
    note = comment(member.row, COLUMN_LIMIT - len(_indent) - 4) if member.row is not None else ""
    if member.name in RECORDS:
      if note:
        _out.append(f"{_indent}/// {note}")
      _out.append(f"{_indent}{record_declaration(member)}")
      continue
    if not member.children:
      if note:
        _out.append(f"{_indent}/// {note}")
      _out.append(f"{_indent}{declaration(member)}")
      continue
    inner = _indent + "  "
    if member.is_word():
      _out.append(f"{_indent}/// {member.name}'s two bytes, as the symbol table names them (DS:{member.offset:04X} in 1987).")
      _out.append(f"{_indent}struct {type_name(member)}Bytes")
      _out.append(f"{_indent}{{")
      emit_copy_rule(f"{type_name(member)}Bytes", inner, _out)
      emit_members(member.children, inner, _out)
      _out.append(f"{_indent}}};")
      _out.append(f"{_indent}union")
      _out.append(f"{_indent}{{")
      if note:
        _out.append(f"{inner}/// {comment(member.row, COLUMN_LIMIT - len(inner) - 4)}")
      _out.append(f"{inner}{WORD} {member.name};")
      _out.append(f"{inner}{type_name(member)}Bytes {member.name}Bytes;")
      _out.append(f"{_indent}}};")
      continue
    _out.append(f"{_indent}/// {member.name}'s fields, DS:{member.offset:04X}-{member.end - 1:04X} in 1987, at their places there.")
    _out.append(f"{_indent}struct {type_name(member)}")
    _out.append(f"{_indent}{{")
    emit_copy_rule(type_name(member), inner, _out)
    emit_members(member.children, inner, _out)
    _out.append(f"{_indent}}};")
    if note:
      _out.append(f"{_indent}/// {note}")
    _out.append(f"{_indent}{type_name(member)} {member.name};")


def struct_types(_members: list[Member], _scope: str) -> list[tuple[str, int]]:
  """Every struct nested in _members, as its name qualified from _scope and its size, depth first."""
  types = []
  for member in _members:
    if not member.children or member.name in RECORDS:
      continue
    name = f"{_scope}::{type_name(member)}{'Bytes' if member.is_word() else ''}"
    types.append((name, member.size))
    types += struct_types(member.children, name)
  return types


def offset_paths(_members: list[Member], _strip: str, _base: int) -> list[tuple[str, int]]:
  """Every member's path, less the prefix _strip, and its offset from _base, depth first; a word's bytes struct among them."""
  paths = []
  for member in _members:
    path = member.path.removeprefix(_strip)
    paths.append((path, member.offset - _base))
    if member.is_word() and member.children:
      paths.append((f"{path}Bytes", member.offset - _base))
    paths += offset_paths(member.children, _strip, _base)
  return paths


# ---- The partition ----

PARTITION = ROOT / "Design" / "DataPartition.tsv"
GAME_LOGIC = ROOT / "GameLogic"
DATA_PLACES = GAME_LOGIC / "DataPlaces.h"
GENERATED = "// Generated by Tools/GenerateGameData.py"
COMMANDER = "commanderBlock"
CONSTANT = "constant"
UNUSED = "unused"
# The subsystems that may own state, each the stem of its .cpp, its filter and its struct's header, <Subsystem>State.h.
SUBSYSTEMS = ["Ai", "Combat", "Docked", "Docking", "Equipment", "Flight", "Galaxy", "Hyperspace", "Input", "Market", "Maths",
              "SaveLoad", "Scene", "Ships", "Sound", "StartUp", "Text", "Timer", "Video"]
# The groups several subsystems write, and what each holds.
SHARED = {
  "PlayerShip": "The player's ship, which several subsystems write: its energy, shields, temperature, altitude, angles and rates, "
                "velocity, missiles, laser, ECM, escape pod, view and docking computer, and whether it is dead.",
  "Messages": "The message line, which several subsystems post to: what it shows and for how many frames, and the warning's frames.",
  "Audio": "The speaker's effects and the music, which Sound starts and the timer's handler steps.",
  "Menus": "The docked menus' rows, prices and typed numbers, which the equipment, market and docked screens share.",
  "Disk": "The disk operation's error and its file, which saving and loading, the start-up and the screenshot share.",
  "Drawing": "What drawing draws into and with, which several subsystems write: the space view buffer, the colour, the paper, the "
             "sun's fringe, the text attribute, the screen's layout and the masking device's background.",
  "World": "The objects in space: the 36 ship slots (SpaceObject), and how many of them are in use.",
  "Flow": "Where the game is, at the title, docked or in flight, and the frame's clock, which several subsystems write.",
  "Jump": "The hyperspace and galactic jumps under way, which flight starts and Hyperspace counts down.",
  "Keyboard": "The keys held, which the keyboard's handler writes and flight clears.",
  UNUSED: "What nothing reads (Design/DataPartition.tsv's unused): kept, and written where the game writes it, for change 3b, which "
          "removes it with the turn probe.",
}


@dataclass
class Group:
  """A group of the partition: its name there, its struct, its member in GameData, its header and filter, and its members."""
  name: str
  members: list[Member] = field(default_factory=list)

  @property
  def type(self) -> str:
    return {UNUSED: "UnusedState"}.get(self.name, f"{self.name}State")

  @property
  def member(self) -> str:
    return {UNUSED: "unused"}.get(self.name, self.name[0].lower() + self.name[1:])

  @property
  def header(self) -> Path:
    return GAME_LOGIC / f"{self.type}.h"

  @property
  def filter(self) -> str:
    return self.name if self.name in SUBSYSTEMS else "State"

  def is_commander(self) -> bool:
    return [member.name for member in self.members] == [COMMANDER]

  def path(self, _member: Member) -> str:
    """The top-level member's path in GameData."""
    return self.member if self.is_commander() else f"{self.member}.{_member.name}"


def read_partition() -> list[dict[str, str]]:
  """Design/DataPartition.tsv's rows, in its order."""
  with PARTITION.open(encoding="utf-8", newline="") as file:
    lines = [line for line in file if not line.startswith("#")]
  return list(csv.DictReader(lines, delimiter="\t"))


def build_groups(_members: list[Member]) -> list[Group]:
  """Every top-level member the partition keeps in the state, in its group, in the 1987 order; the groups in the order GameData
  holds them: the subsystems', then the shared groups', and the unused last. A constant is in none: it is not in the state."""
  by_name = {member.name: member for member in _members}
  groups: dict[str, Group] = {}
  rows = read_partition()
  seen = set()
  for row in rows:
    name, group = row["member"], row["group"]
    member = by_name.get(name)
    if member is None:
      sys.exit(f"GenerateGameData: Design/DataPartition.tsv names {name}, which is not a member of the data segment")
    if name in seen:
      sys.exit(f"GenerateGameData: Design/DataPartition.tsv names {name} twice")
    seen.add(name)
    if row["address"] != f"DS:{member.offset:04X}" or int(row["bytes"]) != member.size:
      sys.exit(f"GenerateGameData: Design/DataPartition.tsv gives {name} as {row['address']}, {row['bytes']} bytes, and the "
               f"symbol table as DS:{member.offset:04X}, {member.size}")
    if group not in SUBSYSTEMS and group not in SHARED and group not in ("Commander", CONSTANT):
      sys.exit(f"GenerateGameData: {name}'s group, {group}, is neither a subsystem nor a shared group")
    if (group == "Commander") != (name == COMMANDER):
      sys.exit("GenerateGameData: the Commander group is commanderBlock, alone")
    if group != CONSTANT:
      groups.setdefault(group, Group(group)).members.append(member)
  missing = [member.name for member in _members if member.row is not None and member.name not in seen]
  if missing:
    sys.exit(f"GenerateGameData: Design/DataPartition.tsv does not place {', '.join(missing)}")
  order = [name for name in SUBSYSTEMS if name in groups] + [name for name in [*SHARED, "Commander"]
                                                             if name in groups and name != UNUSED]
  order += [name for name in (UNUSED,) if name in groups]
  for group in groups.values():
    group.members.sort(key=lambda _member: _member.offset)
  return [groups[name] for name in order]


def includes(_members: list[Member]) -> list[str]:
  """The headers the members' types need, besides Counted.h."""
  headers = set()
  for member in walk(_members):
    if member.name in RECORDS:
      headers.add(RECORDS[member.name][1])
    if member.name in WORD_TYPES:
      headers.add(WORD_TYPES[member.name][1])
  return sorted(headers)


def preamble(_file: str) -> list[str]:
  return [
    f"// GameLogic/{_file}",
    f"{GENERATED} from Design/Symbols.tsv and Design/DataPartition.tsv. Do not edit: change",
    "// them and run the tool again (D38).",
    "#pragma once",
    "",
  ]


def wrap(_text: str, _prefix: str) -> list[str]:
  """_text as comment lines that start with _prefix, each at most COLUMN_LIMIT characters."""
  lines, line = [], _prefix
  for word in _text.split():
    if len(line) + 1 + len(word) > COLUMN_LIMIT and line != _prefix:
      lines.append(line)
      line = _prefix
    line = f"{line} {word}" if line != _prefix else f"{_prefix} {word}"
  lines.append(line)
  return lines


def generate_group(_group: Group) -> str:
  """A group's header: its struct, its members in their 1987 order."""
  if _group.is_commander():
    return generate_commander(_group.members[0])
  out = preamble(f"{_group.type}.h")
  out += [f'#include "{header}"' for header in ["Counted.h", *includes(_group.members)]]
  out += ["", "#include <cstdint>", "#include <type_traits>", "", "namespace Elite", "{", ""]
  if _group.name in SHARED:
    description = SHARED[_group.name]
  else:
    description = (f"What {_group.name}.cpp, alone, writes of the game's state (D38), and its own: read elsewhere at most, never "
                   "written.")
  out += wrap(f"{description} Its members are the 1987 data segment's, by their names, in their order there "
              "(Design/DataPartition.tsv); the GameState holds it in GameData, in its counted block.", "///")
  out += [f"struct {_group.type}", "{"]
  emit_copy_rule(_group.type, "  ", out)
  emit_members(_group.members, "  ", out)
  out += [
    "};",
    "",
    f"static_assert(std::is_trivially_copyable_v<{_group.type}> && std::is_standard_layout_v<{_group.type}>);",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def generate_commander(_block: Member) -> str:
  """CommanderState: commanderBlock, whole, at its 1987 layout, each member's offset in it asserted."""
  out = preamble("CommanderState.h")
  out += ['#include "Counted.h"', "", "#include <cstddef>", "#include <cstdint>", "#include <type_traits>", ""]
  out += [
    "// A member's offset in CommanderState, as a constant expression wherever this header is read; under Clang the builtin, for",
    "// the reason GameData.h gives for GAME_DATA_OFFSET.",
    "#if defined(__clang__)",
    "#   define COMMANDER_STATE_OFFSET(member) __builtin_offsetof(::Elite::CommanderState, member)",
    "#else",
    "#   define COMMANDER_STATE_OFFSET(member) offsetof(::Elite::CommanderState, member)",
    "#endif",
    "",
    "namespace Elite",
    "{",
    "",
    "#pragma pack(push, 1)",
    "",
  ]
  out += wrap(f"The commander (D38): commanderBlock, DS:{_block.offset:04X}-{_block.end - 1:04X} in 1987, kept whole at its 1987 "
              f"layout, every member at its offset there. It is the .cdr file's format, {_block.size} bytes, which "
              "SaveCommanderFile writes from it and LoadCommanderFile reads over it, and Replays/*.cdr are files of it (ADR-008 "
              "item 8). Several subsystems write it.", "///")
  out += ["struct CommanderState", "{"]
  emit_copy_rule("CommanderState", "  ", out)
  emit_members(_block.children, "  ", out)
  out += ["};", "", "#pragma pack(pop)", "", "// Every member at its 1987 offset in the record, every struct at its size, and the record's."]
  for path, offset in offset_paths(_block.children, f"{_block.path}.", _block.offset):
    out.append(f"static_assert(COMMANDER_STATE_OFFSET({path}) == 0x{offset:02X});")
  for name, size in struct_types(_block.children, "CommanderState"):
    out.append(f"static_assert(sizeof({name}) == {size});")
  out += [
    f"static_assert(sizeof(CommanderState) == {_block.size});",
    "static_assert(std::is_trivially_copyable_v<CommanderState> && std::is_standard_layout_v<CommanderState>);",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def generate_game_data(_groups: list[Group]) -> str:
  out = preamble("GameData.h")
  out += [f'#include "{group.type}.h"' for group in _groups]
  out += [
    "",
    "#include <cstddef>",
    "#include <type_traits>",
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
  ]
  out += wrap("The game's state (D19, D38): the reference's data segment distributed, a struct of its groups' structs, each what one "
              "subsystem writes or what several share (Design/DataPartition.tsv), which the GameState holds in its counted block. "
              "Every byte and word is a Counted, of alignment 1, which counts its own changes in its GameState's change count: "
              "native code reads and writes members (`_state.Data().commander.fuel = fuel`), and passes one by reference to a "
              "helper that writes it. DATA_PLACES (DataPlaces.h) says where each member of the 1987 data segment is now.", "///")
  out += ["struct GameData", "{"]
  emit_copy_rule("GameData", "  ", out)
  for group in _groups:
    out.append(f"  {group.type} {group.member};")
  out += [
    "};",
    "",
    "static_assert(std::is_trivially_copyable_v<GameData> && std::is_standard_layout_v<GameData>);",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def generate_data_places(_groups: list[Group], _members: list[Member]) -> str:
  """DataPlaces.h: every top-level member of the 1987 data segment the partition places, and where it is now."""
  placed = {member.name: group for group in _groups for member in group.members}
  fates = {row["member"]: row["group"] for row in read_partition()}
  rows = []
  for member in _members:
    if member.name not in fates:
      continue
    group = placed.get(member.name)
    fate = "Constant" if fates[member.name] == CONSTANT else "Unused" if fates[member.name] == UNUSED else "State"
    place = f"GAME_DATA_OFFSET({group.path(member)})" if group is not None else "NO_PLACE"
    rows.append(f'  {{"{member.name}", 0x{member.offset:04X}, {member.size}, DataFate::{fate}, {place}}},')
  out = preamble("DataPlaces.h")
  out += ['#include "GameData.h"', "", "#include <array>", "#include <cstddef>", "#include <cstdint>", "#include <string_view>", ""]
  out += ["namespace Elite", "{", ""]
  out += [
    "/// What became of a top-level member of the 1987 data segment (Design/DataPartition.tsv).",
    "enum class DataFate : std::uint8_t",
    "{",
    "  State,    ///< a member of GameData, in its group",
    "  Constant, ///< nothing writes it: not in GameData, but its readers' constexpr tables (Tools/GenerateTables.py)",
    "  Unused,   ///< nothing reads it: a member of GameData, in UnusedState, which change 3b removes",
    "};",
    "",
    "/// No place in GameData: a constant, which the state does not hold.",
    "inline constexpr std::uint32_t NO_PLACE = 0xFFFFFFFF;",
    "",
    "/// A top-level member of the 1987 data segment, or a gap of it the game uses: its name, its offset and size there, what became",
    "/// of it, and its place in GameData now, or NO_PLACE.",
    "struct DataPlace",
    "{",
    "  std::string_view name;",
    "  std::uint16_t original;",
    "  std::uint16_t bytes;",
    "  DataFate fate;",
    "  std::uint32_t place;",
    "};",
    "",
  ]
  out += wrap("Every top-level member of the 1987 data segment, and every gap of it the game uses, in their 1987 order, and where "
              "each is now: what the reference's image loads by (ReferenceLoadImage), what the GameState finds the original's "
              "addresses by, and what the state digests' re-base checks member by member (GameLogicTests/StateDumps.h). The gaps "
              "it does not list are not in the state, and nor is a constant with no place: the original's addresses there are "
              "the reference's image's.", "///")
  out += [f"inline constexpr std::array<DataPlace, {len(rows)}> DATA_PLACES = {{{{", *rows, "}};", ""]
  out += [
    "/// The index in DATA_PLACES of the member named _name; past the last if there is none.",
    "[[nodiscard]] consteval std::size_t DataPlaceIndex(std::string_view _name)",
    "{",
    "  std::size_t index = 0;",
    "  while (index < DATA_PLACES.size() && DATA_PLACES[index].name != _name)",
    "  {",
    "    ++index;",
    "  }",
    "  return index;",
    "}",
    "",
  ]
  out += wrap("The 1987 offset of the top-level member named _name, a member of the data segment or a gap the game uses: the address "
              "the original holds for it, which a loop that waits still carries in its turns (ADR-015), and which native code "
              "that still reaches the data segment by the original's addresses uses (change 3b converts it). A name that is not "
              "one does not compile.", "///")
  out += [
    "[[nodiscard]] consteval std::uint16_t OriginalOffset(std::string_view _name)",
    "{",
    "  return DATA_PLACES[DataPlaceIndex(_name)].original;",
    "}",
    "",
    "/// The size in 1987 of the top-level member named _name, as OriginalOffset finds it.",
    "[[nodiscard]] consteval std::uint16_t OriginalBytes(std::string_view _name)",
    "{",
    "  return DATA_PLACES[DataPlaceIndex(_name)].bytes;",
    "}",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def measure(_groups: list[Group]) -> str:
  """What the groups hold, for the record (D38)."""
  members = sum(len(group.members) for group in _groups)
  size = sum(member.size for group in _groups for member in group.members)
  return f"{len(_groups)} groups, {members} top-level members, {size} bytes"


def outputs() -> tuple[dict[Path, str], list[Group]]:
  members = build_members(read_rows())
  groups = build_groups(members)
  texts = {group.header: generate_group(group) for group in groups}
  texts[GAME_DATA] = generate_game_data(groups)
  texts[DATA_PLACES] = generate_data_places(groups, members)
  return texts, groups


def stale(_texts: dict[Path, str]) -> list[Path]:
  """The headers this tool wrote once that it would not write now."""
  return [path for path in sorted(GAME_LOGIC.glob("*.h")) if path not in _texts and GENERATED in path.read_text(encoding="utf-8")]


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--check", action="store_true", help="fail if a header in the tree is out of date")
  arguments = parser.parse_args()
  texts, groups = outputs()
  if arguments.check:
    out_of_date = [path for path, text in texts.items() if (path.read_bytes().decode("utf-8") if path.exists() else "") != text]
    for path in out_of_date:
      print(f"GenerateGameData: {path.relative_to(ROOT)} is out of date; run python Tools/GenerateGameData.py")
    for path in stale(texts):
      print(f"GenerateGameData: {path.relative_to(ROOT)} was generated and is not now; delete it")
    if out_of_date or stale(texts):
      return 1
    print(f"GenerateGameData: up to date ({measure(groups)})")
    return 0
  for path, text in texts.items():
    path.write_bytes(text.encode("utf-8"))
  for path in stale(texts):
    print(f"GenerateGameData: {path.relative_to(ROOT)} is left over from an earlier run; delete it")
  print(f"GenerateGameData: wrote {len(texts)} headers ({measure(groups)})")
  return 0


if __name__ == "__main__":
  sys.exit(main())
