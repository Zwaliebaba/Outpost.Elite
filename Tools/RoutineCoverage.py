#!/usr/bin/env python3
"""Check that comparisons exercised every reachable instruction of each ported routine (plan §5 Phase 3).

A native routine is accepted when it matched the original on every compared call and those calls
covered every reachable instruction of the code it replaces (ADR-010). ReferenceRunner --compare
--native-report FILE lists, for each hooked entry, the offsets the original executed while it was being
compared. This tool walks the code statically, as MapReference.py does, takes for each hooked entry the
instructions of the routine and of everything it calls or runs into, and lists those no comparison
executed. Each one needs a replay that reaches it, or a written reason in Design/NativeCoverage.tsv.

    python Tools/RoutineCoverage.py REPORT.tsv [REPORT.tsv ...]

Development tool only (AGENTS.md R14). Needs Capstone, as MapReference.py does. Exit status 1 when an
instruction is neither covered nor explained.
"""

import argparse
import csv
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import MapReference as Map  # noqa: E402  (a sibling tool, imported for its walk)

REASONS = Map.ROOT / "Design" / "NativeCoverage.tsv"


def read_reasons() -> dict[int, str]:
  """CS offsets explained in Design/NativeCoverage.tsv: offset, then the reason no replay reaches it."""
  reasons: dict[int, str] = {}
  if not REASONS.exists():
    return reasons
  with REASONS.open(encoding="utf-8", newline="") as file:
    for row in csv.reader((line for line in file if not line.startswith("#")), delimiter="\t"):
      if len(row) >= 2 and row[0].strip() and row[0] != "offset":
        reasons[int(row[0].removeprefix("CS:"), 16)] = row[1]
  return reasons


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("reports", nargs="+", type=Path, help="ReferenceRunner --native-report files")
  arguments = parser.parse_args()

  hooked: dict[int, str] = {}
  executed: set[int] = set()
  for report in arguments.reports:
    with report.open(encoding="utf-8", newline="") as file:
      for row in csv.DictReader(file, delimiter="\t"):
        entry = int(row["entry"], 16)
        hooked[entry] = row["routine"]
        executed.update(int(offset, 16) for offset in row["executed"].split())

  data = (Map.ROOT / "ELITES.EXE").read_bytes()
  image = data[struct.unpack_from("<H", data, 8)[0] * 16:]
  code = image[:Map.CODE_SEGMENT_BYTES]
  _, rows = Map.read_table(Map.ROOT / "Design" / "Symbols.tsv")
  entries = {row.offset for row in rows.values() if row.segment == "CS" and row.kind in ("routine", "entry", "isr")}
  instructions = Map.decode_all(code, entries | set(hooked))
  call_targets = {Map.branch_target(i) for i in instructions.values() if i.mnemonic == "call"} - {None}
  routines = Map.build_routines(instructions, (entries | call_targets | set(hooked)) & set(instructions))
  reasons = read_reasons()

  failed = False
  for entry, name in sorted(hooked.items(), key=lambda _item: _item[0]):
    reachable = {address for member in Map.closure(routines, [entry]) for address in routines[member].instructions}
    missing = sorted(reachable - executed)
    unexplained = [offset for offset in missing if offset not in reasons]
    status = "covered" if not missing else ("explained" if not unexplained else "NOT COVERED")
    print(f"{entry:04X}\t{name}\t{len(reachable) - len(missing)} of {len(reachable)} instructions\t{status}")
    for offset in missing:
      print(f"\t{offset:04X}\t{reasons.get(offset, 'no comparison reached it')}")
    failed = failed or bool(unexplained)
  return 1 if failed else 0


if __name__ == "__main__":
  sys.exit(main())
