#!/usr/bin/env python3
"""Measure the reference binary for the provenance record in ADR-001.

Disassembles the code segment of ELITES.EXE by recursive descent from its entry point, its four
interrupt handlers and its three resolved jump tables, then reports:

  * the file's identity (size, SHA-256, MZ header fields);
  * how much of the code segment the descent reaches;
  * every NOP the descent reaches that is not assembler padding after an unconditional jump,
    which is where a patch that removed an instruction leaves its trace;
  * every region the descent does not reach, classified as single-byte padding, short or
    zero-filled data, or code, with any branch into a code region from other unreached code.

This is a measuring instrument for Phase 0, not the Phase 1 map. It knows nothing about the
data-driven dispatch at 0x3D08 (`jmp ax`, with targets read from data), so code reached only
through that is reported as unreached.

Development tool only (AGENTS.md R14 binds what the executable is built from). Needs Capstone:
    python -m pip install capstone==5.0.7
"""

import argparse
import hashlib
import struct
import sys
from pathlib import Path

try:
  from capstone import CS_ARCH_X86, CS_MODE_16, CS_OP_IMM, Cs, CsInsn
except ImportError:
  sys.exit("Capstone is not installed: python -m pip install capstone==5.0.7")

CODE_SEGMENT_BYTES = 0x8F40  # the data segment, 0x08F4, starts here: five relocations load DS with it

ENTRY_POINTS = {
  0x0000: "program entry",
  0x0201: "int 9 handler (keyboard)",
  0x0215: "int 8 handler (timer)",
  0x025E: "int 0 handler (divide overflow)",
  0x02F0: "int 24h handler (critical error)",
}

# (data-segment offset, entry count, the indirect branch that uses it)
JUMP_TABLES = [
  (0x4D15, 4, "jmp [bx+0x4D15] at 0x36E3"),
  (0x7990, 8, "call [bx+0x7990] at 0x4A41"),
  (0x9763, 31, "jmp [bx] at 0x7029, text control codes 1-31"),
]

ENDS_FLOW = {"ret", "retf", "iret", "jmp", "ljmp"}


def load(_path: Path) -> tuple[bytes, tuple[int, ...], bytes]:
  data = _path.read_bytes()
  header = struct.unpack_from("<14H", data, 0)
  if header[0] != 0x5A4D:
    sys.exit(f"{_path} is not an MZ executable")
  return data, header, data[header[4] * 16:]


def disassemble(_code: bytes, _entries: list[int]) -> dict[int, CsInsn]:
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
      operand = decoded.operands[0] if decoded.operands else None
      branches = decoded.mnemonic.startswith(("j", "loop")) or decoded.mnemonic == "call"
      # IP wraps within the segment; Capstone reports a near target past 0xFFFF unwrapped.
      target = operand.imm & 0xFFFF if operand is not None and operand.type == CS_OP_IMM else None
      if branches and target is not None and target < len(_code):
        pending.append(target)
      if decoded.mnemonic in ENDS_FLOW:
        break
      address += decoded.size
  return instructions


def unreached_regions(_size: int, _instructions: dict[int, CsInsn]) -> list[tuple[int, int]]:
  covered = bytearray(_size)
  for address, decoded in _instructions.items():
    covered[address:address + decoded.size] = b"\x01" * decoded.size
  regions = []
  start = None
  for offset in range(_size + 1):
    if offset < _size and not covered[offset]:
      start = offset if start is None else start
    elif start is not None:
      regions.append((start, offset))
      start = None
  return regions


def branch_sources(_code: bytes, _targets: list[int]) -> dict[int, list[int]]:
  """Every byte offset holding a near call, jump or short branch that would land on a target."""
  found: dict[int, list[int]] = {target: [] for target in _targets}
  for offset in range(len(_code) - 2):
    opcode = _code[offset]
    if opcode in (0xE8, 0xE9):
      target = (offset + 3 + struct.unpack_from("<h", _code, offset + 1)[0]) & 0xFFFF
    elif opcode == 0xEB or 0x70 <= opcode <= 0x7F or opcode in (0xE2, 0xE3):
      target = (offset + 2 + struct.unpack_from("<b", _code, offset + 1)[0]) & 0xFFFF
    else:
      continue
    if target in found:
      found[target].append(offset)
  return found


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  default = Path(__file__).resolve().parent.parent / "ELITES.EXE"
  parser.add_argument("exe", nargs="?", type=Path, default=default, help="the reference binary")
  args = parser.parse_args()

  data, header, image = load(args.exe)
  code = image[:CODE_SEGMENT_BYTES]
  data_segment = image[CODE_SEGMENT_BYTES:]

  print(f"file      {args.exe.name}, {len(data)} bytes, SHA-256 {hashlib.sha256(data).hexdigest()}")
  print(f"MZ        {header[3]} relocations, header {header[4] * 16} bytes, entry {header[11]:04X}:{header[10]:04X},"
        f" stack {header[7]:04X}:{header[8]:04X}, checksum field {header[9]:04X}")

  entries = list(ENTRY_POINTS)
  for table, count, user in JUMP_TABLES:
    targets = [target for target in struct.unpack_from(f"<{count}H", data_segment, table) if target < len(code)]
    print(f"table     DS:{table:04X}, {count} entries, {len(targets)} inside the code segment; {user}")
    entries.extend(targets)

  instructions = disassemble(code, entries)
  reached = sum(decoded.size for decoded in instructions.values())
  print(f"reached   {len(instructions)} instructions, {reached} of {len(code)} code-segment bytes")

  print("\nNOPs reached that are not padding after an unconditional jump:")
  for address, decoded in sorted(instructions.items()):
    if decoded.mnemonic != "nop":
      continue
    previous = next((p for p in instructions.values() if p.address + p.size == address), None)
    if previous is not None and previous.mnemonic == "jmp":
      continue
    context = f"{previous.mnemonic} {previous.op_str}".strip() if previous is not None else "(branch target)"
    print(f"  {address:04X}  after `{context}`")

  regions = unreached_regions(len(code), instructions)
  singles = [start for start, end in regions if end - start == 1]
  code_regions = [(start, end) for start, end in regions
                  if end - start >= 4 and code[start:end].count(0) < (end - start) * 0.6]
  other = len(regions) - len(singles) - len(code_regions)
  print(f"\nunreached {sum(end - start for start, end in regions)} bytes in {len(regions)} regions:"
        f" {len(singles)} single bytes, {other} short or zero-filled, {len(code_regions)} code")

  # A branch pattern inside reached code but not at an instruction start is operand bytes, not a branch;
  # only patterns inside unreached code are candidates.
  def unreached(_offset: int) -> bool:
    return any(start <= _offset < end for start, end in code_regions)

  sources = branch_sources(code, [start for start, _ in code_regions])
  for start, end in code_regions:
    before = max((address for address in instructions if address < start), default=None)
    follows = f"{instructions[before].mnemonic} {instructions[before].op_str}".strip() if before is not None else "-"
    callers = [f"{source:04X}" for source in sources[start] if unreached(source)]
    print(f"  {start:04X}-{end - 1:04X} {end - start:5d} bytes, follows `{follows}`;"
          f" branched to from unreached code at: {', '.join(callers) or 'nowhere'}")
  return 0


if __name__ == "__main__":
  sys.exit(main())
