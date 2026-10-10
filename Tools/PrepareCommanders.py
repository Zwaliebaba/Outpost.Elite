#!/usr/bin/env python3
"""Write the prepared commanders that replays load (ADR-008), from a commander the reference saved itself.

  python Tools/PrepareCommanders.py [--check]

Most of what the replay corpus has to reach needs credits, equipment or a place the starting commander
does not have, and the game takes a commander only from a file. A replay's `file NAME SOURCE` step
(GameLogic/Replay.h) puts one of these into DOS's directory, and the disc menu then loads it as the
game's own save. --check writes nothing and fails if a file in Replays/ is not what this would write.

The commander file is the game's own data, verbatim. SaveCommanderFile (CS:0358) writes commanderFileBytes
(DS:7567, 208) bytes from commanderBlock (DS:756B), and LoadCommanderFile (CS:031A) reads as many back over
the same bytes. So a .cdr is the slice DS:756B-763A of the data segment, laid out as Design/Symbols.tsv names
it: the text "Commander file" and a ^Z (which only make `type NAME.CDR` print a title), the status screen's
frame and rank, the cash as text, the random state, the galaxy, the charts' cursors, the current and the
selected system's 25-byte records, the fuel, the equipment, the cash, the legal status and kills, the name,
the hold, and the missions' state. There is no checksum and no version: the loader refuses only a file
with a DOS attribute set (int 21h AX=4300h), and reads what is there, with no check of its length.

Every file here starts from Replays/jameson.cdr, which the reference saved: the starting commander at Lave,
after selecting Leesti on the galactic chart. It is the DIR/files/JAMESON.CDR that this writes, byte for byte
(BASE_SHA256), from the repository root:

  ReferenceRunner --out DIR --steps "wait 3; key space; wait 4; key F5; wait 1; key f; wait 0.5;
    key l; key e; key e; key s; key t; key i; wait 0.3; key Return; wait 1; key F9; wait 1;
    key Escape; wait 1; key s; wait 1; key j; key a; key m; key e; key s; key o; key n;
    wait 0.5; key Return; wait 2"

Each prepared commander then changes only fields that Symbols.tsv names, to values the game's own code
gives them: equipment as the equipment screen fits and removes it, cash with its text as FormatCredits
writes it, kills, an arrival at the selected system as CompleteHyperspaceJump (CS:4707) and
UpdateMissionSchedule make it, the second mission as UpdateMissionSchedule gives it and ShowMissionBriefing
briefs it, and the random state as NextRandom leaves it some number of draws later. That is what makes one
faithful: it is a state the game reaches by play, which could have been saved and loaded, and not one it
could never be in; and the game loads it, as it loads its own saves, by copying the bytes back.

Development tool only (AGENTS.md R14): standard library.
"""

import argparse
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPLAYS = ROOT / "Replays"
SYMBOLS = ROOT / "Design" / "Symbols.tsv"
BASE = "jameson.cdr"
BASE_SHA256 = "cb06ed4615d7f3712130b7568034fe9738f99f67dc348f4fa692ac16bb6f5dd9"

BLOCK = 0x756B  # commanderBlock
BLOCK_BYTES = 208  # commanderFileBytes
RECORD_BYTES = 25  # systemRecordBytes: a system's record, current or selected
SIGNATURE = b"Commander file\x1a"
# FormatCredits writes ten digits, blanks up to eight leading zeros and puts a point before the last.
CREDIT_DIGITS = 10
CREDIT_BLANKS = 8
# CompleteHyperspaceJump: the short-range chart's cursor goes back to its centre.
SHORT_RANGE_CENTER = (0x50, 0x40)
LEGAL_STATUS_PER_JUMP = 5
# The second mission (UpdateMissionSchedule, ShowMissionBriefing): given on the 64th jump counted, it sends the
# commander after a mask ship and five ships in all, which spawn two jumps on.
MASK_MISSION = 2
MASK_MISSION_JUMPS = 0x40
MISSION_BRIEFED = 1
MASK_MISSION_SHIPS = 5
MASK_SYSTEM_JUMPS = 2
# laserMountTypes: two bits per mount, fore lowest.
MOUNTS = {"fore": 0, "aft": 1, "right": 2, "left": 3}
LASERS = {"pulse": ("pulseLaserCount", 0), "beam": ("beamLaserCount", 1), "mining": ("miningLaserCount", 2),
          "military": ("militaryLaserCount", 3)}


def read_symbols() -> dict[str, tuple[int, int]]:
  """The data segment's names in the commander block: offset in the file, and size in bytes."""
  symbols: dict[str, tuple[int, int]] = {}
  for line in SYMBOLS.read_text(encoding="utf-8").splitlines():
    columns = line.split("\t")
    if len(columns) < 4 or not columns[0].startswith("DS:"):
      continue
    offset = int(columns[0][3:], 16) - BLOCK
    if 0 <= offset < BLOCK_BYTES:
      symbols[columns[2]] = (offset, int(columns[3]) if columns[3] else 1)
  return symbols


class Commander:
  """A commander file's 208 bytes, changed by name."""

  def __init__(self, _data: bytes, _symbols: dict[str, tuple[int, int]]):
    if len(_data) != BLOCK_BYTES or not _data.startswith(SIGNATURE):
      sys.exit(f"Replays/{BASE} is not a commander file the reference saved")
    self.data = bytearray(_data)
    self.symbols = _symbols

  def field(self, _name: str, _size: int | None = None) -> tuple[int, int]:
    offset, size = self.symbols[_name]
    return offset, size if _size is None else _size

  def get(self, _name: str, _size: int | None = None) -> int:
    offset, size = self.field(_name, _size)
    return int.from_bytes(self.data[offset:offset + size], "little")

  def set(self, _name: str, _value: int, _size: int | None = None) -> None:
    offset, size = self.field(_name, _size)
    self.data[offset:offset + size] = _value.to_bytes(size, "little")

  def credits(self, _tenths: int) -> None:
    """creditsTenths (a 32-bit word pair, low word first) and creditBalanceText, as FormatCredits writes it."""
    self.set("creditsTenths", _tenths, 4)
    digits = f"{_tenths:0{CREDIT_DIGITS}d}"
    blanked = len(digits) - len(digits.lstrip("0"))
    digits = " " * min(blanked, CREDIT_BLANKS) + digits[min(blanked, CREDIT_BLANKS):]
    offset, _ = self.field("creditBalanceText")
    self.data[offset:offset + CREDIT_DIGITS + 1] = (digits[:-1] + "." + digits[-1]).encode("ascii")

  def fit(self, *_items: str) -> None:
    """Equipment the equipment screen fits with a byte of 1: fuelScoopsFitted, escapePodFitted and the like."""
    for item in _items:
      self.set(item, 1)

  def fit_laser(self, _laser: str, _mount: str) -> None:
    """A laser fitted to a free mount, as the equipment screen fits one: its count, the mount's bit and type."""
    count, kind = LASERS[_laser]
    mount = MOUNTS[_mount]
    if self.get("laserMountsFitted") & (1 << mount):
      sys.exit(f"the {_mount} mount already holds a laser")
    self.set(count, self.get(count) + 1)
    self.set("laserMountsFitted", self.get("laserMountsFitted") | (1 << mount))
    types = self.get("laserMountTypes") & ~(3 << (2 * mount))
    self.set("laserMountTypes", types | (kind << (2 * mount)))

  def remove_laser(self, _mount: str) -> None:
    """The laser on a mount taken off, as selling it on the equipment screen does: its count, the mount's bit.
    The mount's type bits stay, as ChooseMountToRemoveLaser leaves them, until a laser is fitted there."""
    mount = MOUNTS[_mount]
    if not self.get("laserMountsFitted") & (1 << mount):
      sys.exit(f"the {_mount} mount holds no laser")
    kind = (self.get("laserMountTypes") >> (2 * mount)) & 3
    count = next(name for name, laser_kind in LASERS.values() if laser_kind == kind)
    self.set(count, self.get(count) - 1)
    self.set("laserMountsFitted", self.get("laserMountsFitted") & ~(1 << mount))

  def draw_random_numbers(self, _count: int) -> None:
    """randomState0-2 as NextRandom (CS:061C) leaves them after _count more draws: (a, b, c) becomes (b, c, a+b+c).
    The game draws from it every frame in space, so this is the state some later moment of play would save."""
    a, b, c = (self.get(f"randomState{index}") for index in range(3))
    for _ in range(_count):
      a, b, c = b, c, (a + b + c) & 0xFFFF
    for index, value in enumerate((a, b, c)):
      self.set(f"randomState{index}", value)

  def arrive_at_selected_system(self) -> None:
    """The jump to the selected system, as CompleteHyperspaceJump leaves the commander's bytes, and docked there.

    The fuel it costs (as the H key computes it), five off the legal status, the selected system's record
    copied over the current one, the current system and both charts' cursors on it, the short-range cursor
    centred, the market to be rolled afresh, the target latched; then UpdateMissionSchedule, which counts no
    jump in the first galaxy, and the supernova and briefing flags an arrival sets.
    """
    distance = self.get("selectedDistanceTenthsLy")
    cost = max(1, distance * 36 // 10)
    if distance == 0 or cost > self.get("fuel"):
      sys.exit("the selected system is not in reach")
    self.set("fuel", self.get("fuel") - cost)
    self.set("legalStatus", max(0, self.get("legalStatus") - LEGAL_STATUS_PER_JUMP))
    # The record comes from the copy H latched (LatchHyperspaceTarget), distance and all, while the selected
    # system's distance is cleared.
    current, _ = self.field("currentSystemName")
    selected, _ = self.field("selectedSystemName")
    self.data[current:current + RECORD_BYTES] = self.data[selected:selected + RECORD_BYTES]
    self.set("selectedDistanceTenthsLy", 0)
    self.set("marketQuantitiesSet", 0)
    self.set("hyperspaceTargetIndex", self.get("selectedSystemIndex"))
    self.set("witchspaceCountdown", 0)
    # UpdateMissionSchedule: a briefed mask mission counts the jump in any galaxy.
    if self.get("maskSystemJumps") != 0:
      self.set("maskSystemJumps", self.get("maskSystemJumps") - 1)
      if self.get("maskSystemJumps") == 0:
        self.set("fledMaskShip", 1)
    # The galactic chart's cursor sits on the selected system: its x and its y/2.
    x, row = self.get("galacticCursorX"), self.get("galacticCursorY")
    if self.get("chartIsShortRange") != 0:
      sys.exit("select the system on the galactic chart, so that its cursor holds the system's position")
    for name, value in (("currentSystemX", x), ("currentSystemChartY", row), ("chartCursorX", x), ("chartCursorY", row),
                        ("galacticCursorX", x), ("galacticCursorY", row), ("shortRangeCursorX", SHORT_RANGE_CENTER[0]),
                        ("shortRangeCursorY", SHORT_RANGE_CENTER[1])):
      self.set(name, value)
    if self.get("galaxyNumber") != 0:
      sys.exit("UpdateMissionSchedule counts jumps outside the first galaxy, which this does not")
    self.set("supernovaHeat", 0)
    self.set("supernovaFrames", 0)
    self.set("jumpedSinceBriefing", 1)

  def brief_mask_mission(self) -> None:
    """The second mission given and briefed. UpdateMissionSchedule gives it on the 64th jump counted, which only
    jumps outside the first galaxy are, so this is a commander who made them there and came back; the briefing
    at the next station (ShowMissionBriefing) sends him after the mask ship, two jumps away."""
    self.set("missionJumpCount", MASK_MISSION_JUMPS)
    self.set("missionNumber", MASK_MISSION)
    self.set("missionStage", MISSION_BRIEFED)
    self.set("maskMissionShipsLeft", MASK_MISSION_SHIPS)
    self.set("maskSystemJumps", MASK_SYSTEM_JUMPS)


def docking_computer(_commander: Commander) -> None:
  _commander.fit("dockingComputerFitted")


def galactic_hyperdrive(_commander: Commander) -> None:
  _commander.fit("galacticHyperdriveFitted")


def escape_pod(_commander: Commander) -> None:
  _commander.fit("escapePodFitted")


def rich_at_leesti(_commander: Commander) -> None:
  _commander.arrive_at_selected_system()
  _commander.credits(300000)


def fighter(_commander: Commander) -> None:
  _commander.remove_laser("fore")
  _commander.fit_laser("military", "fore")
  _commander.fit("ecmFitted", "energyUnitFitted")
  _commander.set("killCount", 20)


def armed(_commander: Commander) -> None:
  fighter(_commander)
  _commander.fit("energyBombFitted")
  _commander.fit_laser("pulse", "aft")


def mask_mission(_commander: Commander) -> None:
  fighter(_commander)
  _commander.brief_mask_mission()
  _commander.arrive_at_selected_system()


def fighter_after(_draws: int):
  """The fighter, saved later: its random state _draws draws on. Each count was found by a search over counts for
  the run of its replay that meets the most ship types the rest of the corpus does not (see the replay)."""

  def prepare(_commander: Commander) -> None:
    fighter(_commander)
    _commander.draw_random_numbers(_draws)

  return prepare


def mask_mission_after(_draws: int):
  """The mask mission's commander, saved later: as fighter_after, for the fight that meets both kinds of escort."""

  def prepare(_commander: Commander) -> None:
    mask_mission(_commander)
    _commander.draw_random_numbers(_draws)

  return prepare


# Each prepared commander: the file it is written to, what it is, and how it differs from the reference's save.
COMMANDERS = [
  ("docking-computer.cdr", "at Lave, with a docking computer (Lave's tech level does not sell one)",
   docking_computer),
  ("galactic-hyperdrive.cdr", "at Lave, with a galactic hyperdrive", galactic_hyperdrive),
  ("escape-pod.cdr", "at Lave, with an escape capsule", escape_pod),
  ("rich-at-leesti.cdr", "docked at Leesti, tech level 10, after the jump from Lave, with 30,000 credits",
   rich_at_leesti),
  ("fighter.cdr", "at Lave, with a military laser for the pulse laser, ECM, an energy unit and 20 kills: enough "
   "for enemies to fire missiles (3)", fighter),
  ("armed.cdr", "the fighter, with an energy bomb and a pulse laser aft", armed),
  ("combat-orerve.cdr", "the fighter, 3,261,171 random draws on", fighter_after(3261171)),
  ("combat-reorte.cdr", "the fighter, 4,148,768 random draws on", fighter_after(4148768)),
  ("combat-orerve-drifters.cdr", "the fighter, 3,350,928 random draws on", fighter_after(3350928)),
  ("mask-mission.cdr", "the fighter, briefed for the second mission at Lave and docked at Leesti a jump later, "
   "where the mask ship and its escorts spawn", mask_mission),
  ("mask-mission-fight.cdr", "mask-mission.cdr, 3,680,037 random draws on", mask_mission_after(3680037)),
]


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--check", action="store_true",
                      help="write nothing; fail if a file differs from what this writes")
  arguments = parser.parse_args()
  symbols = read_symbols()
  base = (REPLAYS / BASE).read_bytes()
  if hashlib.sha256(base).hexdigest() != BASE_SHA256:
    sys.exit(f"Replays/{BASE} is not the commander the reference saved (BASE_SHA256); see this script's docstring")
  stale = []
  for name, what, prepare in COMMANDERS:
    commander = Commander(base, symbols)
    prepare(commander)
    path = REPLAYS / name
    if arguments.check:
      if not path.exists() or path.read_bytes() != bytes(commander.data):
        stale.append(name)
      continue
    path.write_bytes(bytes(commander.data))
    print(f"wrote Replays/{name}: {what}")
  if stale:
    print(f"not what PrepareCommanders.py writes: {', '.join(stale)}", file=sys.stderr)
    return 1
  return 0


if __name__ == "__main__":
  sys.exit(main())
