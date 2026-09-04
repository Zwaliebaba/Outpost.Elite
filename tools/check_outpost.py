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

WHAT IT DOES NOT CATCH: a signature change. `DockAtStation` gaining a parameter leaves the name in
place and breaks the call anyway, and no amount of grepping finds that. Compiling the app is the
only thing that would, and that needs a Windows machine.
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

    print(f"app sources      {len(sources)}")
    print(f"Elite:: names    {len(used)}")

    if missing:
        for name, where in missing.items():
            print(f"  FAIL  Elite::{name} is used by {', '.join(sorted(set(where)))} "
                  f"and is not declared in GameLogic/*.h")
        print(f"FAIL  {len(missing)} name(s) the app uses no longer exist")
        return 1

    print("OK    every Elite:: name the app uses is declared in GameLogic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
