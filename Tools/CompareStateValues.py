#!/usr/bin/env python3
r"""Every named member's value at every digest point, keyed by its name and not its offset, and two such sets compared: the
proof a change that moves the game's state gives before its state digests are re-based (D32, D38).

GameLogicTests dumps the game's state (OUTPOST_ELITE_DUMP_STATE, GameLogicTests/StateDumps.h): GameData's bytes for every
digest step of every replay and every known answer of every twin,

  DIR/replays/<replay>/<line>.bin    the digest step on that line of Replays/<replay>.replay
  DIR/twins/<twin>/<ordinal>.bin     the twin's known answer of that ordinal (KnownAnswers.txt)

This reads each member's bytes there by its name, wherever the dumping tree laid it out:

  the 1987 layout   a dump of 65,536 bytes with no DIR/layout.tsv beside it, as a tree before change 3 writes it: each member at
                    its offset in Design/Symbols.tsv, as Tools/GenerateGameData.py read it then
  the groups        DIR/layout.tsv beside the dumps, as a tree since change 3 writes it: each top-level member's name, its 1987
                    offset, its size and its place in GameData now (GameState's DATA_PLACES), in hex

A member is named as GameData named it in the 1987 layout: a top-level member (stardust, commanderBlock), a member nested in one
(commanderBlock.fuel), a gap the game uses (gap83FA), or a field of a ship slot (shipSlots[2].energy, SpaceObject.h). A nested
member keeps its place in its top-level member. A constant that a tree no longer holds in its state (Design/DataPartition.tsv's
`constant`) takes its value from the reference's image (GameLogic/ReferenceImage.cpp), which its generated table is.

  values DIR [--out OUT]   every point's values, `NAME HEX` a line, the bytes in the order they are stored: OUT/<kind>/<name>/
                           <point>.tsv, or all of them to standard output
  compare BASE NEW         every point of BASE has its twin in NEW and the other way round, and every member's value is the same
                           at each, but a member DataPartition.tsv calls `unused`, which is reported and allowed to differ. Prints
                           each member that differs and at how many points. Exit 1 on any other difference, a member only one
                           side has, or a point only one side has.

Standard library only.

    python Tools/CompareStateValues.py values DIR --out VALUES
    python Tools/CompareStateValues.py compare BASE NEW
"""

import argparse
import csv
import importlib.util
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PARTITION = ROOT / "Design" / "DataPartition.tsv"
SPACE_OBJECT = ROOT / "GameLogic" / "SpaceObject.h"
SEGMENT_BYTES = 0x10000
SLOT_BYTES = 0x40
SLOTS = 36
LAYOUT = "layout.tsv"


def load_tool(_name: str):
  """Tools/<_name>.py as a module."""
  spec = importlib.util.spec_from_file_location(_name, ROOT / "Tools" / f"{_name}.py")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def fail(_message: str) -> None:
  sys.exit(f"CompareStateValues: {_message}")


def read_partition() -> dict[str, str]:
  """Each member's group in Design/DataPartition.tsv."""
  with PARTITION.open(encoding="utf-8", newline="") as file:
    lines = [line for line in file if not line.startswith("#")]
  return {row["member"]: row["group"] for row in csv.DictReader(lines, delimiter="\t")}


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


@dataclass
class Named:
  """A named member: its top-level member, and where it lies in it."""
  name: str
  top: str
  into: int
  size: int


class Names:
  """Every member by name, in the 1987 layout's terms: the top-level members with their 1987 offsets and sizes, and every
  name with the top-level member it lies in."""

  def __init__(self):
    generator = load_tool("GenerateGameData")
    members = generator.build_members(generator.read_rows())
    self.partition = read_partition()
    self.tops: dict[str, tuple[int, int]] = {}
    self.names: list[Named] = []
    slot_fields = read_slot_fields()
    for member in members:
      if member.row is None and member.name not in self.partition:
        continue
      self.tops[member.name] = (member.offset, member.size)
      self.names.append(Named(member.name, member.name, 0, member.size))
      if member.name == "shipSlots":
        for slot in range(SLOTS):
          for field, (offset, size) in slot_fields.items():
            self.names.append(Named(f"shipSlots[{slot}].{field}", member.name, slot * SLOT_BYTES + offset, size))
        continue
      for child in generator.walk(member.children):
        if child.row is not None:
          self.names.append(Named(child.path, member.name, child.offset - member.offset, child.size))
    missing = sorted(set(self.partition) - set(self.tops))
    if missing:
      fail(f"Design/DataPartition.tsv names members GameData did not have: {', '.join(missing)}")
    self.top = {named.name: named.top for named in self.names}
    self.image: bytes | None = None

  def image_bytes(self) -> bytes:
    """The data segment as the reference's image holds it, zeros past it."""
    if self.image is None:
      segments = load_tool("GenerateTables").read_image()
      data = bytearray(SEGMENT_BYTES)
      for offset, value in segments["DS"].items():
        data[offset] = value
      self.image = bytes(data)
    return self.image


class Layout:
  """Where a dump directory's tree put each top-level member: its 1987 offset, or its place in the groups (DIR/layout.tsv)."""

  def __init__(self, _directory: Path, _names: Names):
    self.names = _names
    path = _directory / LAYOUT
    self.places: dict[str, int] = {}
    self.grouped = path.exists()
    if not self.grouped:
      self.places = {name: offset for name, (offset, _) in _names.tops.items()}
      self.size = SEGMENT_BYTES
      return
    self.size = 0
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
      parts = line.split("\t")
      if len(parts) == 2 and parts[0] == "#bytes":
        self.size = int(parts[1], 16)
        continue
      if len(parts) != 4:
        fail(f"{path} line {number} is not NAME ORIGINAL BYTES PLACE")
      name, original, size, place = parts
      if name not in _names.tops:
        fail(f"{path} line {number}: {name} was not a member of GameData")
      if _names.tops[name] != (int(original, 16), int(size, 16)):
        fail(f"{path} line {number}: {name} was at {_names.tops[name][0]:04X}, {_names.tops[name][1]} bytes, in the 1987 layout")
      if place != "-":
        self.places[name] = int(place, 16)
    if self.size == 0:
      fail(f"{path} gives no #bytes line, GameData's size")

  def values(self, _data: bytes) -> dict[str, str]:
    """Every name's bytes in _data, as hex."""
    if len(_data) != self.size:
      fail(f"a dump of {len(_data)} bytes, where GameData's are {self.size}")
    image = None
    found = {}
    for named in self.names.names:
      place = self.places.get(named.top)
      if place is not None:
        start = place + named.into
        found[named.name] = _data[start:start + named.size].hex().upper()
        continue
      if self.names.partition.get(named.top) != "constant":
        continue
      image = image or self.names.image_bytes()
      start = self.names.tops[named.top][0] + named.into
      found[named.name] = image[start:start + named.size].hex().upper()
    return found


def dumps(_directory: Path) -> dict[str, Path]:
  return {path.relative_to(_directory).with_suffix("").as_posix(): path for path in sorted(_directory.rglob("*.bin"))}


def values(_names: Names, _directory: Path, _out: Path | None) -> int:
  layout = Layout(_directory, _names)
  for point, path in dumps(_directory).items():
    lines = [f"{name}\t{value}" for name, value in layout.values(path.read_bytes()).items()]
    if _out is None:
      sys.stdout.write("".join(f"{point}\t{line}\n" for line in lines))
      continue
    target = _out / f"{point}.tsv"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines) + "\n", encoding="utf-8")
  return 0


def compare(_names: Names, _base: Path, _new: Path) -> int:
  base_layout, new_layout = Layout(_base, _names), Layout(_new, _names)
  base, new = dumps(_base), dumps(_new)
  failed = False
  for point in sorted(set(base) ^ set(new)):
    print(f"only in {'BASE' if point in base else 'NEW'}: {point}")
    failed = True
  differ: dict[str, int] = {}
  compared = 0
  for point in sorted(set(base) & set(new)):
    before = base_layout.values(base[point].read_bytes())
    after = new_layout.values(new[point].read_bytes())
    for name in sorted(set(before) ^ set(after)):
      print(f"{point}: {name} only in {'BASE' if name in before else 'NEW'}")
      failed = True
    for name in before.keys() & after.keys():
      compared += 1
      if before[name] != after[name]:
        if name not in differ and _names.partition.get(_names.top[name]) != "unused":
          print(f"{point}: {name} is {after[name]}, and was {before[name]}")
        differ[name] = differ.get(name, 0) + 1
  unused = {name: count for name, count in differ.items() if _names.partition.get(_names.top[name]) == "unused"}
  used = {name: count for name, count in differ.items() if name not in unused}
  points = len(set(base) & set(new))
  print(f"{points} points compared, {compared} values; base laid out {'by group' if base_layout.grouped else 'as in 1987'}, "
        f"new {'by group' if new_layout.grouped else 'as in 1987'}")
  for name, count in sorted(used.items()):
    print(f"  {name}: differs at {count} points")
  for name, count in sorted(unused.items()):
    print(f"  {name} (unused): differs at {count} points")
  print(f"{len(used)} members differ, {len(unused)} unused members differ")
  return 1 if failed or used else 0


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("command", choices=["values", "compare"])
  parser.add_argument("directories", nargs="+", type=Path, help="values: DIR; compare: BASE and NEW")
  parser.add_argument("--out", type=Path, help="values: the directory to write, else standard output")
  arguments = parser.parse_args()
  names = Names()
  if arguments.command == "values":
    if len(arguments.directories) != 1:
      parser.error("values takes DIR")
    return values(names, arguments.directories[0], arguments.out)
  if len(arguments.directories) != 2:
    parser.error("compare takes BASE and NEW")
  return compare(names, arguments.directories[0], arguments.directories[1])


if __name__ == "__main__":
  sys.exit(main())
