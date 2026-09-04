#!/usr/bin/env python3
"""Check that every `Elite::` name the Windows app uses still exists in GameLogic.

The portable runner builds `GameLogic` and the test suite and nothing else, because `Outpost/` is
Win32 and DirectX 12 and will not compile on a hosted Linux runner. That leaves a hole the whole
width of the app: renaming a type in `GameLogic` breaks `Outpost/Main.cpp` and every local check
still passes, so the first sign of it is a red Windows job several minutes after the push. That is
what happened when `DockedShip` became `FlightStatus`.

This closes the half of the hole that can be closed cheaply. It reads every `Elite::Name` the app
mentions and asserts that `Name` is DECLARED somewhere in `GameLogic/*.h` -- declared, not merely
mentioned, so a comment that says "this used to be called DockedShip" does not satisfy it.

    python tools/check_outpost.py

It also checks HOW MANY ARGUMENTS each call passes, against how many the declaration takes. That
half exists because the name check alone was not enough: `ClearMessageRows` gaining a fifth
parameter left the name in place and broke `Outpost/Shell.cpp`, and the Windows job found it
several minutes after the push -- the second break of the same afternoon through the same hole.

    python tools/check_outpost.py

WHAT IT STILL DOES NOT CATCH: a change of parameter TYPES that keeps the arity, and anything
reached through a member call rather than a qualified `Elite::` one. Compiling the app is the only
thing that would, and that needs a Windows machine. The arity check is the part that is worth
having without one, because it is what a slice like 3d-b or 3d-c actually changes.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
APP = REPO / "Outpost"
LOGIC = REPO / "GameLogic"

QUALIFIED = re.compile(r"\bElite::([A-Za-z_][A-Za-z0-9_]*)")

# `Elite::Testing` is the test namespace and nothing in the app should reach it; anything nested
# below a name this finds is checked by its own first segment.
IGNORED = {"Testing"}


def strip_comments(_text: str) -> str:
    """Comments mention old names on purpose -- a rename note is not a declaration."""
    without_blocks = re.sub(r"/\*.*?\*/", " ", _text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", without_blocks)


def declared_names() -> set[str]:
    names: set[str] = set()
    for header in sorted(LOGIC.glob("*.h")):
        text = strip_comments(header.read_text(encoding="utf-8", errors="replace"))
        names.update(re.findall(r"\b(?:struct|class|enum(?:\s+class)?|using|namespace)\s+([A-Za-z_]\w*)", text))
        names.update(re.findall(r"\bconstexpr\s+(?:[\w:<>,\s*&]+?)\s+([A-Za-z_]\w*)\s*(?:=|\()", text))
        names.update(re.findall(r"\bextern\s+(?:const\s+)?[\w:<>,\s*&]+?\s+([A-Za-z_]\w*)\s*;", text))
        # Free functions and methods: a name immediately before an opening parenthesis.
        names.update(re.findall(r"\b([A-Za-z_]\w*)\s*\(", text))
        # Enumerators and plain members, which a `Field::Fuel` style reference reaches.
        names.update(re.findall(r"^\s*([A-Za-z_]\w*)\s*(?:=|,|;)", text, flags=re.MULTILINE))
    return names


SKIP_ARITY = {
    # Types constructed rather than called, and names whose declarations this cannot parse. A name
    # here is still checked for EXISTENCE; only its argument count is let through.
    "Canvas", "DrawWorkspace", "MathWorkspace", "TextState", "ExtendedTextState", "MessageState",
    "FlightStatus", "FlightState", "CommanderBlock", "Rng", "TokenPrinter", "CharacterPrinter",
    "ExtendedTokenPrinter", "TextPrinter", "StateTokens", "SystemSeeds", "CurrentSystem",
    "MarketState", "Bubble", "ShipBlock", "LineHeap", "Stardust", "PlanetSunState", "Compass",
    "LaserBurst", "GeometryWorkspace", "Projection", "GalaxyNumber",
}


def split_arguments(_text: str) -> int:
    """How many arguments a call passes, counting commas at nesting depth zero.

    `<` and `>` nest, because a declaration's `std::span<T, N>` hides a comma that is not an
    argument separator -- but `->` is removed first, or `game->commander` closes a bracket that
    was never opened and every comma after it is counted at the wrong depth. That was the first
    version's bug and it produced two false positives, which is the failure mode a check like this
    can least afford.
    """
    text = _text.replace("->", ".")
    depth = 0
    count = 1
    for character in text:
        if character in "([{<":
            depth += 1
        elif character in ")]}>":
            depth = max(0, depth - 1)
        elif character == "," and depth == 0:
            count += 1
    return 0 if not _text.strip() else count


def balanced(_text: str, _start: int) -> str | None:
    """The text between the parenthesis at _start and its match, or None if it does not close."""
    depth = 0
    for index in range(_start, len(_text)):
        if _text[index] == "(":
            depth += 1
        elif _text[index] == ")":
            depth -= 1
            if depth == 0:
                return _text[_start + 1:index]
    return None


def declared_arities() -> dict[str, set[int]]:
    """Every arity each GameLogic free function will accept, defaults included."""
    arities: dict[str, set[int]] = {}
    for header in sorted(LOGIC.glob("*.h")):
        text = strip_comments(header.read_text(encoding="utf-8", errors="replace"))
        for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", text):
            name = match.group(1)
            if name in ("if", "for", "while", "switch", "return", "sizeof", "static_cast"):
                continue
            inside = balanced(text, match.end() - 1)
            if inside is None:
                continue
            total = split_arguments(inside)
            optional = inside.count("=")
            for count in range(max(0, total - optional), total + 1):
                arities.setdefault(name, set()).add(count)
    return arities


def main() -> int:
    if not APP.is_dir():
        sys.exit(f"error: {APP} not found")

    declared = declared_names()
    used: dict[str, list[str]] = {}

    sources = sorted(list(APP.glob("*.cpp")) + list(APP.glob("*.h")))
    for source in sources:
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))
        for name in QUALIFIED.findall(text):
            if name in IGNORED:
                continue
            used.setdefault(name, []).append(source.name)

    missing = {name: where for name, where in sorted(used.items()) if name not in declared}

    # ---- and the arity of every call this can read ------------------------------------------
    arities = declared_arities()
    wrong: list[str] = []
    checked = 0

    for source in sources:
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))
        for match in re.finditer(r"\bElite::([A-Za-z_]\w*)\s*\(", text):
            name = match.group(1)
            if name in SKIP_ARITY or name not in arities:
                continue
            inside = balanced(text, match.end() - 1)
            if inside is None:
                continue
            passed = split_arguments(inside)
            checked += 1
            if passed not in arities[name]:
                accepted = ", ".join(str(count) for count in sorted(arities[name]))
                wrong.append(f"  FAIL  {source.name} calls Elite::{name} with {passed} argument(s); "
                             f"GameLogic declares it taking {accepted}")

    print(f"app sources      {len(sources)}")
    print(f"Elite:: names    {len(used)}")
    print(f"calls checked    {checked}")

    for line in wrong:
        print(line)

    if wrong and not missing:
        print(f"FAIL  {len(wrong)} call(s) pass the wrong number of arguments")
        return 1

    if missing:
        for name, where in missing.items():
            print(f"  FAIL  Elite::{name} is used by {', '.join(sorted(set(where)))} "
                  f"and is not declared in GameLogic/*.h")
        print(f"FAIL  {len(missing)} name(s) the app uses no longer exist")
        return 1

    if wrong:
        print(f"FAIL  {len(wrong)} call(s) pass the wrong number of arguments")
        return 1

    print("OK    every Elite:: name the app uses is declared, and every call it makes has the")
    print("      right number of arguments")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
