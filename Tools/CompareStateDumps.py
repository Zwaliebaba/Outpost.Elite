#!/usr/bin/env python3
r"""Compare two sets of data-segment dumps, with named fields masked: the proof a change that converts stored words gives before
its state digests are re-based (D32, D33 piece 3).

A change that turns a stored address into an index moves the words it converts, and so every state digest that hashes them.
Before the digests are re-based it must show that nothing else moved: that at every point the corpus and the twins take a
digest, the data segment holds the same bytes as before the change, but in the fields it converted. GameLogicTests writes
the dumps (OUTPOST_ELITE_DUMP_STATE, GameLogicTests/StateDumps.h): one file of the data segment's 65,536 bytes for every
digest step of every replay and every known answer of every twin,

  DIR/replays/<replay>/<line>.bin    the digest step on that line of Replays/<replay>.replay
  DIR/twins/<twin>/<ordinal>.bin     the twin's known answer of that ordinal (KnownAnswers.txt)

A tree before the change has no dump mode; one is added to a copy of it by hand, as the change's own writes them.

  compare BASE NEW --field F ...   every file of BASE has its twin in NEW and the other way round; every byte that differs lies
                                   in one of the fields. Prints, for each field, at how many points it differs, and every other
                                   difference, by the member of GameData that holds it. Exit 1 on any other difference, or on
                                   a file only one side has.
  mask --field F ... [--out FILE]  the fields as ranges, `START END NAME` in hex, a line each, end exclusive: the mask
                                   OUTPOST_ELITE_REBASE_DIGESTS's gate applies (GameLogicTests/StateDumps.h).

A field is named as one of:

  PATH                  a member of GameData by its path, as GameData.h nests it: messagePointer, commanderBlock.fuel
  PATH[*]+OFFSET:BYTES  BYTES bytes at OFFSET (hex) into every entry of a table member: chartItems[*]+4:2
  SpaceObject.FIELD     a field of the ship slots' record (SpaceObject.h) in every one of shipSlots' 36 slots

GameData's layout is read as Tools/GenerateGameData.py builds it from Design/Symbols.tsv, and SpaceObject's from the offsets
SpaceObject.h asserts, so the tool follows the tree it is run in. Standard library only.

    python Tools/CompareStateDumps.py compare BASE NEW --field messagePointer --field SpaceObject.target ...
    python Tools/CompareStateDumps.py mask --field messagePointer ... --out mask.txt
"""

import argparse
import importlib.util
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SPACE_OBJECT = ROOT / "GameLogic" / "SpaceObject.h"
SEGMENT_BYTES = 0x10000
SLOT_BYTES = 0x40
SLOTS = 36


def load_generator():
  """Tools/GenerateGameData.py as a module: its parser of the symbol table and its tree of GameData's members."""
  spec = importlib.util.spec_from_file_location("GenerateGameData", ROOT / "Tools" / "GenerateGameData.py")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


@dataclass
class Range:
  start: int
  end: int  # exclusive
  name: str


class Layout:
  """GameData's members by path, and SpaceObject's fields by name."""

  def __init__(self):
    generator = load_generator()
    self.generator = generator
    self.members = list(generator.walk(generator.build_members(generator.read_rows())))
    self.by_path = {member.path: member for member in self.members}
    self.slot_fields = self.read_slot_fields()
    ship_slots = self.by_path.get("shipSlots")
    if ship_slots is None or ship_slots.size != SLOTS * SLOT_BYTES:
      sys.exit("CompareStateDumps: GameData has no shipSlots of 36 records")
    self.slots_start = ship_slots.offset

  @staticmethod
  def read_slot_fields() -> dict[str, tuple[int, int]]:
    """SpaceObject's fields: the offset SpaceObject.h asserts for each, and its size, a byte for a Counted<std::uint8_t> and a
    word for any other Counted."""
    text = SPACE_OBJECT.read_text(encoding="utf-8")
    sizes = {name: 1 if kind == "std::uint8_t" else 2 for kind, name in re.findall(r"Counted<([\w:]+)> (\w+);", text)}
    fields = {}
    for assertion in re.findall(r"static_assert\(([^;]*)\);", text):
      for name, offset in re.findall(r"SPACE_OBJECT_OFFSET\((\w+)\) == (0x[0-9A-Fa-f]+)", assertion):
        fields[name] = (int(offset, 16), sizes[name])
    return fields

  def resolve(self, _field: str) -> list[Range]:
    if _field.startswith("SpaceObject."):
      name = _field[len("SpaceObject."):]
      if name not in self.slot_fields:
        sys.exit(f"CompareStateDumps: SpaceObject has no field {name}")
      offset, size = self.slot_fields[name]
      return [Range(self.slots_start + slot * SLOT_BYTES + offset, self.slots_start + slot * SLOT_BYTES + offset + size,
                    f"{_field}[{slot}]") for slot in range(SLOTS)]
    match = re.fullmatch(r"([\w.]+)\[\*\]\+([0-9A-Fa-f]+):(\d+)", _field)
    if match:
      member = self.member(match.group(1))
      entry = self.generator.element_bytes(member.row["contract"]) if member.row and member.row["kind"] == "table" else None
      if entry is None:
        sys.exit(f"CompareStateDumps: {member.path} is not a table of entries of a size")
      offset, size = int(match.group(2), 16), int(match.group(3))
      if offset + size > entry:
        sys.exit(f"CompareStateDumps: {_field} runs past an entry of {entry} bytes")
      return [Range(member.offset + index * entry + offset, member.offset + index * entry + offset + size, f"{member.path}[{index}]")
              for index in range(member.size // entry)]
    member = self.member(_field)
    return [Range(member.offset, member.end, member.path)]

  def member(self, _path: str):
    member = self.by_path.get(_path)
    if member is None:
      sys.exit(f"CompareStateDumps: GameData has no member {_path}")
    return member

  def holder(self, _offset: int) -> str:
    """The innermost member that holds the byte at _offset, as PATH+OFFSET."""
    holders = [member for member in self.members if member.offset <= _offset < member.end]
    if not holders:
      return f"DS:{_offset:04X}"
    member = holders[-1]
    return f"{member.path}+{_offset - member.offset:X}" if _offset != member.offset else member.path


def dumps(_directory: Path) -> dict[str, Path]:
  return {path.relative_to(_directory).as_posix(): path for path in sorted(_directory.rglob("*.bin"))}


def compare(_layout: Layout, _base: Path, _new: Path, _fields: list[str]) -> int:
  ranges = {field: _layout.resolve(field) for field in _fields}
  owner = {}
  for field, field_ranges in ranges.items():
    for item in field_ranges:
      for offset in range(item.start, item.end):
        owner.setdefault(offset, field)
  base, new = dumps(_base), dumps(_new)
  failed = False
  for name in sorted(set(base) ^ set(new)):
    print(f"only in {'BASE' if name in base else 'NEW'}: {name}")
    failed = True
  points = {field: 0 for field in _fields}
  bytes_differ = {field: 0 for field in _fields}
  unmasked = 0
  for name in sorted(set(base) & set(new)):
    before, after = base[name].read_bytes(), new[name].read_bytes()
    if len(before) != SEGMENT_BYTES or len(after) != SEGMENT_BYTES:
      print(f"{name}: not a data segment of {SEGMENT_BYTES} bytes")
      failed = True
      continue
    seen = set()
    for offset in (index for index in range(SEGMENT_BYTES) if before[index] != after[index]):
      field = owner.get(offset)
      if field is None:
        unmasked += 1
        if unmasked <= 50:
          print(f"{name}: DS:{offset:04X} {_layout.holder(offset)} is {after[offset]:02X}, and was {before[offset]:02X}")
        continue
      bytes_differ[field] += 1
      seen.add(field)
    for field in seen:
      points[field] += 1
  compared = len(set(base) & set(new))
  print(f"{compared} points compared ({len(base)} in BASE, {len(new)} in NEW)")
  for field in _fields:
    print(f"  {field}: differs at {points[field]} points, {bytes_differ[field]} bytes")
  print(f"{unmasked} bytes differ outside the fields")
  return 1 if failed or unmasked else 0


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("command", choices=["compare", "mask"])
  parser.add_argument("directories", nargs="*", type=Path, help="compare: BASE and NEW")
  parser.add_argument("--field", action="append", default=[], help="a field the change converted (repeatable)")
  parser.add_argument("--out", type=Path, help="mask: the file to write, else standard output")
  arguments = parser.parse_args()
  layout = Layout()
  if arguments.command == "mask":
    lines = [f"{item.start:04X} {item.end:04X} {item.name}" for field in arguments.field for item in layout.resolve(field)]
    text = "\n".join(lines) + "\n"
    if arguments.out:
      arguments.out.write_text(text, encoding="utf-8")
    else:
      sys.stdout.write(text)
    return 0
  if len(arguments.directories) != 2:
    parser.error("compare takes BASE and NEW")
  return compare(layout, arguments.directories[0], arguments.directories[1], arguments.field)


if __name__ == "__main__":
  sys.exit(main())
