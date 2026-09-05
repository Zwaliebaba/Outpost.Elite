#!/usr/bin/env python3
"""Coverage ledger for the Elite port.

Three jobs, per Design/ADR-004 section 5:

  1. --check-includes  Every INCLUDE and INCBIN path in MasterFile/*.asm resolves in the
                       vendored upstream tree.  This is slice 0a's acceptance criterion and
                       the thing that catches the masters and the library drifting apart.

  2. (default)         The coverage report: how many upstream library files are accounted
                       for in Design/Source-Inventory.md, and which ported functions in
                       GameLogic/ carry a "// 6502: LABEL" marker (AGENTS.md R7).

  3. --strict          Exit non-zero if any library file is unaccounted for.  Green since
                       2026-09-05 (plan section 6.120), and CI runs it on every push.

A ledger row names its files in backticks, and two shorthands count as naming: a multi-part
routine's family is accounted for by any one `x_part_N_of_M` in it, and `bdro1`–`bdro15` accounts
for every number between.  The unit is the master-level include; the files those include in turn
are workspace fields and binaries, covered by their parent's row.

Run from the repository root.  Python 3.9+, no third-party packages.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MASTERS = REPO / "MasterFile"
UPSTREAM = REPO / "Upstream" / "elite-source-code-library"
LEDGER = REPO / "Design" / "Source-Inventory.md"
PORT = REPO / "GameLogic"

INCLUDE_RE = re.compile(r'^\s*INC(?:LUDE|BIN)\s+"([^"]+)"', re.MULTILINE)
MARKER_RE = re.compile(r"//\s*6502:\s*([^\n]+)")
# A ledger row names its labels in backticks in the first column.
BACKTICK_RE = re.compile(r"`([^`]+)`")


def master_includes() -> dict[str, list[str]]:
    """Every INCLUDE/INCBIN path in the master files, mapped to the masters that use it."""
    found: dict[str, list[str]] = {}
    if not MASTERS.is_dir():
        sys.exit(f"error: {MASTERS} not found -- run from the repository root")
    for asm in sorted(MASTERS.glob("*.asm")):
        text = asm.read_text(encoding="utf-8", errors="replace")
        for path in INCLUDE_RE.findall(text):
            found.setdefault(path.strip(), []).append(asm.name)
    return found


def check_includes(paths: dict[str, list[str]]) -> int:
    if not UPSTREAM.is_dir():
        print(f"FAIL  upstream tree not found at {UPSTREAM}")
        print("      Slice 0a: clone markmoxon/elite-source-code-library there.")
        return 1

    missing = [p for p in sorted(paths) if not (UPSTREAM / p).is_file()]
    library = [p for p in paths if p.startswith("library/")]
    print(f"master files      {len(list(MASTERS.glob('*.asm')))}")
    print(f"include paths     {len(paths)} distinct ({len(library)} under library/)")
    print(f"resolved          {len(paths) - len(missing)}/{len(paths)}")
    for path in missing:
        print(f"  MISSING  {path}   (from {', '.join(sorted(set(paths[path])))})")
    if missing:
        print(f"FAIL  {len(missing)} include path(s) do not resolve")
        return 1
    print("OK    every INCLUDE and INCBIN resolves in the upstream tree")
    return 0


PART_RE = re.compile(r"part_(\d+)_of_(\d+)")
# `bdro1`–`bdro15`, `ks1`–`ks4`: two backtick groups with one stem and a number, joined by a dash.
RANGE_RE = re.compile(r"`([A-Za-z_][A-Za-z_\-]*?)(\d+)`\s*[\u2013\-]\s*`\1(\d+)`")


def expand_label(label: str) -> set[str]:
    """The stems one backtick group accounts for.

    A multi-part routine is one routine that the annotation split into files, and the ledger names
    the family once: `mveit_part_1_of_9` … `part_9_of_9`. Naming any part accounts for every part
    with the same `_of_N`, and the substitution runs over the whole group so that a compound stem
    (`loin_part_1_of_7-loinq_part_1_of_7`) expands as a whole.
    """
    stems = {label}
    parts = PART_RE.findall(label)
    if parts:
        total = int(parts[0][1])
        for index in range(1, total + 1):
            stems.add(PART_RE.sub(lambda m: f"part_{index}_of_{m.group(2)}", label))
    return stems


def ledger_labels() -> set[str]:
    """Label stems named in backticks anywhere in the source inventory, ranges expanded."""
    if not LEDGER.is_file():
        return set()
    text = LEDGER.read_text(encoding="utf-8", errors="replace")
    labels: set[str] = set()
    for hit in BACKTICK_RE.findall(text):
        # Rows list labels one per backtick group; normalise to the include file stem style.
        for stem in expand_label(hit.strip().lower()):
            labels.add(stem)
    for stem, first, last in RANGE_RE.findall(text):
        for index in range(int(first), int(last) + 1):
            labels.add(f"{stem}{index}".lower())
    return labels


def port_markers() -> dict[str, list[str]]:
    """"// 6502: LABEL" markers in the port, mapped to the files that carry them."""
    markers: dict[str, list[str]] = {}
    if not PORT.is_dir():
        return markers
    for src in sorted(list(PORT.glob("*.h")) + list(PORT.glob("*.cpp"))):
        text = src.read_text(encoding="utf-8", errors="replace")
        for hit in MARKER_RE.findall(text):
            # "DORND -- generate ..." or "RAND (four bytes)": keep the labels, drop the prose.
            head = re.split(r"\s+[-—]{1,2}\s+", hit.strip())[0]
            head = re.split(r"\(", head)[0]
            for label in re.split(r"[,/]| and ", head):
                label = label.strip().rstrip(".").lower()
                if label:
                    markers.setdefault(label, []).append(src.name)
    return markers


def report(paths: dict[str, list[str]], strict: bool) -> int:
    library = sorted(p for p in paths if p.startswith("library/"))
    stems = {Path(p).stem.lower(): p for p in library}
    labels = ledger_labels()
    markers = port_markers()

    accounted = {s for s in stems if s in labels}
    unaccounted = sorted(set(stems) - accounted)
    ported = {s for s in stems if s in markers}

    print(f"library files       {len(library)} paths, {len(stems)} distinct stems")
    print(f"named in ledger     {len(accounted)}")
    print(f"unaccounted         {len(unaccounted)}")
    print(f"ported (// 6502:)   {len(ported)}")
    print(f"port markers found  {len(markers)} distinct label(s) in {PORT.name}/")

    # A marker may legitimately name something that is not its own file: a second entry point
    # into a routine (DORND2 lives inside dornd.asm), or a workspace field. Those are reported
    # so a typo is visible, but they are not failures on their own.
    orphans = sorted(m for m in markers if m not in stems)
    if orphans:
        print("\nmarkers naming no library file of their own (entry point, workspace field, or typo):")
        for label in orphans:
            print(f"  {label}   in {', '.join(sorted(set(markers[label])))}")

    if unaccounted and strict:
        print("\nunaccounted library files (first 40):")
        for stem in unaccounted[:40]:
            print(f"  {stems[stem]}")
        print(f"\nFAIL  {len(unaccounted)} library file(s) have no row in {LEDGER.name}")
        return 1

    # Orphan markers are reported but never fatal: entry points are real and have no file.

    print("\nOK    (run with --strict to fail on unaccounted files)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check-includes", action="store_true", help="resolve every master INCLUDE against Upstream/")
    parser.add_argument("--strict", action="store_true", help="fail when a library file has no ledger row")
    args = parser.parse_args()

    paths = master_includes()
    if args.check_includes:
        return check_includes(paths)
    return report(paths, args.strict)


if __name__ == "__main__":
    sys.exit(main())
