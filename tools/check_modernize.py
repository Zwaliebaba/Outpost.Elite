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
LEDGER_FILE = re.compile(r"`([A-Za-z0-9_]+\.(?:h|cpp))`")
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


VIEW_TEMPLATE = re.compile(r"template\s*<[^>]*>\s*struct\s+\w+\s*\{[^}]*\}", re.DOTALL)


def count_aggregate_refs(_root: Path) -> int:
    """P5 -- reference members of the argument-list structs (`Canvas& canvas;`) in the headers.

    A templated struct of references is a VIEW over bytes (`AxisBytes<Byte>` and kin, M1-a), generic
    over constness precisely because it is one, and not an argument list; its bodies are cut before
    the count so that naming a byte does not read as a pattern coming back.
    """
    return len(AGGREGATE_REF.findall(VIEW_TEMPLATE.sub(" ", read_stripped(headers(_root)))))


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


def count_mutant_files(_root: Path) -> int:
    """The distinct files the recorded mutants edit -- what M6-0-g's floor is measured in."""
    recorded = json.loads((_root / "tools" / "mutants.json").read_text(encoding="utf-8"))
    return len({mutant["file"] for unit in recorded["units"] for mutant in unit["mutants"]})


def count_inventory_stale_files(_root: Path) -> int:
    """M5-c -- `.h`/`.cpp` names in a ledger row's HOME cell that name no file in any project folder.

    IT READS THE HOME CELL AND NOT THE WHOLE FILE, and M5-c narrowed it there rather than lowering a
    ceiling to meet the tree (Risk R18 is the other way round). A home is the row's live claim about
    where its labels live; the notes beside it are HISTORY, and the plan's rule for numbers already
    draws that line -- a journal number was true when it was written and is never touched. Two notes
    name a file precisely to say the tree does NOT have it (§6.129's raster row, and the workspace
    row M5-c rewrote), so a counter over the whole file would need a finding deleted to reach zero.
    `inventory.py --check-homes` applies the same rule and is the repository check behind it.
    """
    ledger = _root / "Design" / "Source-Inventory.md"
    on_disk: set[str] = set()
    for folder in ("GameLogic", "Outpost", "NeuronCore", "Tests/GameLogicTests", "Tests/PortableRunner/Shim"):
        directory = _root / folder
        if directory.is_dir():
            on_disk.update(path.name for path in directory.iterdir() if path.is_file())

    stale = 0
    for line in ledger.read_text(encoding="utf-8", errors="replace").split("\n"):
        if not line.startswith("|"):
            continue
        cells = line.split("|")
        if len(cells) < 5:
            continue
        stale += len([name for name in LEDGER_FILE.findall(cells[3]) if name not in on_disk])
    return stale


# ---- M6-d's instrument: the assembly transcribed in comments -------------------------------------
#
# The 6502's 56 mnemonics, split by addressing mode, because the two need different tests.
#
# A mnemonic ALONE is not a transcription. "`ORA` touches no flag" and "its top BIT is set" are
# prose about behaviour that happen to name an instruction, and R20 says a reason is what M6-d keeps.
# What M6-d removes is the QUOTATION: an instruction with its operand, or a run of implied-mode
# instructions separated by slashes. So the counter asks for instruction SHAPE, not for a word.
MNEMONICS = ("ADC AND ASL BCC BCS BEQ BIT BMI BNE BPL BRK BVC BVS CLC CLD CLI CLV CMP CPX CPY DEC DEX DEY "
             "EOR INC INX INY JMP JSR LDA LDX LDY LSR NOP ORA PHA PHP PLA PLP ROL ROR RTI RTS SBC SEC SED "
             "SEI STA STX STY TAX TAY TSX TXA TXS TYA").split()

# The ones that take no operand, so shape has to come from what surrounds them instead.
IMPLIED = sorted("BRK CLC CLD CLI CLV DEX DEY INX INY NOP PHA PHP PLA PLP RTI RTS SEC SED SEI TAX TAY TSX "
                 "TXA TXS TYA".split())

# `LDA #0`, `STA SC+1`, `JSR MULTU`, `ASL A` -- a mnemonic with a real OPERAND after it.
#
# THE OPERAND MAY NOT BE AN ENGLISH WORD, and that is the correction M6-d-2 made after the first
# file: "clears both halves AND the carry" and "expresses that with ROR through the carry flag" both
# read as an instruction to a looser pattern, and both are prose. A 6502 operand starts with `#`,
# `$`, `&`, `%` or `(`, or is the accumulator, or is a label -- and every label in this game's source
# begins with a capital, a digit or a dot. Lowercase after a mnemonic is a sentence carrying on.
OPCODE_OPERAND = re.compile(r"\b(?:" + "|".join(MNEMONICS) + r")\s+(?:A\b|[#$&%(]|[A-Z0-9.][\w.%+,]*)")

# `TXA / CLC` -- an implied-mode instruction needs a SLASH beside it to be a listing.
#
# Backticks are not enough: "the `CLC` here looks dead" and "no `CLC` between them" name an
# instruction in a sentence about behaviour, which is what R20 keeps and what §3 says is not a
# quotation. A slash is the thing only a transcription has.
OPCODE_IMPLIED = re.compile(r"(?:/\s*(?:" + "|".join(IMPLIED) + r")\b|\b(?:" + "|".join(IMPLIED) + r")\s*/)")

COMMENT_LINE = re.compile(r"^\s*(?://|\*|/\*)")

# The tag that makes a quotation DELIBERATE (§1 R-i, ruled 2026-09-08).
#
# M6-d's row wants the ratchet at zero and Risk R20 lets a comment keep its instruction sequence
# when that sequence IS the reason. Both hold once the counter can tell the two apart, and the only
# thing that can tell them apart is the author saying which this is. So a kept quotation carries the
# tag, ON EVERY LINE OF IT: an untagged listing is transcription and goes, a tagged one is counted
# against a cap you can see. Tagging each line rather than opening a block keeps the rule
# unambiguous and makes a long quotation cost more to keep, which is the right incentive.
#
# Not `6502:` -- that is the marker M6-e removes, and `\b6502:` does not match this.
QUOTED_TAG = re.compile(r"\b6502 quoted:")


def _opcode_lines(_root: Path):
    """Every comment line in the port that shows instruction shape, with whether it is tagged.

    Counted per LINE and not per instruction, because a rewrite replaces lines: a run of six
    instructions across two comment lines is two sites to rewrite, not six.

    `Design/` is not read. The plan's own journal quotes assembly deliberately and is history --
    M6-d's row says so -- and a counter that read it could never reach zero.
    """
    for folder in ("GameLogic", "Outpost"):
        here = _root / folder
        if not here.is_dir():
            continue
        for path in sorted(here.glob("*.h")) + sorted(here.glob("*.cpp")):
            for line in path.read_text(encoding="utf-8", errors="replace").split("\n"):
                if COMMENT_LINE.match(line) and (OPCODE_OPERAND.search(line) or OPCODE_IMPLIED.search(line)):
                    yield line, bool(QUOTED_TAG.search(line))


def count_opcode_transcriptions(_root: Path) -> int:
    """P12 -- instruction listings that carry no reason. M6-d drives this one to ZERO."""
    return sum(1 for _line, tagged in _opcode_lines(_root) if not tagged)


def count_opcode_quotations(_root: Path) -> int:
    """P12 -- instruction sequences kept BECAUSE they are the reason (R20, R-i). Capped, not zero."""
    return sum(1 for _line, tagged in _opcode_lines(_root) if tagged)


def count_origin_markers(_root: Path) -> int:
    """P12 -- `6502:` references in GameLogic/ -- the `//` markers inventory.py reads and the `*`-prefixed
    ones inside block comments alike -- read from the RAW text because they are comments."""
    total = 0
    for path in headers(_root) + sources(_root):
        total += len(ORIGIN_MARKER.findall(path.read_text(encoding="utf-8", errors="replace")))
    return total


# The identifiers that are 6502 labels and nothing else (P12, Design/Modernize.md M6-c).
#
# WRITTEN AS A LIST AND NOT DERIVED FROM `Upstream/`, for two reasons. The first is that it has to
# keep working after M6-f deletes the upstream tree, and a counter that reads the original cannot
# read zero once the original is gone. The second is that "is a label" is not the question: 135
# identifier spellings in `GameLogic/` are labels of the C64 build, and most of them are labels
# BECAUSE THE ORIGINAL ALSO NEEDED A WORD FOR THE THING -- `view`, `status`, `type`, `energy`,
# `name`, `counter`, `pixel`, `swap`, `sun`, `junk`, `cash`, `checksum`, `x1`, `y1`, `x2`, `y2` --
# and renaming those would make the code worse, not freer of the original. So the question the
# counter asks is the one M6-c is actually about: **does the name say what it holds, or do you have
# to have read the original to know?**
#
# What that leaves is five families, and the count was taken over the tree before any of them moved
# (§8, M6-c-0): 971 sites in `GameLogic/`, 7 in `Outpost/` and 1,611 in the suite.
ORIGIN_IDENTIFIERS = (
    # The zero-page scratch bytes. `Q` is a divisor here and a multiplicand there; `T` is whatever
    # the routine needed a byte for. One letter, no meaning, and four hundred sites of it.
    #
    # `c` AND `v` ARE NOT HERE AND THAT IS DELIBERATE. Both are 6502 labels, and in this tree both
    # are the PROCESSOR'S STATUS FLAGS: `cpu.c` in a hundred and fifty fixtures, and `Flags` in
    # `EliteTypes.h`, which is the status register as a struct and names its four bits as the
    # processor names them. That is the right name for the thing, so the counter does not ask for it
    # back. `PlanetDraw`'s own `v` is a carry-over and M6-c renames it by eye rather than by ratchet.
    "k k2 k3 k5 k6 q q2 r s s2 t t1 u b m p p2 "
    # `XX2` to `XX17` -- the drawing workspace, named by their offsets from `XX0`.
    "xx2 xx3 xx12 xx16 xx17 "
    # The rates and the counters. `ALPHA` and `BETA` are the roll and the pitch and say neither;
    # `ALP1`/`ALP2` and `BET1`/`BET2` are their magnitudes and signs, `DELTA` the speed, `DELT4` the
    # speed shifted for the stardust, `RAT`/`RAT2` the damping constants, `SC` the screen pointer.
    "sc cnt cnt2 rat rat2 alp1 alp2 bet1 bet2 alpha beta delta delt4 "
    # The named oddities: labels whose names are not words in any language. `frump`, `lotus` and
    # `santana` are the authors' jokes; `ze`, `stp`, `lsp` and `yx2M1` are abbreviations of nothing.
    # `SUNX` is NOT here: it is a label, and `sunX` says where the sun's centre is, which is the
    # test this list applies to everything else (M6-c-5).
    "lsp stp ze yx2M1 dontclip newb newzp mutok pltog patg frump fist sprx spry innersec lotus santana "
    # The music player's, which are the SID's registers under the player's own numbering.
    "value0 value1 value2 value3 value4 vibrato2 vibrato3 "
    "voice2lo1 voice2hi1 voice2lo2 voice2hi2 voice3lo1 voice3hi1 voice3lo2 voice3hi2"
).split()

# The prefixes the naming convention puts in front of a name (AGENTS.md §1): a parameter's `_`, a
# member's `m_`, a global's `g_`, a mutable static's `sm_`. `_q` is the same carry-over as `q`.
NAME_PREFIX = re.compile(r"^(?:sm_|m_|g_|_)")
IDENTIFIER = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")

def code_only(_text: str) -> str:
    """Comments, string literals and character literals out, in ONE pass over the text.

    `strip_comments` and a literal regex run one after the other are wrong in both orders and this
    counter met both halves of it: a character literal holds `Put('m')`, which is not a routine still
    called `m`, and a comment holds an apostrophe -- "sprite 1's low nibble" -- which opens a
    character literal in whatever the other pass left behind and swallows code up to the next one.
    That is how `lotus` came to be counted once in a file that does not use it. A single scan cannot
    get the order wrong because there is no order: whichever opens first closes first.

    `strip_comments` above is left alone deliberately -- eight other counters are calibrated against
    it and this is not the slice that re-measures them.
    """
    out: list[str] = []
    at = 0
    end = len(_text)
    while at < end:
        here = _text[at]
        pair = _text[at : at + 2]
        if pair == "//":
            at = _text.find("\n", at)
            if at < 0:
                break
        elif pair == "/*":
            closed = _text.find("*/", at + 2)
            at = end if closed < 0 else closed + 2
        elif here == "'" and at > 0 and _text[at - 1].isdigit() and at + 1 < end and _text[at + 1].isdigit():
            # A DIGIT SEPARATOR, not a character literal: `4'000'000` is all over the suite, and
            # reading its apostrophe as a quote swallows every identifier up to the next one. This
            # is what made the two-pass strip read 99 sites FEWER than there are.
            out.append(here)
            at += 1
        elif here in "\"'":
            at += 1
            while at < end and _text[at] != here:
                at += 2 if _text[at] == "\\" else 1
            at += 1
        else:
            out.append(here)
            at += 1
    return "".join(out)



def count_origin_identifiers(_root: Path) -> int:
    """P12 -- sites where the port still calls something by its 6502 label (M6-c).

    THE SUITE IS COUNTED TOO, and it is two thirds of the total. M6-c's scope is "in the code and
    the tests", and a counter that stopped at `GameLogic/` would read zero with 1,611 sites left.
    """
    vocabulary = set(ORIGIN_IDENTIFIERS)
    total = 0
    for folder in ("GameLogic", "Outpost", "Tests/GameLogicTests"):
        here = _root / folder
        if not here.is_dir():
            continue
        for path in sorted(here.glob("*.h")) + sorted(here.glob("*.cpp")):
            # The interpreter is not the port. `Cpu6502` MODELS a 6502, so `c`, `v` and `p` are the
            # processor's own flags and pointers and are the right names for it -- 120 of the suite's
            # sites are `cpu.c` -- and the whole file leaves the tree at M6-f anyway.
            if path.stem == "Cpu6502":
                continue
            code = code_only(path.read_text(encoding="utf-8", errors="replace"))
            for match in IDENTIFIER.finditer(code):
                if NAME_PREFIX.sub("", match.group(0)) in vocabulary:
                    total += 1
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
    "mutant-files": (count_mutant_files, "distinct files those mutants edit"),
    "inventory-stale-files": (count_inventory_stale_files, "file names Source-Inventory.md cites that are not on disk"),
    "origin-markers": (count_origin_markers, "P12: 6502: references in GameLogic/ comments"),
    "opcode-transcriptions": (count_opcode_transcriptions, "P12: instruction listings in comments that carry no reason"),
    "opcode-quotations": (count_opcode_quotations, "P12: instruction sequences kept because they ARE the reason"),
    "origin-identifiers": (count_origin_identifiers, "P12: identifiers that are 6502 labels, in the library, the app and the suite"),
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
  template <class Byte> struct AxisBytes
  {
    Byte& lo;
    Byte& hi;
  };
  // 6502: LDA #0 / STA SC+1 -- an instruction with an operand, so this line IS a transcription
  // `ORA` touches no flag, and its top BIT is set: prose that NAMES an instruction is not one
  // it clears both halves AND the carry, and expresses that with ROR through the flag: also prose
  /// 6502: TXA / CLC -- implied-mode instructions in a quoted run count too
  // 6502 quoted: LDA #1 / STA T -- tagged, so this one is a QUOTATION and not a transcription
  // std::uint8_t _a in a comment does not count, and neither does bool _carryIn here
  // k3 and q here are a COMMENT and are not counted either
  std::uint8_t m_alp2 = 0;                                  // a member's prefix is stripped before the match
  void Divide(std::uint8_t _q, std::uint8_t _k3) noexcept;  // a parameter's is too
  std::uint8_t x1 = 0;                                      // a label the port would have chosen anyway: NOT counted
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
  void F(Ship& _work, Bubble& _bubble, std::uint8_t slot)
  {
    _work[31] = static_cast<std::uint8_t>(_work[31] | 0x20u);   /* work[3] in a comment */ // 6502: MV1
    _bubble.blocks[slot][36] = _work[SHIP_FLAGS_OFFSET];
    const std::uint8_t z = _work[8u];
    const std::uint8_t xx12 = z; /* xx16 in a comment does not count */
    Put('m');                    // and 'm' is a character literal, not the label
  }
}
"""

SAMPLE_MAIN = "#include \"pch.h\"\nint main() { Elite::Game game; Elite::Canvas canvas; return Elite::Run(); }\n"

SAMPLE_MUTANTS = {
    "units": [
        {"name": "u", "mutants": [{"id": "a", "file": "GameLogic/Sample.cpp"}, {"id": "b", "file": "GameLogic/Sample.cpp"}]},
        {"name": "v", "mutants": [{"id": "c", "file": "GameLogic/Other.cpp"}]},
    ]
}

SAMPLE_LEDGER = (
    "| Labels | N | Home | Disposition | Notes |\n"
    "|---|---|---|---|---|\n"
    "| `alpha` | 1 | `Present.h`, `Missing.cpp` | Port | it was in `Gone.cpp` once, which is HISTORY |\n"
)

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
    "mutant-files": 2,
    "inventory-stale-files": 1,
    "origin-markers": 4,
    "origin-identifiers": 5,
    "opcode-transcriptions": 2,
    "opcode-quotations": 1,
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
