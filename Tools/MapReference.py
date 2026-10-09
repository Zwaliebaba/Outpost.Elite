#!/usr/bin/env python3
"""Map the reference binary from the symbol table: the Phase 1 workbench (plan §4, §5).

Design/Symbols.tsv is the source of truth for what is known about ELITEL.EXE: one row per address,
naming a routine, a jump table or a data item and saying what it does. This tool reads it, walks
the code from every routine and table it names, and writes:

  --listing FILE   an annotated disassembly, one block per routine, with names substituted for
                   addresses, the routine's callers, and every data item it touches;
  --seed           new rows in the table for every routine entry and every data address the code
                   references that has no row yet, named by address until someone names them;
  (always)         a coverage summary: code-segment bytes reached, data references named.

Row format (tab-separated, header line first, `#` lines are comments):
  address   CS:XXXX or DS:XXXX
  kind      routine | entry | isr | label | code | table | data | text
            (an entry is a second way into a routine and starts its own block; a label names a place
            inside one and does not; code is code no walk reaches yet)
  name      routines PascalCase (they become functions); data camelCase (fields of the overlay)
  size      bytes, optional; for a table, the entry count
  contract  registers and flags in and out, for routines; element type, for tables
  subsystem one of the plan's §5 subsystem names
  notes     anything else, briefly

A `table` row whose contract is `code` holds near code addresses; every one of them is walked as
a routine entry. Development tool only (AGENTS.md R14). Needs Capstone:
    python -m pip install capstone==5.0.7
"""

import argparse
import csv
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

try:
  from capstone import CS_ARCH_X86, CS_MODE_16, CS_OP_IMM, CS_OP_MEM, Cs, CsInsn
  from capstone import x86 as X86
except ImportError:
  sys.exit("Capstone is not installed: python -m pip install capstone==5.0.7")

ROOT = Path(__file__).resolve().parent.parent
CODE_SEGMENT_BYTES = 0x8F60
COLUMNS = ["address", "kind", "name", "size", "contract", "subsystem", "notes"]
UNNAMED = re.compile(r"(Routine|data)[0-9A-F]{4}")
KINDS = {"routine", "entry", "isr", "label", "code", "table", "data", "text"}
ENDS_FLOW = {"ret", "retf", "iret", "jmp", "ljmp"}
POINTER_REGISTERS = {X86.X86_REG_SI, X86.X86_REG_DI, X86.X86_REG_BX, X86.X86_REG_DX, X86.X86_REG_BP}


@dataclass
class Row:
  segment: str
  offset: int
  kind: str
  name: str
  size: str = ""
  contract: str = ""
  subsystem: str = ""
  notes: str = ""

  def key(self) -> tuple[str, int]:
    return self.segment, self.offset

  def cells(self) -> list[str]:
    return [f"{self.segment}:{self.offset:04X}", self.kind, self.name, self.size, self.contract, self.subsystem,
            self.notes]


@dataclass
class Routine:
  entry: int
  instructions: dict[int, CsInsn] = field(default_factory=dict)
  calls: set[int] = field(default_factory=set)
  leaves_to: set[int] = field(default_factory=set)  # jumps that land in another routine
  data: dict[int, set[str]] = field(default_factory=dict)  # DS offset -> access widths
  code_data: set[int] = field(default_factory=set)  # CS: data written or read


# ── The symbol table ─────────────────────────────────────────────────────────────────────────

def read_table(_path: Path) -> tuple[list[str], dict[tuple[str, int], Row]]:
  comments: list[str] = []
  rows: dict[tuple[str, int], Row] = {}
  if not _path.exists():
    return comments, rows
  lines = _path.read_text(encoding="utf-8").splitlines()
  body = []
  for line in lines:
    if line.startswith("#"):
      comments.append(line)
    elif line.strip():
      body.append(line)
  reader = csv.reader(body, delimiter="\t", quoting=csv.QUOTE_NONE)
  header = next(reader, None)
  if header != COLUMNS:
    sys.exit(f"{_path}: the header must be {' / '.join(COLUMNS)}")
  for number, cells in enumerate(reader, start=2):
    cells += [""] * (len(COLUMNS) - len(cells))
    match = re.fullmatch(r"(CS|DS):([0-9A-F]{4})", cells[0])
    if match is None or cells[1] not in KINDS or not cells[2]:
      sys.exit(f"{_path}: row {number} is malformed: {cells}")
    row = Row(match.group(1), int(match.group(2), 16), *cells[1:7])
    if row.key() in rows:
      sys.exit(f"{_path}: {cells[0]} has two rows")
    rows[row.key()] = row
  names = [row.name for row in rows.values()]
  duplicates = {name for name in names if names.count(name) > 1}
  if duplicates:
    sys.exit(f"{_path}: names used twice: {', '.join(sorted(duplicates))}")
  return comments, rows


def write_table(_path: Path, _comments: list[str], _rows: dict[tuple[str, int], Row]) -> None:
  lines = list(_comments) + ["\t".join(COLUMNS)]
  for key in sorted(_rows):
    lines.append("\t".join(_rows[key].cells()))
  _path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


# ── Walking the code ─────────────────────────────────────────────────────────────────────────

def decode_all(_code: bytes, _entries: set[int]) -> dict[int, CsInsn]:
  disassembler = Cs(CS_ARCH_X86, CS_MODE_16)
  disassembler.detail = True
  instructions: dict[int, CsInsn] = {}
  pending = list(_entries)
  while pending:
    address = pending.pop()
    while 0 <= address < len(_code) and address not in instructions:
      decoded = next(disassembler.disasm(_code[address:address + 16], address), None)
      if decoded is None:
        break
      instructions[address] = decoded
      target = branch_target(decoded)
      if target is not None and 0 <= target < len(_code):
        pending.append(target)
      if decoded.mnemonic in ENDS_FLOW:
        break
      address += decoded.size
  return instructions


def branch_target(_instruction: CsInsn) -> int | None:
  mnemonic = _instruction.mnemonic
  if not (mnemonic.startswith(("j", "loop")) or mnemonic == "call") or mnemonic in ("ljmp",):
    return None
  operand = _instruction.operands[0] if _instruction.operands else None
  # IP wraps within the segment; Capstone reports a near target past 0xFFFF unwrapped.
  return operand.imm & 0xFFFF if operand is not None and operand.type == CS_OP_IMM else None


# A displacement this close to zero, added to a base register, is a field offset inside a record
# (`[di+0x1E]`, `[di-1]`) rather than the address of a table; the data segment's first and last bytes
# are not indexed that way anywhere in the code.
FIELD_OFFSET_REACH = 0x0200


def memory_references(_instruction: CsInsn) -> list[tuple[str, int, int]]:
  """(segment, offset, width) for each memory operand that names an absolute address or a table base."""
  found = []
  for operand in _instruction.operands:
    if operand.type != CS_OP_MEM:
      continue
    memory = operand.mem
    if memory.index != 0 or memory.base == X86.X86_REG_BP:
      continue
    if memory.base != 0 and not FIELD_OFFSET_REACH <= (memory.disp & 0xFFFF) <= 0x10000 - FIELD_OFFSET_REACH:
      continue
    segment = {0: "DS", X86.X86_REG_DS: "DS", X86.X86_REG_CS: "CS"}.get(memory.segment)
    if segment is None:
      continue
    found.append((segment, memory.disp & 0xFFFF, operand.size))
  return found


def build_routines(_instructions: dict[int, CsInsn], _entries: set[int]) -> dict[int, Routine]:
  routines: dict[int, Routine] = {}
  for entry in sorted(_entries):
    if entry not in _instructions:
      continue
    routine = Routine(entry)
    pending = [entry]
    while pending:
      address = pending.pop()
      while address in _instructions and address not in routine.instructions:
        if address != entry and address in _entries:
          routine.leaves_to.add(address)
          break
        decoded = _instructions[address]
        routine.instructions[address] = decoded
        for segment, offset, width in memory_references(decoded):
          if segment == "DS":
            routine.data.setdefault(offset, set()).add({1: "byte", 2: "word", 4: "dword"}.get(width, str(width)))
          else:
            routine.code_data.add(offset)
        target = branch_target(decoded)
        if target is not None:
          if decoded.mnemonic == "call":
            routine.calls.add(target)
          else:
            pending.append(target)
        if decoded.mnemonic in ENDS_FLOW:
          break
        address += decoded.size
    routines[entry] = routine
  return routines


WRITING_MNEMONICS = {"mov", "inc", "dec", "add", "sub", "and", "or", "xor", "not", "neg", "shl", "shr", "sar", "rol",
                     "ror", "rcl", "rcr", "xchg", "adc", "sbb", "pop"}


def closure(_routines: dict[int, Routine], _roots: list[int]) -> set[int]:
  """Every routine reachable from the roots by call or by running on into another routine."""
  seen: set[int] = set()
  pending = list(_roots)
  while pending:
    entry = pending.pop()
    if entry in seen or entry not in _routines:
      continue
    seen.add(entry)
    pending += list(_routines[entry].calls) + list(_routines[entry].leaves_to)
  return seen


def data_accesses(_routines: dict[int, Routine], _members: set[int]) -> tuple[dict[int, set[int]], dict[int, set[int]]]:
  """(writes, reads): DS offset -> the instruction addresses that write or only read it."""
  writes: dict[int, set[int]] = {}
  reads: dict[int, set[int]] = {}
  for entry in _members:
    for address, decoded in _routines[entry].instructions.items():
      first_is_memory = bool(decoded.operands) and decoded.operands[0].type == CS_OP_MEM
      for segment, offset, _width in memory_references(decoded):
        if segment == "DS":
          target = writes if decoded.mnemonic in WRITING_MNEMONICS and first_is_memory else reads
          target.setdefault(offset, set()).add(address)
  return writes, reads


def report_shared(_routines: dict[int, Routine], _rows: dict[tuple[str, int], Row], _handlers: list[int]) -> None:
  handlers = closure(_routines, _handlers)
  rest = closure(_routines, [0x0000]) - handlers
  handler_writes, handler_reads = data_accesses(_routines, handlers)
  main_writes, main_reads = data_accesses(_routines, rest)

  def name(_offset: int) -> str:
    return label(_rows, "DS", _offset) or f"DS:{_offset:04X}"

  print(f"shared state: {len(handlers)} routines run from the handlers, {len(rest)} from the main program")
  print("  written by the handlers, and read or written by the main program:")
  for offset in sorted(handler_writes):
    use = [verb for verb, table in (("writes", main_writes), ("reads", main_reads)) if offset in table]
    if use:
      print(f"    {name(offset):<26} handler writes at {', '.join(f'{a:04X}' for a in sorted(handler_writes[offset]))};"
            f" main {' and '.join(use)}")
  print("  written by the main program and only read by the handlers:")
  for offset in sorted(main_writes):
    if offset in handler_reads and offset not in handler_writes:
      print(f"    {name(offset)}")


# ── Output ───────────────────────────────────────────────────────────────────────────────────

def label(_rows: dict[tuple[str, int], Row], _segment: str, _offset: int) -> str | None:
  row = _rows.get((_segment, _offset))
  return row.name if row else None


def annotate(_instruction: CsInsn, _rows: dict[tuple[str, int], Row]) -> str:
  text = f"{_instruction.mnemonic} {_instruction.op_str}".strip()
  notes = []
  target = branch_target(_instruction)
  if target is not None:
    name = label(_rows, "CS", target)
    if name:
      text = re.sub(r"0x[0-9a-f]+$", name, text)
  for segment, offset, _width in memory_references(_instruction):
    name = label(_rows, segment, offset)
    if name:
      notes.append(name)
  # `mov si, imm` and the like load an address only when the immediate names a data row; an `add` or
  # `cmp` with the same number is arithmetic, and a code address in an immediate is rare enough to
  # leave to the reader.
  if _instruction.mnemonic == "mov" and len(_instruction.operands) == 2:
    first, second = _instruction.operands
    if first.type == 1 and first.reg in POINTER_REGISTERS and second.type == CS_OP_IMM:
      row = _rows.get(("DS", second.imm & 0xFFFF))
      if row is not None and row.kind in ("data", "table", "text"):
        notes.append(f"&{row.name}")
  return text + (f"    ; {', '.join(notes)}" if notes else "")


def write_listing(_path: Path, _routines: dict[int, Routine], _rows: dict[tuple[str, int], Row],
                  _code: bytes) -> None:
  callers: dict[int, list[int]] = {}
  for routine in _routines.values():
    for target in routine.calls:
      callers.setdefault(target, []).append(routine.entry)
  out = []
  for entry in sorted(_routines):
    routine = _routines[entry]
    row = _rows.get(("CS", entry))
    name = row.name if row else f"Routine{entry:04X}"
    out.append("")
    out.append(f";; {'=' * 96}")
    out.append(f";; {name}  CS:{entry:04X}  [{row.kind if row else 'unnamed'}]"
               + (f"  subsystem: {row.subsystem}" if row and row.subsystem else ""))
    if row and row.contract:
      out.append(f";; contract: {row.contract}")
    if row and row.notes:
      out.append(f";; notes: {row.notes}")
    called_by = sorted(set(callers.get(entry, [])))
    if called_by:
      out.append(";; called by: " + ", ".join(label(_rows, "CS", c) or f"Routine{c:04X}" for c in called_by))
    if routine.data:
      shown = [f"{label(_rows, 'DS', offset) or f'DS:{offset:04X}'}({'/'.join(sorted(widths))})"
               for offset, widths in sorted(routine.data.items())]
      out.append(";; data: " + ", ".join(shown))
    if routine.leaves_to:
      onward = (label(_rows, "CS", target) or f"{target:04X}" for target in sorted(routine.leaves_to))
      out.append(";; continues into: " + ", ".join(onward))
    for address in sorted(routine.instructions):
      decoded = routine.instructions[address]
      inner = _rows.get(("CS", address))
      if inner and address != entry:
        out.append(f"{inner.name}:")
      out.append(f"  {address:04X}  {decoded.bytes.hex():<14} {annotate(decoded, _rows)}")
  _path.write_text("\n".join(out) + "\n", encoding="utf-8")


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("--exe", type=Path, default=ROOT / "ELITEL.EXE")
  parser.add_argument("--table", type=Path, default=ROOT / "Design" / "Symbols.tsv")
  parser.add_argument("--listing", type=Path, help="write the annotated disassembly here")
  parser.add_argument("--seed", action="store_true", help="add a row for every unnamed routine and data reference")
  parser.add_argument("--gaps", action="store_true", help="list the unreached regions that look like code")
  parser.add_argument("--shared", metavar="ADDR,ADDR", help="report the data the routines at these code addresses "
                      "(interrupt handlers) share with the rest of the program")
  args = parser.parse_args()

  data = args.exe.read_bytes()
  image = data[struct.unpack_from("<H", data, 8)[0] * 16:]
  code, data_segment = image[:CODE_SEGMENT_BYTES], image[CODE_SEGMENT_BYTES:]
  comments, rows = read_table(args.table)

  entries = {row.offset for row in rows.values() if row.segment == "CS" and row.kind in ("routine", "entry", "isr")}
  entries.add(0x0000)
  for row in rows.values():
    if row.kind == "table" and row.contract == "code" and row.size:
      source = data_segment if row.segment == "DS" else code
      for target in struct.unpack_from(f"<{int(row.size)}H", source, row.offset):
        entries.add(target)
  instructions = decode_all(code, entries)
  call_targets = {branch_target(i) for i in instructions.values() if i.mnemonic == "call"} - {None}
  routine_entries = (entries | call_targets) & set(instructions)
  routines = build_routines(instructions, routine_entries)

  reached = sum(i.size for i in instructions.values())
  referenced = {offset for routine in routines.values() for offset in routine.data}

  def is_named(_segment: str, _offset: int) -> bool:
    row = rows.get((_segment, _offset))
    return row is not None and not UNNAMED.fullmatch(row.name)

  print(f"code: {len(instructions)} instructions, {reached} of {len(code)} bytes reached, {len(routines)} routines,"
        f" {sum(1 for entry in routines if is_named('CS', entry))} named")
  named_data = sum(1 for offset in referenced if is_named("DS", offset))
  print(f"data: {len(referenced)} DS offsets referenced by code, {named_data} named")
  if args.gaps:
    covered = bytearray(len(code))
    for address, decoded in instructions.items():
      covered[address:address + decoded.size] = b"\x01" * decoded.size
    start = None
    for offset in range(len(code) + 1):
      if offset < len(code) and not covered[offset]:
        start = offset if start is None else start
      elif start is not None:
        if offset - start > 1 and code[start:offset].count(0) < (offset - start) * 0.6:
          print(f"  unreached CS:{start:04X}-{offset - 1:04X} ({offset - start} bytes)")
        start = None

  if args.shared:
    report_shared(routines, rows, [int(part, 16) for part in args.shared.split(",")])
  if args.seed:
    added = 0
    for entry in routines:
      if ("CS", entry) not in rows:
        rows[("CS", entry)] = Row("CS", entry, "routine", f"Routine{entry:04X}")
        added += 1
    for offset in referenced:
      if ("DS", offset) not in rows:
        widths = {w for routine in routines.values() for w in routine.data.get(offset, set())}
        size = {"byte": "1", "word": "2"}.get(next(iter(widths)), "") if len(widths) == 1 else ""
        rows[("DS", offset)] = Row("DS", offset, "data", f"data{offset:04X}", size)
        added += 1
    write_table(args.table, comments, rows)
    print(f"seed: {added} rows added to {args.table}")
  if args.listing:
    write_listing(args.listing, routines, rows, code)
    print(f"listing: {args.listing}")
  return 0


if __name__ == "__main__":
  sys.exit(main())
