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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import c64_source  # noqa: E402 -- the C64 branch filter, for the coverage review (M6-0-f)

REPO = Path(__file__).resolve().parent.parent
MASTERS = REPO / "MasterFile"
UPSTREAM = REPO / "Upstream" / "elite-source-code-library"
LEDGER = REPO / "Design" / "Source-Inventory.md"
PORT = REPO / "GameLogic"

# Where a ledger row's `Home` may point. A row names a file the port actually has, or the row is a
# claim about the tree that has stopped being true.
HOME_FOLDERS = ("GameLogic", "Outpost", "NeuronCore", "Tests/GameLogicTests", "Tests/PortableRunner/Shim")

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


def ledger_homes() -> list[tuple[int, str, str]]:
    """Every `.h`/`.cpp` name in a ledger row's HOME cell, as (line number, row label, file name).

    THE HOME CELL IS THE ROW'S CLAIM ABOUT THE TREE and the notes beside it are its HISTORY. A note
    that says a routine was built "in `Spawner.cpp` as one function" was true on the day it was
    written and stays; the plan's own rule for numbers says the same thing (AGENTS.md: journal
    numbers are history and are never touched). Two of the notes go further and name a file to say
    it does NOT exist -- §6.129's raster row, and the workspace row M5-c corrected -- so a check
    over the whole file would demand that a finding be deleted to go green. So this reads the third
    cell of a table row and nothing else.
    """
    if not LEDGER.is_file():
        return []
    found: list[tuple[int, str, str]] = []
    for number, line in enumerate(LEDGER.read_text(encoding="utf-8", errors="replace").split("\n"), start=1):
        if not line.startswith("|"):
            continue
        cells = line.split("|")
        if len(cells) < 5:
            continue
        label = cells[1].strip()
        # A separator row (|---|---|) names nothing and a header row has no backticks either.
        for name in re.findall(r"`([A-Za-z0-9_]+\.(?:h|cpp))`", cells[3]):
            found.append((number, label, name))
    return found


def check_homes() -> int:
    """Every file a ledger row's HOME cell names exists in one of the port's folders."""
    on_disk: set[str] = set()
    for folder in HOME_FOLDERS:
        directory = REPO / folder
        if directory.is_dir():
            on_disk.update(path.name for path in directory.iterdir() if path.is_file())

    homes = ledger_homes()
    stale = [(number, label, name) for number, label, name in homes if name not in on_disk]

    print(f"ledger rows with a home   {len({number for number, _, _ in homes}):>4}")
    print(f"file names in those homes {len(homes):>4}")
    if stale:
        print("\nFAIL  a ledger row's home names a file the tree does not have:")
        for number, label, name in stale:
            print(f"      line {number}: {name} -- {label[:70]}")
        print("\n      A home is where the labels LIVE. Correct the cell to the file that holds them,")
        print("      and leave the notes alone: they are history and say where things used to go.")
        return 1

    print("\nOK    every file a ledger row's home names is on disk")
    return 0


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


# ---- the coverage review (M6-0-f) ---------------------------------------------------------------

LABEL_DEF_RE = re.compile(r"^\.(\w+)", re.MULTILINE)
# A Port row exempts the stems no test runs with a comment in the row that says which and why:
#   <!--uncovered: stem stem -- why no test calls them-->
EXEMPT_RE = re.compile(r"<!--uncovered:\s*([^-]*?)\s*--\s*(.*?)-->")


def read_coverage(path: Path) -> tuple[set[str], set[str], int]:
    """The portable runner's coverage file: per test, a tab, labels executed, a tab, labels trapped.

    Returns the union of executed labels, the union of trapped labels, and how many tests wrote a line.
    """
    executed: set[str] = set()
    trapped: set[str] = set()
    tests = 0
    for line in path.read_text(encoding="utf-8", errors="replace").split("\n"):
        if not line.strip():
            continue
        cells = line.split("\t")
        tests += 1
        if len(cells) > 1:
            executed.update(cells[1].split())
        if len(cells) > 2:
            trapped.update(cells[2].split())
    return executed, trapped, tests


def row_stems(cells: list[str]) -> set[str]:
    """The include-file stems one ledger row names, families and ranges expanded, as `ledger_labels` reads them."""
    label_cell = cells[1]
    stems: set[str] = set()
    for hit in BACKTICK_RE.findall(label_cell):
        for stem in expand_label(hit.strip().lower()):
            stems.add(stem)
    for stem, first, last in RANGE_RE.findall(label_cell):
        for index in range(int(first), int(last) + 1):
            stems.add(f"{stem}{index}".lower())
    return stems


def file_labels(paths: dict[str, list[str]]) -> dict[str, set[str]]:
    """Every `.LABEL` an included SUBROUTINE file defines, by the file's stem.

    Only `subroutine/` files can be run. A workspace, a variable or a macro file is bytes a routine
    reads, and a Port row that names one is porting data; those stems are skipped rather than
    failed, and the review says so.
    """
    labels: dict[str, set[str]] = {}
    for path in paths:
        if not path.startswith("library/") or "/subroutine/" not in path:
            continue
        source = UPSTREAM / path
        if not source.is_file():
            continue
        stem = Path(path).stem.lower()
        # The labels THIS BUILD assembles: a file's other branches name labels the C64 image has no
        # address for, and a label with no address can never be run (tools/c64_source.py).
        try:
            text = "\n".join(c64_source.filter_file(source, True, True))
        except Exception:  # noqa: BLE001 -- a file the filter cannot read is read raw, which only widens the set
            text = source.read_text(encoding="utf-8", errors="replace")
        labels.setdefault(stem, set()).update(LABEL_DEF_RE.findall(text))
    return labels


def check_coverage(paths: dict[str, list[str]], coverage_file: Path) -> int:
    """Every Port row's files have a label some test RAN, or the row says which do not and why.

    THIS IS THE REVIEW M6-a's ACCEPTANCE ASKS FOR, as a tool's output rather than a person's. A
    label a test only TRAPPED does not count: reached is not run, and a trapped routine's fixture
    compares the trap's answer, not the routine. A stem with no label of its own (a file of data,
    or of macros) cannot be run and is reported separately rather than failed.
    """
    if not coverage_file.is_file():
        print(f"FAIL  no coverage file at {coverage_file}")
        print("      Tests/PortableRunner/run_tests.sh --coverage <file> writes one")
        return 1
    executed, trapped, tests = read_coverage(coverage_file)
    labels = file_labels(paths)
    runnable = set(labels)  # subroutine files only; data files are skipped, and said so below

    def family_ran(stem: str) -> bool:
        """A part with no label of its own is entered by falling through from a sibling, so it
        is run when any part of its family is."""
        parts = PART_RE.findall(stem)
        if not parts:
            return False
        head = PART_RE.split(stem)[0]
        return any(labels.get(other, set()) & executed for other in labels if other.startswith(head) and PART_RE.search(other))

    port_rows = 0
    covered_stems = 0
    exempt_stems = 0
    unlabelled: list[str] = []
    failures: list[tuple[int, str, list[str]]] = []
    only_trapped: list[tuple[str, str]] = []
    for number, line in enumerate(LEDGER.read_text(encoding="utf-8", errors="replace").split("\n"), start=1):
        if not line.startswith("|"):
            continue
        cells = line.split("|")
        if len(cells) < 6 or not cells[4].strip().startswith("Port"):
            continue
        port_rows += 1
        exempt: set[str] = set()
        for stems_text, _why in EXEMPT_RE.findall(line):
            exempt.update(s.strip().lower() for s in stems_text.split())
        gaps: list[str] = []
        for stem in sorted(row_stems(cells)):
            if stem not in runnable:
                continue  # data, a macro, a workspace, or a name that is not a file of its own
            defined = labels.get(stem, set())
            if not defined:
                if family_ran(stem):
                    covered_stems += 1
                else:
                    unlabelled.append(stem)
                continue
            if defined & executed:
                covered_stems += 1
            elif stem in exempt:
                exempt_stems += 1
            else:
                gaps.append(stem)
                if defined & trapped:
                    only_trapped.append((stem, ", ".join(sorted(defined & trapped))))
        if gaps:
            failures.append((number, cells[1].strip()[:50], gaps))

    print(f"coverage file       {coverage_file} -- {tests} test(s), {len(executed)} label(s) run, {len(trapped)} trapped")
    print(f"Port rows           {port_rows}")
    print(f"stems run           {covered_stems}")
    print(f"stems exempted      {exempt_stems}")
    if unlabelled:
        print(f"parts with no label {len(unlabelled)}, whose family no test runs: {', '.join(sorted(unlabelled))}")
    if only_trapped:
        print("\nreached only through a trap, which is not run:")
        for stem, at in only_trapped:
            print(f"  {stem}   trapped at {at}")
    if failures:
        print("\nPort rows with a file no test runs and no exemption saying why:")
        for number, label, gaps in failures:
            print(f"  line {number}  {label} ...")
            print(f"           {', '.join(gaps)}")
        print("\n      Give each a test that runs it, or the row a `<!--uncovered: stem -- why-->` note.")
        print(f"\nFAIL  {sum(len(g) for _, _, g in failures)} stem(s) in {len(failures)} Port row(s) are run by no test")
        return 1
    print("\nOK    every Port row's files are run by some test, or the row says which are not and why")
    return 0


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


def self_test() -> int:
    """A ledger whose home names a file that is not there must fail, and one that does must pass.

    The trap is the one this check exists for: a note that names a missing file is HISTORY and must
    NOT fail, so the sample carries both and only the home is counted.
    """
    global LEDGER, REPO  # noqa: PLW0603 -- the sample tree stands in for the repository
    import tempfile

    real_ledger, real_repo = LEDGER, REPO
    complaints: list[str] = []
    try:
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "GameLogic").mkdir()
            (root / "GameLogic" / "Here.cpp").write_text("// present\n", encoding="utf-8")
            REPO = root
            LEDGER = root / "Ledger.md"

            LEDGER.write_text(
                "| Labels | N | Home | Disposition | Notes |\n"
                "|---|---|---|---|---|\n"
                "| `alpha` | 1 | `Here.cpp` | Port | Was in `Gone.cpp` until it moved. |\n",
                encoding="utf-8",
            )
            if check_homes() != 0:
                complaints.append("self-test: a clean home failed, and a note naming `Gone.cpp` must not count")

            LEDGER.write_text(
                "| Labels | N | Home | Disposition | Notes |\n"
                "|---|---|---|---|---|\n"
                "| `alpha` | 1 | `Here.cpp`, `Gone.cpp` | Port | notes |\n",
                encoding="utf-8",
            )
            if check_homes() == 0:
                complaints.append("self-test: a planted stale home was not caught")
    finally:
        LEDGER, REPO = real_ledger, real_repo

    for line in complaints:
        print(line)
    if complaints:
        print(f"\nFAIL  {len(complaints)} self-test failure(s)")
        return 1
    print("\nOK    self-test passed: a planted stale home was caught and a stale NOTE was not")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check-includes", action="store_true", help="resolve every master INCLUDE against Upstream/")
    parser.add_argument("--strict", action="store_true", help="fail when a library file has no ledger row")
    parser.add_argument("--check-homes", action="store_true", help="every file a ledger row's home names is on disk")
    parser.add_argument("--self-test", action="store_true", help="prove --check-homes catches a planted stale home")
    parser.add_argument("--coverage", metavar="FILE", help="every Port row's files are run by some test in this coverage file (M6-0-f)")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.check_homes:
        return check_homes()

    paths = master_includes()
    if args.check_includes:
        return check_includes(paths)
    if args.coverage:
        return check_coverage(paths, Path(args.coverage))
    return report(paths, args.strict)


if __name__ == "__main__":
    sys.exit(main())
