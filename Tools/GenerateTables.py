#!/usr/bin/env python3
r"""Generate each subsystem's tables of the reference, GameLogic/<Subsystem>Tables.h, and GameLogic/Message.h from
GameLogic/ReferenceImage.cpp and Design/Symbols.tsv (D33, D38).

The reference keeps tables of addresses in its data segment: tables of pointers to texts, lists of pointers to phrases, the
blueprints by ship type, and the docked frame's corners as cells of video memory. Native code reads them as what they mean,
from the typed tables this writes, rather than by following the addresses through the data segment. The bytes are the
reference's: this reads the checked-in image (ReferenceImage.cpp), by the address each row of it is commented with, never
ELITES.EXE, and follows each table's pointers there.

  text tables          std::array<std::string_view, N>: each entry the text its pointer points at, up to its NUL
  descriptionPhraseLists
                       DESCRIPTION_PHRASES, every phrase a list points at, in the order of their addresses, and
                       DESCRIPTION_PHRASE_LISTS, each list's five phrases as indexes into it
  blueprintTable       BLUEPRINT_DATA, the blueprints' bytes as the image holds them, and BLUEPRINTS, each type's
                       blueprint as its shape and the parts of its body, which are views of those bytes
  frameCorners         FRAME_CORNERS, each corner's row and column on the 40-column text page and its character
  messagePointer       Message (Message.h), the texts the message line shows, which the original stores the addresses of,
                       and MESSAGE_TEXTS, each one's text
  warningTexts         WARNING_MESSAGES, the four warnings' messages

A text the program writes into is a buffer, which native code reads from the data segment where it is written: its entry in
a table is empty, and says what writes it.

Each table goes in the header of the subsystem that reads it (D38): GalaxyTables.h for Galaxy.cpp, SceneTables.h for
Scene.cpp, and so on, SUBSYSTEMS lists them. One .cpp reads each, so its tables are in an unnamed namespace there, and nothing
else reaches them: the check fails if any other file includes the header. Message.h is the one header every file may read,
since GameData's message words hold its enumeration. The types a table's entries have that other files name, Blueprint and
BlueprintShape, are the subsystem's own header's (Scene.h).

A text is written as a string literal: printable ASCII as it is, any other byte as a three-digit octal escape, and the
literal broken after an escape that a digit follows, so that no compiler reads the digit as part of it. Bytes are written
eight to a row, each row ending in its address in a comment, which keeps clang-format's layout cheap and stable
(Tools/GenerateReference.py measured why).

The headers are generated, never edited: run this again. With --check it writes nothing and fails if a header in the tree
is not what it would write, or is included by a file other than its subsystem's .cpp. Standard library only.

    python Tools/GenerateTables.py [--check]
"""

import argparse
import csv
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "GameLogic" / "ReferenceImage.cpp"
SYMBOLS = ROOT / "Design" / "Symbols.tsv"
GAME_LOGIC = ROOT / "GameLogic"
MESSAGE_HEADER = GAME_LOGIC / "Message.h"
# The projects whose files may include a generated header, which the check looks through.
PROJECTS = ["GameLogic", "GameLogicTests", "ReferenceRunner", "Outpost"]

INDENT = "  "
ROW_BYTES = 8
COMMENT_COLUMN = len(INDENT) + ROW_BYTES * len("0x00, ")
ROW_ADDRESS = re.compile(r"//\s*(CS|DS):([0-9A-F]{4})")
BYTE = re.compile(r"0x([0-9A-F]{2})")

# The subsystems that read generated tables, each the stem of its .cpp and of its header, <Subsystem>Tables.h.
SUBSYSTEMS = ["Docked", "Equipment", "Flight", "Galaxy", "SaveLoad", "Scene", "Text"]
# The tables of pointers to texts, each with the constant it becomes and the subsystem that reads it.
TEXT_TABLES = [
  ("economyNames", "ECONOMY_NAMES", "Galaxy"),
  ("governmentNames", "GOVERNMENT_NAMES", "Galaxy"),
  ("speciesSizeNames", "SPECIES_SIZE_NAMES", "Galaxy"),
  ("speciesColorNames", "SPECIES_COLOR_NAMES", "Galaxy"),
  ("speciesTraitNames", "SPECIES_TRAIT_NAMES", "Galaxy"),
  ("speciesTypeNames", "SPECIES_TYPE_NAMES", "Galaxy"),
  ("laserTypeNames", "LASER_TYPE_NAMES", "Equipment"),
  ("laserMountNames", "LASER_MOUNT_NAMES", "Docked"),
  ("legalStatusNames", "LEGAL_STATUS_NAMES", "Docked"),
  ("ratingNames", "RATING_NAMES", "Docked"),
  ("equipmentNames", "EQUIPMENT_NAMES", "Docked"),
  ("inputDeviceNames", "INPUT_DEVICE_NAMES", "SaveLoad"),
  ("diskOperationNames", "DISK_OPERATION_NAMES", "SaveLoad"),
]
# The texts of these tables that the program writes, which native code reads from the data segment where it writes them: the
# entry, and what writes it.
BUFFER_ENTRIES = {
  ("equipmentNames", 0): "a buffer: the status screen writes the missiles' count at its '?' (data84F5)",
}
# The texts the message line shows, each a row of the symbol table: what native code posts in messagePointer. The four
# warnings, warningTexts' entries, join them, named for the checks warningChecks lists in the same order.
MESSAGES = [
  "alienItemsText", "arrivalGalaxyText", "arrivalSystemText", "autoDockMessage", "bountyText", "cargoBayFullText",
  "dockingComputerOffMessage", "dockingComputerOnMessage", "dockingDeniedMessage", "ecmActiveMessage", "equipmentLossMessage",
  "escapePodRetrievedText", "frontViewText", "fuelLeakText", "fuelScoopsActiveText", "galacticDriveReadyMessage",
  "gameOverMessage", "gamePausedMessage", "hyperspaceCountdownMessage", "hyperspaceEngagedText", "hyperspaceJammedMessage",
  "idIndicatorMessage", "jumpDriveEngagedMessage", "jumpDriveMassLockedMessage", "jumpDriveVelocityLockedMessage",
  "leavingStationMessage", "leftViewText", "maskingDeviceText", "metalAlloysText", "missileJammedMessage",
  "missileLaunchedMessage", "missileLockedMessage", "missilePrimedMessage", "missileTargetDestroyedMessage",
  "missileUnarmedMessage", "navCompErrorMessage", "navCompRepairedMessage", "ninthGalaxyText", "noSystemSelectedMessage",
  "notEnoughFuelMessage", "outOfRangeMessage", "preciousMetalsText", "rearViewText", "rightViewText",
  "scoopRetrievalInactiveText", "scoopedCargoText", "shipIdentityText", "stationOutOfRangeMessage", "witchSpaceArrivalText",
]
# The messages the program writes into, which MessageText reads from the data segment: what writes each.
MESSAGE_BUFFERS = {
  "arrivalGalaxyText": "the arrival writes its digit",
  "bountyText": "ShowBountyMessage writes it",
  "hyperspaceCountdownMessage": "the countdown writes digits after it",
  "scoopedCargoText": "TryScoopObject writes it",
  "shipIdentityText": "ShowShipIdentity writes it",
}
PHRASE_LIST_ENTRIES = 5
PHRASE_CODE = 0x80  # the first byte ExpandDescriptionText expands as a phrase
LAST_CONTROL_CODE = 6  # StopCapitalizing: textControlCodes' last

# A blueprint's first word names its handler, which builds the vertices its program starts from.
BOX_HANDLER = 0x377A  # BuildBoxCornerVertices
DODO_HANDLER = 0x38BF  # BuildDodoVertices
FACE_TRIANGLE = 0x80
TRIANGLE_ITEM_BYTES = 4
# What the blueprints index, which every index they hold stays inside, so that native code can index them: vertexBuffer's 42
# vertices of 6 bytes, which RunVertexProgram's operations name in their low six bits and which ProjectVertices turns into
# points of 4 bytes in place, which the edges and faces name by their byte offsets; and faceFillPatterns' 16 words, which a
# triangle item names by its even byte offset.
VERTEX_BUFFER_BYTES = 42 * 6
VERTEX_INDEX_BITS = 0x3F
EDGE_POINT_BITS = 0xFCFC
FILL_PATTERN_BYTES = 16 * 2

TEXT_ROW_BYTES = 0x50  # a row of the 40-column text page


def fail(_message: str) -> None:
  sys.exit(f"GenerateTables: {_message}")


@dataclass
class Symbol:
  segment: str
  offset: int
  kind: str
  name: str
  size: int | None
  notes: str


def read_symbols() -> dict[str, Symbol]:
  with SYMBOLS.open(encoding="utf-8", newline="") as file:
    lines = [line for line in file if not line.startswith("#")]
  symbols = {}
  for row in csv.DictReader(lines, delimiter="\t"):
    segment, _, offset = row["address"].strip().partition(":")
    size = row["size"].strip()
    symbols[row["name"].strip()] = Symbol(segment, int(offset, 16), row["kind"], row["name"].strip(), int(size) if size else None,
                                          row["notes"].strip())
  return symbols


def read_image() -> dict[str, dict[int, int]]:
  """The image's bytes by segment and address, from the address each row of ReferenceImage.cpp is commented with."""
  segments: dict[str, dict[int, int]] = {"CS": {}, "DS": {}}
  for line in IMAGE.read_text(encoding="utf-8").splitlines():
    code, _, comment = line.partition("//")
    match = ROW_ADDRESS.match("//" + comment) if comment else None
    if match is None:
      continue
    segment, address = match.group(1), int(match.group(2), 16)
    for index, value in enumerate(BYTE.findall(code)):
      segments[segment][address + index] = int(value, 16)
  return segments


class Image:
  def __init__(self, _segments: dict[str, dict[int, int]]):
    self.segments = _segments

  def byte(self, _segment: str, _offset: int) -> int:
    value = self.segments[_segment].get(_offset)
    if value is None:
      fail(f"{_segment}:{_offset:04X} is not in {IMAGE.relative_to(ROOT)}")
    return value

  def bytes(self, _segment: str, _offset: int, _count: int) -> bytes:
    return bytes(self.byte(_segment, _offset + index) for index in range(_count))

  def word(self, _offset: int) -> int:
    return self.byte("DS", _offset) | (self.byte("DS", _offset + 1) << 8)

  def text(self, _offset: int) -> bytes:
    """The text at DS:_offset, up to its NUL."""
    out = bytearray()
    while (value := self.byte("DS", _offset + len(out))) != 0:
      out.append(value)
    return bytes(out)


def aligned(_lines: list[tuple[str, str]]) -> list[str]:
  """Code and trailing comment pairs, the comments aligned a space past the longest code, as clang-format aligns a run of them."""
  column = max(len(code) for code, _ in _lines) + 1
  return [code.ljust(column) + comment for code, comment in _lines]


def literal(_text: bytes) -> str:
  """_text as a C++ narrow string literal, broken after an escape that a digit follows."""
  parts = ['"']
  for index, value in enumerate(_text):
    if 0x20 <= value < 0x7F and value not in (0x22, 0x5C):
      parts.append(chr(value))
      continue
    parts.append(f"\\{value:03o}")
    if index + 1 < len(_text) and chr(_text[index + 1]).isdigit():
      parts.append('" "')
  parts.append('"')
  return "".join(parts)


def text_view(_text: bytes) -> str:
  return f"std::string_view{{{literal(_text)}}}" if _text else "std::string_view{}"


def byte_rows(_data: bytes, _segment: str, _start: int, _labels: dict[int, str]) -> list[str]:
  """_data as rows of up to eight bytes, each ending in its address, broken where a label begins, which its comment names."""
  rows = []
  at = 0
  while at < len(_data):
    end = min(len(_data), (at // ROW_BYTES + 1) * ROW_BYTES)
    end = next((label for label in sorted(_labels) if at < label < end), end)
    body = INDENT + ", ".join(f"0x{value:02X}" for value in _data[at:end]) + ","
    comment = f"// {_segment}:{_start + at:04X}"
    if at in _labels:
      comment += " " + _labels[at]
    rows.append(body.ljust(COMMENT_COLUMN) + comment)
    at = end
  return rows


def text_tables(_image: Image, _symbols: dict[str, Symbol], _subsystem: str) -> list[str]:
  out = []
  for name, constant, subsystem in TEXT_TABLES:
    if subsystem != _subsystem:
      continue
    symbol = _symbols[name]
    entries = [_image.word(symbol.offset + 2 * index) for index in range(symbol.size)]
    out += [
      f"/// {name} (DS:{symbol.offset:04X}): each pointer's text, up to its NUL.",
      f"constexpr std::array<std::string_view, {len(entries)}> {constant} = {{",
    ]
    out += aligned([(f"{INDENT}{text_view(b'' if (name, index) in BUFFER_ENTRIES else _image.text(entry))},",
                     f"// DS:{entry:04X}" + (f", {BUFFER_ENTRIES[(name, index)]}" if (name, index) in BUFFER_ENTRIES else ""))
                    for index, entry in enumerate(entries)])
    out += ["};", ""]
  return out


def phrase_tables(_image: Image, _symbols: dict[str, Symbol]) -> list[str]:
  symbol = _symbols["descriptionPhraseLists"]
  lists = [_image.word(symbol.offset + 2 * index) for index in range(symbol.size)]
  pointers = [[_image.word(entry + 2 * pick) for pick in range(PHRASE_LIST_ENTRIES)] for entry in lists]
  phrases = sorted({pointer for entry in pointers for pointer in entry})
  if len(phrases) > 0x100:
    fail("more phrases than a byte can index")
  # ExpandDescriptionText runs control codes 1-6 and expands phrase codes from 80h by these lists. No text it reads, the
  # phrases and descriptionTemplate, holds another code, which the original would run into the words after the tables.
  template = _symbols["descriptionTemplate"]
  for text in [_image.text(pointer) for pointer in phrases] + [_image.text(template.offset)]:
    for code in text:
      if LAST_CONTROL_CODE < code < 0x20 or code >= PHRASE_CODE + len(lists):
        fail(f"a description text holds the code {code:02X}h, which no handler and no list stands for")
  index = {pointer: number for number, pointer in enumerate(phrases)}
  out = [
    "/// Every phrase descriptionPhraseLists' lists point at, in the order of their addresses: its text up to its NUL, which",
    "/// may hold the control codes (1-31) and phrase codes (80h on) ExpandDescriptionText expands.",
    f"inline constexpr std::array<std::string_view, {len(phrases)}> DESCRIPTION_PHRASES = {{",
  ]
  out += aligned([(f"{INDENT}{text_view(_image.text(pointer))},", f"// {number} DS:{pointer:04X}") for number, pointer in enumerate(phrases)])
  out += [
    "};",
    "",
    f"/// descriptionPhraseLists (DS:{symbol.offset:04X}): for each phrase code from 80h, the five phrases it picks from, as",
    "/// indexes into DESCRIPTION_PHRASES.",
    f"inline constexpr std::array<std::array<std::uint8_t, {PHRASE_LIST_ENTRIES}>, {len(lists)}> DESCRIPTION_PHRASE_LISTS = {{{{",
  ]
  out += aligned([(f"{INDENT}{{{', '.join(str(index[pointer]) for pointer in picks)}}},", f"// {PHRASE_CODE + code:02X}h DS:{entry:04X}")
                  for code, (entry, picks) in enumerate(zip(lists, pointers))])
  out += ["}};", ""]
  return out


@dataclass
class Blueprint:
  start: int
  shape: str
  half_width: int
  extents: int
  program: tuple[int, int]  # offset and length in the image
  projected: int
  fixed_edges: tuple[int, int]
  face_edges: tuple[int, int]
  faces: int
  face_list: tuple[int, int]
  end: int


def parse_blueprint(_image: Image, _start: int) -> Blueprint:
  """A blueprint as RunBlueprintHandler and RenderBlueprint read it."""
  handler = _image.word(_start)
  if handler not in (BOX_HANDLER, DODO_HANDLER):
    fail(f"the blueprint at DS:{_start:04X} names a handler at CS:{handler:04X}, neither box nor Dodo")
  at = _start + 3
  extents = 0
  if handler == BOX_HANDLER:
    extents = _image.word(at)
    at += 2
  operations = _image.byte("DS", at)
  program = (at + 1, operations)
  at += 1 + operations
  projected = _image.byte("DS", at)
  at += 1
  fixed = _image.byte("DS", at)
  fixed_edges = (at + 1, 2 * fixed)
  at += 1 + 2 * fixed
  face_edge_count = _image.byte("DS", at)
  face_edges = (at + 1, 2 * face_edge_count)
  at += 1 + 2 * face_edge_count
  faces = _image.byte("DS", at)
  at += 1
  list_start = at

  def check(_inside: bool, _what: str) -> None:
    if not _inside:
      fail(f"the blueprint at DS:{_start:04X} has {_what} outside what it indexes")

  for operation in _image.bytes("DS", program[0], program[1]):
    check(6 * (operation & VERTEX_INDEX_BITS) + 6 <= VERTEX_BUFFER_BYTES, "a vertex")
  for edge in range(face_edge_count):
    points = _image.word(face_edges[0] + 2 * edge) & EDGE_POINT_BITS
    check(max(points & 0xFF, points >> 8) + 4 <= VERTEX_BUFFER_BYTES, "an edge's point")
  for _ in range(faces):
    check(max(_image.bytes("DS", at, 3)) + 4 <= VERTEX_BUFFER_BYTES, "a face's point")
    at += 3
    left = _image.byte("DS", at)
    at += 1
    while left > 0:
      item = _image.byte("DS", at)
      at += 1
      if item & FACE_TRIANGLE:
        fill = item & ~FACE_TRIANGLE
        check(fill % 2 == 0 and fill + 2 <= FILL_PATTERN_BYTES, "a fill pattern")
        check(max((point << 1) & 0xFF for point in _image.bytes("DS", at, 3)) + 4 <= VERTEX_BUFFER_BYTES, "a triangle's point")
        at += 3
        left -= TRIANGLE_ITEM_BYTES
      else:
        check(item + 2 <= 2 * face_edge_count, "an edge item")
        left -= 1
      if left < 0:
        fail(f"a face of the blueprint at DS:{_start:04X} does not end where its count says")
  return Blueprint(_start, "Box" if handler == BOX_HANDLER else "Dodo", _image.byte("DS", _start + 2), extents, program, projected,
                   fixed_edges, face_edges, faces, (list_start, at - list_start), at)


def blueprint_tables(_image: Image, _symbols: dict[str, Symbol]) -> list[str]:
  table = _symbols["blueprintTable"]
  container = _symbols["blueprints"]
  starts = [_image.word(table.offset + 2 * index) for index in range(table.size)]
  blueprints = [parse_blueprint(_image, start) for start in starts]
  first, end = container.offset, container.offset + container.size
  if min(starts) != first or max(blueprint.end for blueprint in blueprints) != end:
    fail("the blueprints do not fill the blueprints container")
  names = {symbol.offset: symbol.name for symbol in _symbols.values() if symbol.segment == "DS" and first <= symbol.offset < end}
  type_names = _symbols["shipTypeNames"]
  labels = {}
  for type_number, blueprint in enumerate(blueprints):
    label = names.get(blueprint.start)
    if label is None or label == "blueprints":
      label = _image.text(type_names.offset + 12 * type_number).decode("ascii").strip()
    labels[blueprint.start - first] = f"type {type_number}: {label}"
  data = _image.bytes("DS", first, end - first)

  def view(_part: tuple[int, int]) -> str:
    offset, length = _part
    return f"BlueprintPart({offset - first}, {length})"

  out = [
    f"/// blueprints (DS:{first:04X}-{end - 1:04X}): the 30 blueprints' bytes, back to back, as the image holds them.",
    f"constexpr std::array<std::uint8_t, {len(data)}> BLUEPRINT_DATA = {{",
    *byte_rows(data, "DS", first, labels),
    "};",
    "",
    "/// _length bytes of BLUEPRINT_DATA from _offset.",
    "[[nodiscard]] consteval std::span<const std::uint8_t> BlueprintPart(std::size_t _offset, std::size_t _length)",
    "{",
    f"{INDENT}return std::span<const std::uint8_t>(BLUEPRINT_DATA).subspan(_offset, _length);",
    "}",
    "",
    f"/// blueprintTable (DS:{table.offset:04X}): each ship type's blueprint, its parts views of BLUEPRINT_DATA.",
    f"constexpr std::array<Blueprint, {len(blueprints)}> BLUEPRINTS = {{{{",
  ]
  for type_number, blueprint in enumerate(blueprints):
    out += [
      f"{INDENT}// type {type_number}, DS:{blueprint.start:04X}: {labels[blueprint.start - first].split(': ', 1)[1]}",
      f"{INDENT}{{.shape = BlueprintShape::{blueprint.shape},",
      f"{INDENT} .halfWidth = {blueprint.half_width},",
      f"{INDENT} .extents = 0x{blueprint.extents:04X},",
      f"{INDENT} .vertexProgram = {view(blueprint.program)},",
      f"{INDENT} .projectedVertices = {blueprint.projected},",
      f"{INDENT} .fixedEdges = {view(blueprint.fixed_edges)},",
      f"{INDENT} .faceEdges = {view(blueprint.face_edges)},",
      f"{INDENT} .faces = {blueprint.faces},",
      f"{INDENT} .faceList = {view(blueprint.face_list)}}},",
    ]
  out += ["}};", ""]
  return out


def frame_corners(_image: Image, _symbols: dict[str, Symbol]) -> list[str]:
  symbol = _symbols["frameCorners"]
  out = [
    f"/// frameCorners (DS:{symbol.offset:04X}): the docked frame's corners and tees, each a cell of the 40-column text page,",
    "/// as its row and column, and the CP437 character there.",
    f"constexpr std::array<FrameCorner, {symbol.size}> FRAME_CORNERS = {{{{",
  ]
  corners = []
  for index in range(symbol.size):
    at = symbol.offset + 3 * index
    cell = _image.word(at)
    if cell % 2 != 0:
      fail(f"frameCorners entry {index} is not a character cell")
    corners.append((f"{INDENT}{{{cell // TEXT_ROW_BYTES}, {cell % TEXT_ROW_BYTES // 2}, 0x{_image.byte('DS', at + 2):02X}}},",
                    f"// cell {cell:04X}h"))
  out += aligned(corners)
  out += ["}};", ""]
  return out


@dataclass
class Message:
  enumerator: str
  name: str  # the text's row, or the warning check's routine
  offset: int
  buffer: str | None


def messages(_symbols: dict[str, Symbol], _image: Image) -> list[Message]:
  """The texts the message line shows, in the order of their addresses, each named for its row less a last Text or Message."""
  found = []
  for name in MESSAGES:
    symbol = _symbols[name]
    if symbol.segment != "DS" or symbol.kind != "text":
      fail(f"{name} is not a text of the data segment")
    stem = re.sub(r"(Text|Message)$", "", name)
    found.append(Message(stem[0].upper() + stem[1:], name, symbol.offset, MESSAGE_BUFFERS.get(name)))
  checks = _symbols["warningChecks"]
  texts = _symbols["warningTexts"]
  routines = {symbol.offset: symbol.name for symbol in _symbols.values() if symbol.segment == "CS"}
  for index in range(texts.size):
    routine = routines[_image.word(checks.offset + 2 * index)]
    found.append(Message(routine.removeprefix("Check"), f"warningTexts[{index}]", _image.word(texts.offset + 2 * index), None))
  if len({message.enumerator for message in found}) != len(found):
    fail("two messages share a name")
  if set(MESSAGE_BUFFERS) - set(MESSAGES):
    fail("a message buffer is not a message")
  return sorted(found, key=lambda _message: _message.offset)


def generate_message(_messages: list[Message]) -> str:
  out = [
    "// GameLogic/Message.h",
    "// Generated by Tools/GenerateTables.py from GameLogic/ReferenceImage.cpp and Design/Symbols.tsv. Do not edit: run the",
    "// tool again (D33).",
    "#pragma once",
    "",
    "#include <cstdint>",
    "",
    "namespace Elite",
    "{",
    "",
    "/// A text the message line shows: what messagePointer, messageShown and warningMessage hold, where the original holds the",
    "/// text's address (D33, piece 3). Each is named for the text's row in the symbol table, or for its warning's check, in the",
    "/// order of their addresses. MessageText gives a message's text; MESSAGE_TEXTS holds the constant ones.",
    "enum class Message : std::uint16_t",
    "{",
    *aligned([(f"{INDENT}None,", "///< 0, as messageShown is when no message is shown")] +
             [(f"{INDENT}{message.enumerator},", f"///< {message.name}, DS:{message.offset:04X}") for message in _messages]),
    "};",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def message_texts(_image: Image, _messages: list[Message]) -> list[str]:
  out = [
    "/// Each Message's text, up to its NUL, by its value. A buffer's is empty: the program writes it, and MessageText reads it",
    "/// from the data segment.",
    f"constexpr std::array<std::string_view, {len(_messages) + 1}> MESSAGE_TEXTS = {{",
  ]
  rows = [(f"{INDENT}std::string_view{{}},", "// None")]
  for message in _messages:
    text = b"" if message.buffer else _image.text(message.offset)
    rows.append((f"{INDENT}{text_view(text)},", f"// {message.enumerator}" + (f", a buffer: {message.buffer}" if message.buffer else "")))
  out += aligned(rows)
  out += ["};", ""]
  return out


def warning_messages(_messages: list[Message]) -> list[str]:
  warnings = sorted((message for message in _messages if message.name.startswith("warningTexts[")), key=lambda _m: _m.name)
  return [
    "/// warningTexts: each warning's message, by the warning's number, the order of warningChecks' checks (SetWarning).",
    f"constexpr std::array<Message, {len(warnings)}> WARNING_MESSAGES = {{",
    *aligned([(f"{INDENT}Message::{message.enumerator},", f"// {message.name}") for message in warnings]),
    "};",
    "",
  ]


def subsystem_tables(_image: Image, _symbols: dict[str, Symbol], _messages: list[Message], _subsystem: str) -> tuple[list[str], list[str]]:
  """The headers a subsystem's tables need, and the tables."""
  out = text_tables(_image, _symbols, _subsystem)
  headers = ["<array>", "<string_view>"]
  if _subsystem == "Galaxy":
    out += phrase_tables(_image, _symbols)
    headers = ["<array>", "<cstdint>", "<string_view>"]
  elif _subsystem == "Docked":
    out = [
      "/// A corner or tee of a docked screen's frame (DrawDockedFrame): its cell on the 40-column text page and its character.",
      "struct FrameCorner",
      "{",
      f"{INDENT}std::uint8_t row;",
      f"{INDENT}std::uint8_t column;",
      f"{INDENT}std::uint8_t character; ///< CP437",
      "};",
      "",
      *out,
      *frame_corners(_image, _symbols),
    ]
    headers = ["<array>", "<cstdint>", "<string_view>"]
  elif _subsystem == "Scene":
    out += blueprint_tables(_image, _symbols)
    headers = ['"Scene.h"', "", "<array>", "<cstddef>", "<cstdint>", "<span>"]
  elif _subsystem == "Text":
    out += message_texts(_image, _messages)
    headers = ['"Message.h"', "", "<array>", "<string_view>"]
  elif _subsystem == "Flight":
    out += warning_messages(_messages)
    headers = ['"Message.h"', "", "<array>"]
  return headers, out


def generate_subsystem(_image: Image, _symbols: dict[str, Symbol], _messages: list[Message], _subsystem: str) -> str:
  headers, tables = subsystem_tables(_image, _symbols, _messages, _subsystem)
  out = [
    f"// GameLogic/{_subsystem}Tables.h",
    "// Generated by Tools/GenerateTables.py from GameLogic/ReferenceImage.cpp and Design/Symbols.tsv. Do not edit: run the",
    "// tool again (D33, D38).",
    "#pragma once",
    "",
    *[f"#include {header}" if header else "" for header in headers],
    "",
    "namespace Elite",
    "{",
    "",
    f"// {_subsystem}.cpp's tables of the reference's addresses, as what they mean (D33, piece 3): each entry what its address points",
    "// at, read from the reference's image (ReferenceImage.cpp). The tables' own words stay in the data segment, which nothing",
    f"// reads them from. {_subsystem}.cpp alone includes this, so they are in an unnamed namespace (D38).",
    "namespace",
    "{",
    "",
    *tables,
    "} // namespace",
    "",
    "} // namespace Elite",
    "",
  ]
  return "\r\n".join(out)


def foreign_includes() -> list[str]:
  """Every include of a subsystem's generated header by a file other than its subsystem's .cpp."""
  found = []
  for project in PROJECTS:
    for path in sorted((ROOT / project).glob("*.[ch]*")):
      if path.suffix not in (".h", ".cpp"):
        continue
      text = path.read_text(encoding="utf-8", errors="replace")
      for subsystem in SUBSYSTEMS:
        if f'#include "{subsystem}Tables.h"' in text and path != GAME_LOGIC / f"{subsystem}.cpp":
          found.append(f"{path.relative_to(ROOT)} includes {subsystem}Tables.h, which {subsystem}.cpp alone may read")
  return found


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--check", action="store_true", help="fail if the header in the tree is out of date")
  arguments = parser.parse_args()
  image = Image(read_image())
  symbols = read_symbols()
  found = messages(symbols, image)
  outputs = {GAME_LOGIC / f"{subsystem}Tables.h": generate_subsystem(image, symbols, found, subsystem) for subsystem in SUBSYSTEMS}
  outputs[MESSAGE_HEADER] = generate_message(found)
  if arguments.check:
    stale = [path for path, text in outputs.items() if (path.read_bytes().decode("utf-8") if path.exists() else "") != text]
    for path in stale:
      print(f"GenerateTables: {path.relative_to(ROOT)} is out of date; run python Tools/GenerateTables.py")
    foreign = foreign_includes()
    for line in foreign:
      print(f"GenerateTables: {line}")
    if stale or foreign:
      return 1
    print(f"GenerateTables: up to date ({len(SUBSYSTEMS)} subsystems' tables: {len(TEXT_TABLES)} text tables, the phrases, the "
          f"blueprints, the frame's corners and {len(found)} messages; each included by its own .cpp alone)")
    return 0
  for path, text in outputs.items():
    path.write_bytes(text.encode("utf-8"))
    print(f"GenerateTables: wrote {path.relative_to(ROOT)}")
  return 0


if __name__ == "__main__":
  sys.exit(main())
