#!/usr/bin/env python3
"""The modernisation ratchet (Design/Modernize.md section 3 and rule 5 of section 5).

Design/Modernize.md names eleven legacy patterns the port carries -- register-shaped parameters,
numeric ship-byte indices, reference aggregates, phase-order seams and the rest -- and measures
each one. A measurement in a document is a claim that ages (AGENTS.md section 6), so the counts
live here, where they are re-taken on every run, and the document reaches them through
`check_counts.py`'s `<!--count:NAME-->` markers.

The RATCHET is the second half. `tools/modernize_ratchet.json` records a ceiling per pattern and
this check fails in either direction:

  - a count ABOVE its ceiling means a change reintroduced a pattern a slice removed, and the
    build says so before review does;
  - a count BELOW its ceiling by more than the recorded slack means the file has stopped
    describing the tree, and the slice that lowered the count lowers the ceiling in the same
    commit (with a journal entry naming itself -- Risk R18).

    python tools/check_modernize.py              # the ratchet, after its own self-test
    python tools/check_modernize.py --list       # what the tree holds right now
    python tools/check_modernize.py --update     # rewrite the ceilings to the tree (a slice landing)
    python tools/check_modernize.py --self-test  # prove each counter still detects what it claims

WHAT THE COUNTERS ARE NOT. They are regular expressions over source with comments stripped, and a
regular expression is a good-enough detector rather than a parser: `ship-literal-sites` counts
`work[31]` and would count a `work[31]` in a string literal too. The self-test below is what stops
a counter quietly counting nothing (section 6.154's rule, applied to the tool that enforces it).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RATCHET = REPO / "tools" / "modernize_ratchet.json"


# ---- reading source -------------------------------------------------------------------------------


def strip_comments(_text: str) -> str:
    """Comments describe the patterns on purpose; only code is counted."""
    without_blocks = re.sub(r"/\*.*?\*/", " ", _text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", without_blocks)


def read_stripped(_paths: list[Path]) -> str:
    return "\n".join(strip_comments(path.read_text(encoding="utf-8", errors="replace")) for path in _paths)


def headers(_root: Path) -> list[Path]:
    return sorted((_root / "GameLogic").glob("*.h"))


def sources(_root: Path) -> list[Path]:
    return sorted((_root / "GameLogic").glob("*.cpp"))


# ---- the counters, one per pattern ------------------------------------------------------------------

REGISTER_PARAM = re.compile(r"\bstd::uint8_t\s+_(?:a|x|y)\b")
SHIP_LITERAL = re.compile(r"\b(?:_?work|_?block|_?ship|_?other|blocks\[[a-z]+\])\[[0-9]+u?\]")
SHIP_OFFSET = re.compile(r"\[SHIP_(?!TYPE_)[A-Z_0-9]+\]")  # `counts[SHIP_TYPE_x]` is a type, not a byte of a block
AGGREGATE_REF = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_:<>]*&\s+[a-z][A-Za-z0-9]*;", re.MULTILINE)
CARRY_PARAM = re.compile(r"\bbool\s+_carryIn\b")
OUT_PARAM = re.compile(r"\bstd::uint8_t&\s+_[a-z][A-Za-z0-9]*")
WORKSPACE_PARAM = re.compile(r"\b(?:Math|Draw|Geometry)Workspace&\s+_[a-z]")
CLASS_HEAD = re.compile(r"\b(?:class|struct)\s+[A-Za-z_]\w*\s*(?:final\s*)?(?::[^{;]*)?\{")
PURE_VIRTUAL = re.compile(r"\)\s*(?:const\s*)?(?:noexcept\s*)?=\s*0\s*;")
ELITE_NAME = re.compile(r"\bElite::([A-Za-z_]\w*)")
LEDGER_FILE = re.compile(r"`([A-Za-z0-9]+\.(?:h|cpp))`")
ORIGIN_MARKER = re.compile(r"\b6502:")
ORACLE_USE = re.compile(r"\bOracleImage\b|\bOracleMissing\b")
ORIGIN_PATH = re.compile(r"\bUpstream\b|\bMasterFile\b")


def count_register_params(_root: Path) -> int:
    """P1 -- parameters that are a 6502 register: `std::uint8_t _a`, `_x`, `_y` in the headers."""
    return len(REGISTER_PARAM.findall(read_stripped(headers(_root))))


def count_ship_literal_sites(_root: Path) -> int:
    """P3 -- a ship block indexed by a NUMBER: `work[31]`, `_ship[8]`, `blocks[slot][36]`."""
    return len(SHIP_LITERAL.findall(read_stripped(sources(_root))))


def count_ship_offset_sites(_root: Path) -> int:
    """P3 -- a ship block indexed by a named offset constant: `work[SHIP_STATE_OFFSET]`. A ship TYPE in
    brackets (`counts[SHIP_TYPE_STATION]`) indexes the per-type tally and is not one."""
    return len(SHIP_OFFSET.findall(read_stripped(sources(_root))))


def count_aggregate_refs(_root: Path) -> int:
    """P5 -- reference members of the argument-list structs (`Canvas& canvas;`) in the headers."""
    return len(AGGREGATE_REF.findall(read_stripped(headers(_root))))


def count_effects_seams(_root: Path) -> int:
    """P7 -- classes in the headers that declare at least one pure virtual method at their own depth.

    A brace walk rather than a regex over the whole file, so that a class nested in another is
    counted for itself and its owner is not counted for it.
    """
    seams = 0
    for path in headers(_root):
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for head in CLASS_HEAD.finditer(text):
            depth = 1
            index = head.end()
            body: list[str] = []
            while index < len(text) and depth > 0:
                char = text[index]
                if char == "{":
                    depth += 1
                elif char == "}":
                    depth -= 1
                if depth == 1:
                    body.append(char)
                index += 1
            if PURE_VIRTUAL.search("".join(body)):
                seams += 1
    return seams


def count_carry_params(_root: Path) -> int:
    """P11 -- `bool _carryIn` parameters in the headers, kernel and boundary alike."""
    return len(CARRY_PARAM.findall(read_stripped(headers(_root))))


def count_out_params(_root: Path) -> int:
    """P10 -- a byte handed back through a reference parameter: `std::uint8_t& _docked`."""
    return len(OUT_PARAM.findall(read_stripped(headers(_root))))


def count_workspace_params(_root: Path) -> int:
    """P2 -- parameters that are a zero-page scratch struct: `MathWorkspace& _math` and kin."""
    return len(WORKSPACE_PARAM.findall(read_stripped(headers(_root))))


def count_main_lines(_root: Path) -> int:
    """P6 -- the size of the composition root, which holds the dispatch and both outer loops."""
    return len((_root / "Outpost" / "Main.cpp").read_text(encoding="utf-8", errors="replace").splitlines())


def count_outpost_elite_names(_root: Path) -> int:
    """P6 -- distinct `Elite::` names the executable reaches, which is the surface only MSVC checks."""
    names: set[str] = set()
    app = _root / "Outpost"
    for path in sorted(list(app.glob("*.cpp")) + list(app.glob("*.h"))):
        names.update(ELITE_NAME.findall(strip_comments(path.read_text(encoding="utf-8", errors="replace"))))
    return len(names)


def count_mutants(_root: Path) -> int:
    """The recorded mutants, which every renaming slice has to re-anchor (rule 3)."""
    recorded = json.loads((_root / "tools" / "mutants.json").read_text(encoding="utf-8"))
    return sum(len(unit["mutants"]) for unit in recorded["units"])


def count_inventory_stale_files(_root: Path) -> int:
    """M5-c -- `.h`/`.cpp` names the ledger cites that name no file in any project folder."""
    ledger = _root / "Design" / "Source-Inventory.md"
    cited = set(LEDGER_FILE.findall(ledger.read_text(encoding="utf-8", errors="replace")))
    on_disk: set[str] = set()
    for folder in ("GameLogic", "Outpost", "NeuronCore", "Tests/GameLogicTests", "Tests/PortableRunner/Shim"):
        directory = _root / folder
        if directory.is_dir():
            on_disk.update(path.name for path in directory.iterdir() if path.is_file())
    return len([name for name in cited if name not in on_disk])


def count_origin_markers(_root: Path) -> int:
    """P12 -- `6502:` references in GameLogic/ -- the `//` markers inventory.py reads and the `*`-prefixed
    ones inside block comments alike -- read from the RAW text because they are comments."""
    total = 0
    for path in headers(_root) + sources(_root):
        total += len(ORIGIN_MARKER.findall(path.read_text(encoding="utf-8", errors="replace")))
    return total


def count_oracle_test_files(_root: Path) -> int:
    """P12 -- test translation units that load the assembled original through OracleImage."""
    tests = _root / "Tests" / "GameLogicTests"
    return len([path for path in sorted(tests.glob("*Tests.cpp"))
                if ORACLE_USE.search(strip_comments(path.read_text(encoding="utf-8", errors="replace")))])


def count_origin_tools(_root: Path) -> int:
    """P12 -- scripts in tools/ that read Upstream/ or MasterFile/ (this one reads neither)."""
    total = 0
    for path in sorted((_root / "tools").glob("*.py")):
        if path.name == Path(__file__).name:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        code = "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))
        if ORIGIN_PATH.search(code):
            total += 1
    return total


COUNTERS = {
    "register-params": (count_register_params, "P1: std::uint8_t _a/_x/_y parameters in GameLogic/*.h"),
    "workspace-params": (count_workspace_params, "P2: zero-page workspace reference parameters in GameLogic/*.h"),
    "ship-literal-sites": (count_ship_literal_sites, "P3: ship blocks indexed by a numeric literal in GameLogic/*.cpp"),
    "ship-offset-sites": (count_ship_offset_sites, "P3: ship blocks indexed by a SHIP_* offset constant in GameLogic/*.cpp"),
    "aggregate-refs": (count_aggregate_refs, "P5: reference members of the argument-list structs in GameLogic/*.h"),
    "main-lines": (count_main_lines, "P6: lines in Outpost/Main.cpp"),
    "outpost-elite-names": (count_outpost_elite_names, "P6: distinct Elite:: names Outpost/ reaches"),
    "effects-seams": (count_effects_seams, "P7: abstract classes (a pure virtual at their own depth) in GameLogic/*.h"),
    "out-params": (count_out_params, "P10: std::uint8_t& output parameters in GameLogic/*.h"),
    "carry-params": (count_carry_params, "P11: bool _carryIn parameters in GameLogic/*.h"),
    "mutants": (count_mutants, "recorded mutants in tools/mutants.json"),
    "inventory-stale-files": (count_inventory_stale_files, "file names Source-Inventory.md cites that are not on disk"),
    "origin-markers": (count_origin_markers, "P12: 6502: references in GameLogic/ comments"),
    "oracle-test-files": (count_oracle_test_files, "P12: test files that load the assembled original"),
    "origin-tools": (count_origin_tools, "P12: tools that read Upstream/ or MasterFile/"),
}


def counts(_root: Path = REPO) -> dict[str, tuple[int, str]]:
    """Every count, name -> (value, what it means). `check_counts.py` merges this into its own."""
    return {name: (counter(_root), meaning) for name, (counter, meaning) in COUNTERS.items()}


# ---- the ratchet -----------------------------------------------------------------------------------


def load_ratchet() -> dict[str, dict[str, int | str]]:
    if not RATCHET.is_file():
        return {}
    return json.loads(RATCHET.read_text(encoding="utf-8"))["ceilings"]


def check_ratchet(_measured: dict[str, tuple[int, str]]) -> list[str]:
    ceilings = load_ratchet()
    complaints: list[str] = []

    for name, (value, _meaning) in _measured.items():
        if name not in ceilings:
            complaints.append(f"{name}: no ceiling recorded in {RATCHET.name} -- run --update and journal it")
            continue
        entry = ceilings[name]
        ceiling = int(entry["ceiling"])
        slack = int(entry.get("slack", 0))
        if value > ceiling:
            complaints.append(f"{name}: {value} is above its ceiling of {ceiling} -- a pattern Design/Modernize.md removes came back")
        elif ceiling - value > slack:
            complaints.append(f"{name}: {value} is below its ceiling of {ceiling} -- lower the ceiling (--update) and journal the slice")

    for name in ceilings:
        if name not in _measured:
            complaints.append(f"{name}: a ceiling with no counter -- remove it from {RATCHET.name}")

    return complaints


def write_ratchet(_measured: dict[str, tuple[int, str]]) -> None:
    existing = load_ratchet()
    ceilings: dict[str, dict[str, int | str]] = {}
    for name, (value, _meaning) in _measured.items():
        previous = existing.get(name, {})
        ceilings[name] = {"ceiling": value, "slack": int(previous.get("slack", 0)), "slice": str(previous.get("slice", "M0-a"))}
    document = {
        "$schema-note": [
            "The ceilings tools/check_modernize.py holds the tree to (Design/Modernize.md section 5, rule 5).",
            "",
            "A count above its ceiling fails the build: a pattern a slice removed has come back. A count",
            "below it by more than `slack` fails too: the slice that lowered it lowers the ceiling here, in",
            "the same commit, and names itself in `slice` and in the plan's journal (Risk R18). `python",
            "tools/check_modernize.py --update` rewrites the numbers; the journal entry is yours to write.",
        ],
        "ceilings": ceilings,
    }
    RATCHET.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


# ---- the self-test ---------------------------------------------------------------------------------

SAMPLE_HEADER = """
#pragma once
namespace Elite
{
  struct MathWorkspace { std::uint8_t p = 0; };
  class Seam { public: virtual ~Seam() = default; virtual void Put(std::uint8_t _c) = 0; };
  class NotASeam { public: void Put(std::uint8_t _c); class Inner { public: virtual bool Go() const noexcept = 0; }; };
  struct Screen
  {
    Canvas& canvas;
    MathWorkspace& math;
    std::uint8_t& view;
  };
  // std::uint8_t _a in a comment does not count, and neither does bool _carryIn here
  /// 6502: MAS2 -- a marker, which IS counted, from the raw text
  [[nodiscard]] std::uint8_t Mas2(const Bubble& _bubble, std::uint8_t _slot, std::uint8_t _a) noexcept;
  void Spawn(MathWorkspace& _math, std::uint8_t _x, bool _carryIn) noexcept;
  void Launch(std::uint8_t& _docked, std::uint8_t _y) noexcept;
}
"""

SAMPLE_SOURCE = """
#include "pch.h"
namespace Elite
{
  void F(ShipBlock& _work, Bubble& _bubble, std::uint8_t slot)
  {
    _work[31] = static_cast<std::uint8_t>(_work[31] | 0x20u);   /* work[3] in a comment */ // 6502: MV1
    _bubble.blocks[slot][36] = _work[SHIP_FLAGS_OFFSET];
    const std::uint8_t z = _work[8u];
  }
}
"""

SAMPLE_MAIN = "#include \"pch.h\"\nint main() { Elite::Game game; Elite::Canvas canvas; return Elite::Run(); }\n"

SAMPLE_MUTANTS = {"units": [{"name": "u", "mutants": [{"id": "a"}, {"id": "b"}]}, {"name": "v", "mutants": [{"id": "c"}]}]}

SAMPLE_LEDGER = "| `ZeroPage.h` | `Present.h` | `Missing.cpp` |\n"

SAMPLE_ORACLE_TEST = "#include \"OracleImage.h\"\nTEST_CLASS(A) { TEST_METHOD(B) { OracleImage::Instance(); } };\n"
SAMPLE_PLAIN_TEST = "// OracleImage only in a comment\nTEST_CLASS(C) { TEST_METHOD(D) { } };\n"
SAMPLE_ORIGIN_TOOL = "# Upstream in a comment does not count\nROOT = REPO / \"Upstream\" / \"elite-source-code-library\"\n"
SAMPLE_PLAIN_TOOL = "# MasterFile mentioned only here\nprint(1)\n"

EXPECTED = {
    "register-params": 3,
    "workspace-params": 1,
    "ship-literal-sites": 4,
    "ship-offset-sites": 1,
    "aggregate-refs": 3,
    "main-lines": 2,
    "outpost-elite-names": 3,
    "effects-seams": 2,
    "out-params": 1,
    "carry-params": 1,
    "mutants": 3,
    "inventory-stale-files": 2,
    "origin-markers": 2,
    "oracle-test-files": 1,
    "origin-tools": 1,
}


def self_test() -> list[str]:
    """Build a tiny tree with a known number of each pattern and check every counter reads it."""
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        (root / "GameLogic").mkdir()
        (root / "Outpost").mkdir()
        (root / "tools").mkdir()
        (root / "Design").mkdir()
        (root / "GameLogic" / "Sample.h").write_text(SAMPLE_HEADER, encoding="utf-8")
        (root / "GameLogic" / "Present.h").write_text("#pragma once\n", encoding="utf-8")
        (root / "GameLogic" / "Sample.cpp").write_text(SAMPLE_SOURCE, encoding="utf-8")
        (root / "Outpost" / "Main.cpp").write_text(SAMPLE_MAIN, encoding="utf-8")
        (root / "tools" / "mutants.json").write_text(json.dumps(SAMPLE_MUTANTS), encoding="utf-8")
        (root / "Design" / "Source-Inventory.md").write_text(SAMPLE_LEDGER, encoding="utf-8")
        (root / "Tests" / "GameLogicTests").mkdir(parents=True)
        (root / "Tests" / "GameLogicTests" / "ATests.cpp").write_text(SAMPLE_ORACLE_TEST, encoding="utf-8")
        (root / "Tests" / "GameLogicTests" / "BTests.cpp").write_text(SAMPLE_PLAIN_TEST, encoding="utf-8")
        (root / "tools" / "labels.py").write_text(SAMPLE_ORIGIN_TOOL, encoding="utf-8")
        (root / "tools" / "check_docs.py").write_text(SAMPLE_PLAIN_TOOL, encoding="utf-8")

        measured = counts(root)

    complaints: list[str] = []
    for name, expected in EXPECTED.items():
        actual = measured[name][0]
        if actual != expected:
            complaints.append(f"self-test: {name} read {actual} from the sample tree, expected {expected}")
    for name in measured:
        if name not in EXPECTED:
            complaints.append(f"self-test: {name} has a counter and no expectation")
    return complaints


# ---- main -------------------------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(description="The modernisation ratchet (Design/Modernize.md).")
    parser.add_argument("--list", action="store_true", help="print every count and stop")
    parser.add_argument("--update", action="store_true", help="rewrite the ceilings to the tree as it is")
    parser.add_argument("--self-test", action="store_true", help="prove the counters on a synthetic tree, and stop")
    arguments = parser.parse_args()

    failures = self_test()
    if failures:
        for line in failures:
            print(line)
        print(f"FAIL  self-test: {len(failures)} counter(s) do not read the sample tree")
        return 1
    if arguments.self_test:
        print(f"OK    self-test passed: {len(EXPECTED)} counters read the sample tree")
        return 0

    measured = counts()

    if arguments.list:
        width = max(len(name) for name in measured)
        for name, (value, meaning) in measured.items():
            print(f"{name:{width}s} {value:7,d}   {meaning}")
        return 0

    if arguments.update:
        write_ratchet(measured)
        print(f"OK    {RATCHET.relative_to(REPO).as_posix()} rewritten to the tree: {len(measured)} ceilings")
        return 0

    complaints = check_ratchet(measured)
    if complaints:
        for line in complaints:
            print(line)
        print(f"FAIL  {len(complaints)} pattern count(s) disagree with {RATCHET.name}")
        return 1

    print(f"OK    {len(measured)} pattern counts at their ceilings ({RATCHET.name})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
