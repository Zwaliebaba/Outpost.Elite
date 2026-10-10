#!/usr/bin/env python3
"""Check that comparisons exercised every reachable instruction of each ported routine (plan §5 Phase 3).

A native routine is accepted when it matched the original and the runs that showed it covered every
reachable instruction of the code it replaces (ADR-010 item 5). What counts as matching depends on
whether the routine waits (NativeWait):

- A routine that never waits, or waits only sometimes, is compared call by call. Its instructions are
  those of the routine and of everything it calls or runs into, short of a routine that always waits;
  each must have run in a compared call. A native report (*.tsv) lists, for each hooked entry, the
  offsets the original executed in the calls that were compared.
- A routine that always waits is never compared on its own: the digests compare it, an interpreted run
  with the native one. Its instructions are those of the routine and of what it calls or runs into,
  short of any other hooked entry; each must have run in an interpreted run whose digests the native
  run reproduces. An offsets file (*.offsets) lists the instructions such a run executed.

GameLogicTests writes both into OutpostEliteNativeReports under the system's temporary directory: a
native report for each compared corpus replay and constructed test, and an offsets file for each
interpreted corpus replay and twin test. ReferenceRunner writes them for a run with --compare
--native-report FILE and --coverage FILE. This tool walks the code statically, as MapReference.py does,
and lists the instructions nothing ran. Each one needs a replay or a test that reaches it, or a written
reason in Design/NativeCoverage.tsv; a reason for an instruction the runs now cover is stale, and fails too.

    python Tools/RoutineCoverage.py [REPORT.tsv | RUN.offsets | DIRECTORY ...]

With no argument it reads everything GameLogicTests left. CI runs it so after the tests.

Development tool only (AGENTS.md R14). Needs Capstone, as MapReference.py does. Exit status 1 when an
instruction is neither covered nor explained, or a reason is stale. Read a partial set of reports with that
in mind: an instruction no hooked routine of theirs reaches reads as stale.
"""

import argparse
import csv
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import MapReference as Map  # noqa: E402  (a sibling tool, imported for its walk)

REASONS = Map.ROOT / "Design" / "NativeCoverage.tsv"
# Where GameLogicTests leaves its reports (GameLogicTests/ComparisonRig.h).
TEST_REPORTS = Path(tempfile.gettempdir()) / "OutpostEliteNativeReports"


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


def closure_short_of(routines: dict, root: int, stops: set[int]) -> set[int]:
  """Every routine reachable from root by call or by running on into another, not entering those in stops."""
  seen: set[int] = set()
  pending = [root]
  while pending:
    entry = pending.pop()
    if entry in seen or entry not in routines or (entry != root and entry in stops):
      continue
    seen.add(entry)
    pending += list(routines[entry].calls) + list(routines[entry].leaves_to)
  return seen


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("reports", nargs="*", type=Path, default=[TEST_REPORTS],
                      help=f"native reports and offsets files, or directories of them (default {TEST_REPORTS})")
  arguments = parser.parse_args()
  files = [file for path in arguments.reports
           for file in (sorted([*path.glob("*.tsv"), *path.glob("*.offsets")]) if path.is_dir() else [path])]
  reports = [file for file in files if file.suffix == ".tsv"]
  runs = [file for file in files if file.suffix == ".offsets"]
  if not reports:
    print(f"no native report in {' '.join(str(path) for path in arguments.reports)}: run GameLogicTests first", file=sys.stderr)
    return 1

  hooked: dict[int, str] = {}
  waits: dict[int, str] = {}
  compared: set[int] = set()
  for report in reports:
    with report.open(encoding="utf-8", newline="") as file:
      for row in csv.DictReader(file, delimiter="\t"):
        entry = int(row["entry"], 16)
        hooked[entry] = row["routine"]
        waits[entry] = row.get("wait") or "never"
        compared.update(int(offset, 16) for offset in row["executed"].split())
  interpreted: set[int] = set()
  for run in runs:
    interpreted.update(int(line, 16) for line in run.read_text(encoding="utf-8").split())

  data = (Map.ROOT / "ELITES.EXE").read_bytes()
  image = data[struct.unpack_from("<H", data, 8)[0] * 16:]
  code = image[:Map.CODE_SEGMENT_BYTES]
  _, rows = Map.read_table(Map.ROOT / "Design" / "Symbols.tsv")
  entries = {row.offset for row in rows.values() if row.segment == "CS" and row.kind in ("routine", "entry", "isr")}
  instructions = Map.decode_all(code, entries | set(hooked))
  call_targets = {Map.branch_target(i) for i in instructions.values() if i.mnemonic == "call"} - {None}
  routines = Map.build_routines(instructions, (entries | call_targets | set(hooked)) & set(instructions))
  reasons = read_reasons()

  always = {entry for entry, wait in waits.items() if wait == "always"}
  failed = False
  uncovered: set[int] = set()
  for entry, name in sorted(hooked.items(), key=lambda _item: _item[0]):
    # A routine that always waits answers for itself and what it alone runs; any other answers for all
    # it reaches, short of a routine that always waits.
    if entry in always:
      members, executed, kind = closure_short_of(routines, entry, set(hooked) - {entry}), interpreted, " (digests)"
    else:
      members, executed, kind = closure_short_of(routines, entry, always - {entry}), compared, ""
    reachable = {address for member in members for address in routines[member].instructions}
    missing = sorted(reachable - executed)
    uncovered.update(missing)
    unexplained = [offset for offset in missing if offset not in reasons]
    status = ("covered" if not missing else ("explained" if not unexplained else "NOT COVERED")) + kind
    print(f"{entry:04X}\t{name}\t{len(reachable) - len(missing)} of {len(reachable)} instructions\t{status}")
    for offset in missing:
      print(f"\t{offset:04X}\t{reasons.get(offset, 'no comparison reached it')}")
    failed = failed or bool(unexplained)
  # A reason for an instruction the runs now cover is stale: it would explain away a regression.
  stale = sorted(set(reasons) - uncovered)
  for offset in stale:
    print(f"stale\t{offset:04X}\tlisted in {REASONS.name}, but every routine that reaches it is covered there")
  return 1 if failed or stale else 0


if __name__ == "__main__":
  sys.exit(main())
