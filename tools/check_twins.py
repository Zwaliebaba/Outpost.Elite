#!/usr/bin/env python3
"""Check that every faithful drawing routine that has a 640x400 twin still calls it.

WHY THIS EXISTS (Design/Resolution.md sections 4 and 8.6). The resolution track draws the picture a
person sees on a SECOND surface, beside the canvas the oracle compares, from the same inputs at the
same call sites. Nothing in the compiler or the suite makes that pairing hold: a faithful routine can
grow a new canvas write, or lose the twin call beside an old one, and every oracle test stays green
because the canvas is still right. What goes wrong is the PICTURE, and only where a test happens to
look. Resolution.md R23 is that risk, and this is its cheap half -- the shadow tests are the dear one.

    python tools/check_twins.py
    python tools/check_twins.py --self-test

THREE RULES, and each is narrow on purpose.

1. EVERY `*2x` ROUTINE NAMES WHAT IT IS A TWIN OF. A `/// 2x of: Name` line on its declaration, and
   `Name` must be declared somewhere in GameLogic. That is not decoration: Modernize.md M6-c renames
   every identifier that is a 6502 label, and a twin named for a routine that has been renamed away
   is the first thing that would rot. It is also why a twin carries this line rather than a
   `// 6502:` marker -- a twin ports nothing, and claiming otherwise would put it in the ledger.

2. IN A TWINNED FILE, A FUNCTION THAT DRAWS ON THE CANVAS DRAWS ON THE PICTURE TOO. Either it calls
   a `*2x` routine itself, or `PAIRED_BY_CALLER` says its twin is the caller's to call -- and then
   rule 3 checks the callers. A file that is not in `TWINNED` is not yet twinned and is COUNTED
   rather than failed: the track lands region by region (Resolution.md section 10), and a check that
   went red from RS-1 to RS-4 would teach people to skip it.

3. A ROUTINE PAIRED BY ITS CALLER IS PAIRED BY EVERY CALLER. Every call to it anywhere in GameLogic
   is followed, inside the same function, by a call to the twin it names. This is the rule that
   earns the exemption in rule 2: without it, "the caller pairs them" is a promise nobody checks,
   which is how the Windows build broke on RS-0 -- a declaration and its uses drifting apart with
   every check green.

WHAT IT CANNOT SEE, said plainly. It reads text, not types. A twin called with the wrong arguments,
on the wrong cell, or in the wrong order still passes here; that is what the shadow tests of
section 8.1 are for, and they are the instrument that matters. This one catches the pairing being
absent, which is the failure a scripted edit produces and the one a reviewer skims past.
"""

from __future__ import annotations

import re
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOGIC = REPO / "GameLogic"

# The files whose canvas drawing has a twin, and the slice that gave it one. A slice adds its files
# here in the same commit as its twins; anything absent is reported as outstanding.
TWINNED: dict[str, str] = {
    "TextPrint.cpp": "RS-1",
    "TextPrint2x.cpp": "RS-1",
}

# Routines whose twin is the CALLER's to call, and the twin each one names. Rule 3 checks them.
PAIRED_BY_CALLER: dict[str, str] = {
    "ClearTextArea": "ClearTextArea2x",
}

# A function in a twinned file that draws on the canvas and needs no twin, with the reason. Kept as
# data rather than as a silence, so that a reader can see what has been decided and disagree.
NEEDS_NO_TWIN: dict[str, str] = {
    "ResetCellColours": "screen RAM palettes for the whole canvas, which the picture keeps per cell "
                        "and the glyph twin writes; there is no second surface to reset",
    "ClearMessageRows": "RS-5's, with the docked screens it serves -- its ten callers are spread "
                        "over six files and most have no picture in scope yet",
}

# What "draws on the canvas" is: a write through a `Canvas&`, or one of the pixel primitives.
DRAWS = re.compile(r"\.(?:Write|ExclusiveOr)\s*\(|\b(?:PlotPixel|PlotRelativePixel|PlotDash|PlotBlock|DrawLine|DrawHorizontalLine)\s*\(")
CALLS_TWIN = re.compile(r"\b[A-Za-z_]\w*2x\s*\(")
TWIN_MARKER = re.compile(r"///\s*2x of:\s*([A-Za-z_][\w:]*)")
DECLARED = re.compile(r"\b([A-Za-z_]\w*)\s*\(")


def strip_comments(_text: str) -> str:
    without_block = re.sub(r"/\*.*?\*/", " ", _text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", without_block)


def functions(_text: str) -> list[tuple[str, str]]:
    """Every function DEFINITION in one translation unit, as (name, body).

    A definition is a name, a parenthesised list and a `{` at the start of a line's indentation --
    which is what this codebase's formatting guarantees (Allman braces, AGENTS.md section 4). A
    declaration ends in `;` and is skipped by the same rule.
    """
    found: list[tuple[str, str]] = []
    for match in re.finditer(r"^[ \t]*(?:[\w:<>,*&\[\]]+[ \t]+)+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)?)\s*\([^;{]*\)"
                             r"(?:\s*(?:const|noexcept|override|final))*\s*\n?\s*\{", _text, re.M):
        start = _text.index("{", match.end() - 1)
        depth = 0
        for index in range(start, len(_text)):
            if _text[index] == "{":
                depth += 1
            elif _text[index] == "}":
                depth -= 1
                if depth == 0:
                    found.append((match.group(1).split("::")[-1], _text[start:index]))
                    break
    return found


def declared_names(_root: Path) -> set[str]:
    names: set[str] = set()
    for path in sorted(_root.glob("*.h")):
        names.update(DECLARED.findall(strip_comments(path.read_text(encoding="utf-8", errors="replace"))))
    return names


def check(_root: Path, _twinned: dict[str, str], _paired: dict[str, str]) -> tuple[int, int, list[str]]:
    """Returns (routines checked, files not yet twinned, failures)."""
    wrong: list[str] = []
    checked = 0

    # ---- rule 1: every twin names a routine that exists ----------------------------------------
    known = declared_names(_root)
    for path in sorted(_root.glob("*.h")):
        text = path.read_text(encoding="utf-8", errors="replace")
        marked = {name for name in TWIN_MARKER.findall(text)}
        for match in re.finditer(r"\b([A-Za-z_]\w*2x)\s*\(", strip_comments(text)):
            checked += 1
            twin = match.group(1)
            faithful = twin[:-2]
            if not marked:
                wrong.append(f"  FAIL  {path.name}: {twin} carries no '/// 2x of:' line saying what it twins")
            elif faithful not in known and not any(name.split("::")[-1] in known for name in marked):
                wrong.append(f"  FAIL  {path.name}: {twin} names a routine GameLogic does not declare")

    # ---- rule 2: a twinned file's drawing functions draw on both --------------------------------
    for path in sorted(_root.glob("*.cpp")):
        if path.name not in _twinned:
            continue
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for name, body in functions(text):
            if not DRAWS.search(body):
                continue
            checked += 1
            if CALLS_TWIN.search(body) or name in _paired or name in NEEDS_NO_TWIN or name.endswith("2x"):
                continue
            wrong.append(f"  FAIL  {path.name}: {name} draws on the canvas and calls no 2x twin, "
                         f"and is in neither PAIRED_BY_CALLER nor NEEDS_NO_TWIN")

    # ---- rule 3: a routine paired by its caller is paired by EVERY caller ------------------------
    for path in sorted(_root.glob("*.cpp")):
        text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for name, body in functions(text):
            for faithful, twin in _paired.items():
                if not re.search(r"\b" + faithful + r"\s*\(", body):
                    continue
                if name == faithful or name.endswith("2x"):
                    continue
                checked += 1
                if not re.search(r"\b" + twin + r"\s*\(", body):
                    wrong.append(f"  FAIL  {path.name}: {name} calls {faithful} and not {twin} beside it")

    outstanding = sum(1 for path in sorted(_root.glob("*.cpp")) if path.name not in _twinned
                      and DRAWS.search(strip_comments(path.read_text(encoding="utf-8", errors="replace"))))
    return checked, outstanding, wrong


def self_test() -> int:
    with tempfile.TemporaryDirectory() as folder:
        root = Path(folder)
        (root / "Planted.h").write_text("void PaintIt(Canvas& _c);\n"
                                        "/// 2x of: PaintIt\n"
                                        "void PaintIt2x(Picture& _p);\n"
                                        "/// 2x of: Wipe\n"
                                        "void Wipe2x(Picture& _p);\n"
                                        "void Wipe(Canvas& _c);\n", encoding="utf-8")

        # A drawing function with its twin beside it passes; the same one without it does not.
        good = "void PaintIt(Canvas& _c)\n{\n  _c.Write(0, 1);\n  PaintIt2x(picture);\n}\n"
        (root / "Planted.cpp").write_text(good, encoding="utf-8")
        _, _, clean = check(root, {"Planted.cpp": "test"}, {})
        if clean:
            print("FAIL  the self-test's paired function was reported")
            for line in clean:
                print(line)
            return 1

        (root / "Planted.cpp").write_text("void PaintIt(Canvas& _c)\n{\n  _c.Write(0, 1);\n}\n", encoding="utf-8")
        _, _, missing = check(root, {"Planted.cpp": "test"}, {})
        if not any("calls no 2x twin" in line for line in missing):
            print("FAIL  the self-test's unpaired drawing function was not reported")
            return 1

        # And a caller that drops the twin beside a routine paired by its callers.
        (root / "Planted.cpp").write_text("void Wipe(Canvas& _c)\n{\n  _c.Write(0, 0);\n  Wipe2x(p);\n}\n"
                                          "void User()\n{\n  Wipe(canvas);\n}\n", encoding="utf-8")
        _, _, dropped = check(root, {"Planted.cpp": "test"}, {"Wipe": "Wipe2x"})
        if not any("calls Wipe and not Wipe2x" in line for line in dropped):
            print("FAIL  the self-test's caller that dropped the twin was not reported")
            return 1

        # A twin naming a routine nothing declares, which is what an M6-c rename would leave.
        (root / "Planted.h").write_text("/// 2x of: Vanished\nvoid Vanished2x(Picture& _p);\n", encoding="utf-8")
        (root / "Planted.cpp").write_text("void Nothing()\n{\n}\n", encoding="utf-8")
        _, _, orphan = check(root, {}, {})
        if not any("names a routine GameLogic does not declare" in line for line in orphan):
            print("FAIL  the self-test's twin of a routine that no longer exists was not reported")
            return 1

    _, _, real = check(LOGIC, TWINNED, PAIRED_BY_CALLER)
    if real:
        print("FAIL  the tree itself does not pass")
        for line in real:
            print(line)
        return 1

    print("OK    self-test passed: a planted unpaired routine, a planted caller that dropped its "
          "twin and a planted twin of a routine that no longer exists were caught, and the tree is clean")
    return 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()

    checked, outstanding, wrong = check(LOGIC, TWINNED, PAIRED_BY_CALLER)

    print(f"twinned files    {len(TWINNED)}")
    print(f"pairings checked {checked}")
    print(f"not yet twinned  {outstanding}   (RS-2 to RS-4 -- Design/Resolution.md section 10)")

    if wrong:
        for line in wrong:
            print(line)
        print(f"FAIL  {len(wrong)} routine(s) draw on the canvas with no twin beside them")
        return 1

    print("OK    every twinned routine still draws on both surfaces, and every twin names what it twins")
    return 0


if __name__ == "__main__":
    sys.exit(main())
