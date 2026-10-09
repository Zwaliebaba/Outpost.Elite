#!/usr/bin/env python3
"""Compare two instruction traces of the reference, the host's against DOSBox-X's (ADR-003).

  python Tools/CompareTrace.py EXPECTED ACTUAL [--wait START-END]... [--context N]

Both files are in the format Tools/ReferenceTrace.py describes: "XTRACE1\\0" and then, per instruction,
the fourteen words CS IP AX BX CX DX SI DI BP SP DS ES SS FLAGS as they were before it executed.

Two emulators running the same program do not produce the same trace, and four differences are not
the program's. Each is set aside before the comparison:

  BIOS and DOS code   DOSBox-X runs its own handlers in segment F000; the host services those calls
                      without executing anything. Every record in F000 is dropped.
  Hardware interrupts The 1 kHz timer lands on instruction boundaries that each emulator's cycle
                      accounting chooses. A record at one of the game's interrupt handlers (--isr, by
                      default TimerInterrupt 0x0215 and KeyboardInterrupt 0x0201 in the entry code
                      segment) starts a span that ends when SS:SP is back to the value it had before
                      the interrupt pushed FLAGS, CS and IP, which happens only after its IRET. The span
                      is dropped.
  REP iterations      DOSBox-X logs a repeated string instruction once per iteration; the host
                      executes it in one step. Consecutive records at the same CS:IP become the first.
  Waits on time       A loop that waits for the timer or the retrace spins as often as the clock it
                      watches dictates. Records whose IP lies in a --wait range of the entry code
                      segment are dropped. By default those are the reference's three polling loops:
                      the frame-time wait in PresentSpaceView (0599-05A2) and the vertical-retrace
                      waits it and CopyChartBufferToScreen call (461B-4620, 05D0-05D5). Whatever
                      registers differ when a wait ends hold what it read, and become input registers
                      (below).

One more difference is an input rather than the program's: what the program reads from a port whose
value moves with time -- the timer's counters (0x40-0x42), the timer-2 and refresh bits of 0x61, the
CGA status (0x3DA) and the joystick (0x201) -- and what it reads back from a variable where it kept
such a value (--input-address, by default speakerPortImage 0x2008 and savedSpeakerPort 0x226A, which
hold port 0x61 as read at start-up). The comparison decodes the instruction just executed from the
reference image. After an IN from one of those ports, AX becomes an input register; after a direct
load from one of those addresses (MOV AL/AX from memory, or MOV reg, [disp16]), its destination does.
An input register may differ for as long as it is the only kind that differs, and stops being one
when the two agree again. A difference that reaches any other register is a divergence like any
other, to be looked at rather than waved through.

What is left is compared record by record. CS:IP and the twelve other registers must agree. FLAGS are
compared on the defined bits only, and a difference there is reported but does not fail the
comparison by itself: the 8088 leaves flags undefined after several instructions and the two
emulators fill them in differently. A conditional jump that depends on such a flag diverges, and that
shows up as a CS:IP difference.

Exit status: 0 the traces agree, 1 they diverge (the first divergence is printed with context),
2 usage or file error.
"""

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAGIC = b"XTRACE1\0"
RECORD = struct.Struct("<14H")
FIELDS = ("CS", "IP", "AX", "BX", "CX", "DX", "SI", "DI", "BP", "SP", "DS", "ES", "SS", "FLAGS")
CS, IP, SP, SS, FLAGS = 0, 1, 9, 12, 13
DEFINED_FLAGS = 0x0FD5
BIOS_SEGMENT = 0xF000
AX, BX, CX, DX, SI, DI, BP = 2, 3, 4, 5, 6, 7, 8
TIMED_PORTS = {0x40, 0x41, 0x42, 0x61, 0x3DA, 0x201}
DEFAULT_WAITS = ["0599-05A2", "461B-4620", "05D0-05D5"]
# The ModRM reg field's register, as a record index, for word and for byte operands (AH-BH are halves).
WORD_REGISTERS = (AX, CX, DX, BX, SP, BP, SI, DI)
BYTE_REGISTERS = (AX, CX, DX, BX, AX, CX, DX, BX)


class Image:
  """The reference's load image, to decode the instruction at an address in its entry code segment."""

  def __init__(self, _path: Path) -> None:
    data = _path.read_bytes()
    header_bytes = int.from_bytes(data[8:10], "little") * 16
    self.entry_cs_in_image = int.from_bytes(data[0x16:0x18], "little")
    self.code = data[header_bytes:]

  def input_register(self, _record: tuple[int, ...], _entry_cs: int, _addresses: set[int]) -> int | None:
    """The register the instruction at this record's CS:IP loads with an input, if it does."""
    if _record[CS] != _entry_cs:
      return None
    offset = self.entry_cs_in_image * 16 + _record[IP]
    if offset + 3 >= len(self.code):
      return None
    opcode, second = self.code[offset], self.code[offset + 1]
    if opcode in (0xE4, 0xE5):
      return AX if second in TIMED_PORTS else None
    if opcode in (0xEC, 0xED):
      return AX if _record[DX] in TIMED_PORTS else None
    if opcode in (0xA0, 0xA1):
      return AX if int.from_bytes(self.code[offset + 1:offset + 3], "little") in _addresses else None
    if opcode in (0x8A, 0x8B) and second & 0xC7 == 0x06:
      if int.from_bytes(self.code[offset + 2:offset + 4], "little") in _addresses:
        return (BYTE_REGISTERS if opcode == 0x8A else WORD_REGISTERS)[(second >> 3) & 7]
    return None


def load(_path: Path) -> list[tuple[int, ...]]:
  data = _path.read_bytes()
  if not data.startswith(MAGIC) or (len(data) - len(MAGIC)) % RECORD.size != 0:
    raise ValueError(f"{_path} is not a trace file")
  return list(RECORD.iter_unpack(data[len(MAGIC):]))


def routine_names() -> dict[int, str]:
  names = {}
  with open(ROOT / "Design" / "Symbols.tsv", encoding="utf-8") as table:
    for row in table:
      fields = row.rstrip("\n").split("\t")
      if len(fields) > 2 and fields[0].startswith("CS:") and fields[1] in ("routine", "isr", "entry", "label", "code"):
        names[int(fields[0][3:], 16)] = fields[2]
  return names


def locate(_offset: int, _starts: list[int], _names: dict[int, str]) -> str:
  """The symbol at or below an offset, as name+delta."""
  best = None
  for start in _starts:
    if start > _offset:
      break
    best = start
  if best is None:
    return "?"
  delta = _offset - best
  return _names[best] + (f"+{delta:X}" if delta else "")


def normalize(_records: list[tuple[int, ...]], _isrs: set[int],
              _waits: list[tuple[int, int]]) -> list[tuple[tuple[int, ...], bool]]:
  """The records left once the four non-program differences are set aside, each with a flag that says
  whether only its CS:IP is to be compared (the first record after a wait)."""
  if not _records:
    return []
  entry_cs = _records[0][CS]
  kept: list[tuple[tuple[int, ...], bool]] = []
  index = 0
  after_wait = False
  while index < len(_records):
    record = _records[index]
    if record[CS] == BIOS_SEGMENT:
      index += 1
      continue
    if record[CS] == entry_cs and record[IP] in _isrs:
      resume = (record[SS], (record[SP] + 6) & 0xFFFF)
      index += 1
      while index < len(_records) and (_records[index][SS], _records[index][SP]) != resume:
        index += 1
      continue
    if record[CS] == entry_cs and any(start <= record[IP] < end for start, end in _waits):
      after_wait = True
      index += 1
      continue
    if kept and kept[-1][0][CS] == record[CS] and kept[-1][0][IP] == record[IP] and not after_wait:
      index += 1
      continue
    kept.append((record, after_wait))
    after_wait = False
    index += 1
  return kept


def describe(_record: tuple[int, ...]) -> str:
  return " ".join(f"{name}={value:04X}" for name, value in zip(FIELDS, _record))


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("expected", type=Path, help="the independent trace (DOSBox-X)")
  parser.add_argument("actual", type=Path, help="the trace under test (the host)")
  parser.add_argument("--isr", type=lambda text: int(text, 0), action="append",
                      help="an interrupt handler's offset in the entry code segment (default 0x0215 and 0x0201)")
  parser.add_argument("--wait", action="append",
                      help="START-END, a half-open hex IP range in the entry code segment that waits on time "
                           "(default 0599-05A2, 461B-4620 and 05D0-05D5)")
  parser.add_argument("--context", type=int, default=6, help="matching records to print before a divergence")
  parser.add_argument("--input-address", type=lambda text: int(text, 0), action="append",
                      help="a data-segment offset that holds a timed input (default 0x2008 and 0x226A)")
  parser.add_argument("--exe", type=Path, default=ROOT / "ELITEL.EXE", help="the reference binary, to decode inputs")
  args = parser.parse_args()

  try:
    expected_raw, actual_raw = load(args.expected), load(args.actual)
    image = Image(args.exe)
    waits = [tuple(int(bound, 16) for bound in text.split("-")) for text in args.wait or DEFAULT_WAITS]
  except (OSError, ValueError) as error:
    print(f"error: {error}", file=sys.stderr)
    return 2
  isrs = set(args.isr or [0x0215, 0x0201])
  expected = normalize(expected_raw, isrs, waits)
  actual = normalize(actual_raw, isrs, waits)
  names = routine_names()
  starts = sorted(names)
  print(f"expected: {len(expected_raw)} records, {len(expected)} after normalizing")
  print(f"actual:   {len(actual_raw)} records, {len(actual)} after normalizing")

  flag_differences = 0
  input_differences = 0
  input_registers: set[int] = set()
  addresses = set(args.input_address or [0x2008, 0x226A])
  entry_cs = expected[0][0][CS] if expected else 0
  for index in range(min(len(expected), len(actual))):
    (want, want_ip_only), (got, got_ip_only) = expected[index], actual[index]
    ip_only = want_ip_only or got_ip_only
    if index > 0:
      loaded = image.input_register(expected[index - 1][0], entry_cs, addresses)
      if loaded is not None:
        input_registers.add(loaded)
    differing_registers = {field for field in range(FLAGS) if want[field] != got[field]}
    if ip_only and want[:2] == got[:2]:
      input_registers |= differing_registers  # what the wait left behind
    input_registers &= differing_registers
    if differing_registers and differing_registers <= input_registers and want[:2] == got[:2]:
      input_differences += 1
      same = True
    else:
      same = not differing_registers
    if not same:
      print(f"\nDIVERGED at normalized record {index}, in {locate(want[IP], starts, names)}:")
      for previous in range(max(0, index - args.context), index):
        print(f"  ok        {locate(expected[previous][0][IP], starts, names):28} {describe(expected[previous][0])}")
      print(f"  expected  {locate(want[IP], starts, names):28} {describe(want)}")
      print(f"  actual    {locate(got[IP], starts, names):28} {describe(got)}")
      differing = [FIELDS[field] for field in sorted(differing_registers)]
      print(f"  differing: {', '.join(differing)}" + (" (the first record after a wait)" if ip_only else ""))
      return 1
    if not ip_only and (want[FLAGS] ^ got[FLAGS]) & DEFINED_FLAGS:
      if flag_differences < 5:
        print(f"  flags differ at record {index}, {locate(want[IP], starts, names)}: "
              f"{want[FLAGS]:04X} vs {got[FLAGS]:04X}")
      flag_differences += 1

  if len(expected) != len(actual):
    shorter = "actual" if len(actual) < len(expected) else "expected"
    print(f"\nThe traces agree for {min(len(expected), len(actual))} records; then the {shorter} trace ends.")
    return 1
  print(f"\nThe traces agree: {len(expected)} records, {flag_differences} with differing defined flags, "
        f"{input_differences} where only input registers differ.")
  return 0


if __name__ == "__main__":
  sys.exit(main())
