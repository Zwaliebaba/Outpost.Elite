#!/usr/bin/env python3
"""Check that the numbers the documents state about this tree are the numbers the tree has.

WHY THIS EXISTS. The 2026-09-05 documentation pass (plan section 6.145) read every `.md` against
the tree and found that the PROSE had held up -- no ADR had been overtaken, no decision recorded
had been made differently -- while every NUMBER and every STATUS had quietly rotted. "313 tests",
"52 translation units", "24 slices", "Proposed": each was true when written, none of them
announces that it has stopped being true, and the reasoning around them was still correct three
days and eleven slices later. That pass ended by naming the tool that would stop it happening
again, which is this one.

THE MARKER, AND WHY THERE IS ONE. This corpus is largely a JOURNAL. The plan says "321 tests" and
"304 tests" in entries that were true on the day they were written and must stay exactly as they
are; only a handful of sentences are claims about NOW. A checker that scanned for "N tests" would
fail on the history, so instead a live claim marks itself:

    the suite is <!--count:tests-->349 tests

The marker renders as nothing on GitHub, and it is the point rather than the plumbing: it says out
loud which numbers are claims about the current tree. An unmarked number is history, and history is
not checked. So this does not make every number in the corpus true -- it makes the LIVE ones
checkable, and leaves the rest visibly what they are.

The number after a marker may be digits (`349`, `5,577`) or English words (`nine`, `twenty-six`),
because the documents use both and neither spelling should have to change to be checked.

    python tools/check_counts.py            # verify every marked number
    python tools/check_counts.py --list     # print every count, for writing a new sentence

It also checks one thing that is not a marker, because it is the failure that started this: the
effort table in the plan's section 7 totalled 24 while its own rows added to 26. A table that
disagrees with itself needs no external truth to be caught, so the Total row is checked against
the sum of the phase rows above it.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKIP = {"Upstream", ".git", ".claude", ".vs", "node_modules", "out", "build", "packages", "x64", "Tools-ext"}

MARKER = re.compile(r"<!--\s*count:([a-z0-9-]+)\s*-->")

UNITS = {
    "zero": 0, "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7,
    "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12, "thirteen": 13,
    "fourteen": 14, "fifteen": 15, "sixteen": 16, "seventeen": 17, "eighteen": 18,
    "nineteen": 19,
}
TENS = {"twenty": 20, "thirty": 30, "forty": 40, "fifty": 50, "sixty": 60, "seventy": 70, "eighty": 80, "ninety": 90}


# ---- what the tree actually holds ---------------------------------------------------------------


def count_tests() -> int:
    """TEST_METHOD declarations across the suite -- what vstest and the portable runner both run."""
    total = 0
    for source in sorted((REPO / "Tests" / "GameLogicTests").glob("*.cpp")):
        total += len(re.findall(r"\bTEST_METHOD\s*\(", source.read_text(encoding="utf-8", errors="replace")))
    return total


def count_checks() -> int:
    """The repository checks, read out of check_all.py rather than counted by hand -- which is the
    mistake that produced check_all.py in the first place (section 6.127)."""
    text = (REPO / "tools" / "check_all.py").read_text(encoding="utf-8", errors="replace")
    body = text.split("CHECKS: list[list[str]] = [", 1)[1].split("\n]", 1)[0]
    return len(re.findall(r"^\s*\[", body, re.MULTILINE))


def count_master_lines() -> int:
    """Lines of assembly in MasterFile/ -- the .asm files ONLY.

    The corpus said "13 master files, 5,615 lines" from its opening paragraph onwards and both
    halves counted the FOLDER: there are twelve .asm files totalling 5,577 lines, plus upstream's
    own README.md, which is 38 lines of Markdown and not a master. `inventory.py` had been printing
    twelve the whole time.
    """
    total = 0
    for master in sorted((REPO / "MasterFile").glob("*.asm")):
        total += len(master.read_text(encoding="utf-8", errors="replace").splitlines())
    return total


def count_masterfile_lines() -> int:
    """Lines in every tracked file of MasterFile/, which is what the licence exposure is measured
    in -- upstream's README.md carries its own copyright and is committed like the rest."""
    total = 0
    for path in sorted((REPO / "MasterFile").iterdir()):
        if path.is_file():
            total += len(path.read_text(encoding="utf-8", errors="replace").splitlines())
    return total


def count_includes(_under_library: bool) -> int:
    """Distinct INCLUDE/INCBIN paths in the masters, which is inventory.py's own measurement."""
    paths: set[str] = set()
    pattern = re.compile(r'^\s*INC(?:LUDE|BIN)\s+"([^"]+)"', re.MULTILINE)
    for master in sorted((REPO / "MasterFile").glob("*.asm")):
        for path in pattern.findall(master.read_text(encoding="utf-8", errors="replace")):
            if not _under_library or path.startswith("library/"):
                paths.add(path)
    return len(paths)


def counts() -> dict[str, tuple[int, str]]:
    """Every checkable count: name -> (value, what it means)."""
    game_logic = REPO / "GameLogic"
    tests = REPO / "Tests" / "GameLogicTests"
    return {
        "tests": (count_tests(), "TEST_METHOD declarations in Tests/GameLogicTests/"),
        "test-files": (len(list(tests.glob("*Tests.cpp"))), "test translation units"),
        "checks": (count_checks(), "entries in check_all.py's CHECKS"),
        "gamelogic-sources": (len(list(game_logic.glob("*.cpp"))), "GameLogic/*.cpp, pch included"),
        "gamelogic-headers": (len(list(game_logic.glob("*.h"))), "GameLogic/*.h, pch included"),
        "masters": (len(list((REPO / "MasterFile").glob("*.asm"))), "MasterFile/*.asm"),
        "master-lines": (count_master_lines(), "lines of assembly in MasterFile/*.asm"),
        # And the same folder counted the other way, which is a DIFFERENT claim and both are made.
        # The exposure Risk R1 accepts is every tracked file in MasterFile/, upstream's own
        # README.md included; the annotated SOURCE is the twelve .asm files. Conflating them is
        # what put "13 master files, 5,615 lines" in the plan's opening paragraph for the source.
        "masterfile-files": (len([path for path in (REPO / "MasterFile").iterdir() if path.is_file()]), "all files in MasterFile/"),
        "masterfile-lines": (count_masterfile_lines(), "lines in all of MasterFile/"),
        "includes": (count_includes(False), "distinct INCLUDE/INCBIN paths in the masters"),
        "library-includes": (count_includes(True), "those of them under library/"),
        "tools": (len(list((REPO / "tools").glob("*.py"))), "scripts in tools/"),
    }


# ---- reading a number a document wrote ----------------------------------------------------------


def read_number(_text: str) -> int | None:
    """The first number in `_text`, as digits or as English words, or None.

    Markdown emphasis is stripped first, so `**349**` and `349` read the same -- a claim should not
    have to give up its formatting to be checked.
    """
    plain = _text.replace("*", "").replace("`", "").replace("_", "").strip()

    digits = re.match(r"(\d[\d,]*)", plain)
    if digits:
        return int(digits.group(1).replace(",", ""))

    words = re.match(r"([a-z]+)(?:[- ]([a-z]+))?", plain.lower())
    if not words:
        return None

    first, second = words.group(1), words.group(2)

    # A compound is tried FIRST and the bare word second, because "ten repository checks" and
    # "twenty-six slices" start the same way: greedily taking two words turns the first into
    # "ten-repository", and taking one turns the second into "twenty".
    if second and first in TENS and second in UNITS and 1 <= UNITS[second] <= 9:
        return TENS[first] + UNITS[second]
    if first in UNITS:
        return UNITS[first]
    if first in TENS:
        return TENS[first]
    return None


def markdown_files() -> list[Path]:
    return sorted(
        path
        for path in REPO.rglob("*.md")
        if not any(part in SKIP for part in path.relative_to(REPO).parts)
    )


def check_markers(_known: dict[str, tuple[int, str]]) -> tuple[list[str], int]:
    """One complaint per marked number that disagrees with the tree, and how many were checked."""
    complaints: list[str] = []
    checked = 0

    for path in markdown_files():
        lines = path.read_text(encoding="utf-8", errors="replace").split("\n")
        fenced = False
        for number, line in enumerate(lines, 1):
            if line.lstrip().startswith("```"):
                fenced = not fenced
                continue
            if fenced:
                continue  # a fenced block shows the SYNTAX; it never states a count

            # A marker inside `backticks` is an EXAMPLE of the syntax, not a claim -- which is how
            # the documents describe this check to a reader. Blank the code spans and the rest of
            # the line keeps its offsets, so a complaint still points at the right column.
            scannable = re.sub(r"`[^`]*`", lambda span: " " * len(span.group(0)), line)
            for match in MARKER.finditer(scannable):
                where = f"{path.relative_to(REPO).as_posix()}:{number}"
                name = match.group(1)

                if name not in _known:
                    complaints.append(f"{where}: unknown count '{name}' -- see --list for the names")
                    continue

                stated = read_number(line[match.end():match.end() + 48])
                if stated is None:
                    complaints.append(f"{where}: count:{name} is not followed by a number")
                    continue

                checked += 1
                actual = _known[name][0]
                if stated != actual:
                    complaints.append(f"{where}: says {stated:,} {name}, the tree has {actual:,}")

    return complaints, checked


# ---- the one table that can be checked against itself --------------------------------------------


def check_effort_table() -> list[str]:
    """The plan's section 7 Total row against the sum of its own phase rows.

    This is the failure that named the tool: the table totalled 24 while its rows added to 26,
    because 4e had been added to the build order and never counted and phase 0's 0b was one slice
    in one place and two in another. No external truth is needed to catch that, only arithmetic.
    """
    plan = REPO / "Design" / "Elite-Conversion-Plan.md"
    if not plan.is_file():
        return [f"{plan.name} not found"]

    phases = 0
    total: int | None = None
    line_of_total = 0
    in_table = False

    for number, line in enumerate(plan.read_text(encoding="utf-8", errors="replace").split("\n"), 1):
        if not line.strip().startswith("|"):
            in_table = False  # the table has ended
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) < 2:
            continue

        # The table is found by its own header rather than by a line number, because the plan has
        # dozens of tables and several of them start a row with a digit.
        if cells[0] == "Phase" and cells[1] == "Slices":
            in_table = True
            continue
        if not in_table:
            continue

        label = cells[0].replace("*", "").strip()
        value = read_number(cells[1])
        if value is None:
            continue

        if label.isdigit():
            phases += value
        elif label.lower() == "total":
            total, line_of_total = value, number

    if total is None:
        return ["Design/Elite-Conversion-Plan.md: section 7's effort table has no Total row"]
    if total != phases:
        return [
            f"Design/Elite-Conversion-Plan.md:{line_of_total}: the effort table totals {total} slices"
            f" and its own phase rows add to {phases}"
        ]
    return []


def main(_argv: list[str]) -> int:
    known = counts()

    if "--list" in _argv:
        width = max(len(name) for name in known)
        for name, (value, meaning) in known.items():
            print(f"{name:<{width}}  {value:>6,}   {meaning}")
        return 0

    complaints, checked = check_markers(known)
    complaints += check_effort_table()

    print(f"markdown files   {len(markdown_files())}")
    print(f"marked numbers   {checked}")

    if complaints:
        print("FAIL  numbers that no longer describe the tree:")
        for complaint in complaints:
            print(f"      {complaint}")
        print("      (`python tools/check_counts.py --list` prints what the tree actually holds)")
        return 1

    print(f"OK    {checked} marked numbers agree with the tree")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
