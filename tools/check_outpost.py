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

And it checks the MEMBERS the app names on a variable whose type it can read. That half exists
because M3 moves every byte of game state into one `Elite::Universe` and rewrites the app around
it, which is a rename of a hundred member names in files no Linux runner compiles -- the hole the
two checks above leave open at exactly the width of the slice about to go through it.

    python tools/check_outpost.py --self-test

It reads `struct X { ... }` and `class X { ... }` out of `GameLogic/*.h` with their base classes,
closes each type's member set over its bases, then finds every `Elite::Type name` the app declares
and checks each `name.member` against that set. An identifier it cannot resolve is skipped, a type
it cannot parse is skipped, and an identifier that two declarations disagree about takes the UNION
of their members -- three conservative choices, because a false positive in a check that gates the
build is worse than a miss.

WHAT IT STILL DOES NOT CATCH: a change of parameter TYPES that keeps the arity, a member reached
through an expression rather than a named variable (`_game.flight.Loop().options`), and anything
about templates. Compiling the app is the only thing that would, and that needs a Windows machine.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
APP = REPO / "Outpost"
LOGIC = REPO / "GameLogic"

QUALIFIED = re.compile(r"\bElite::([A-Za-z_][A-Za-z0-9_]*)")

# ---- the member check's three patterns ---------------------------------------------------------
#
# A member declaration inside a struct body: a type, then a name, then `;`, `=`, `{` or `[`. The
# leading keywords are the ones this codebase uses; `const` is among them because a `const Type&`
# member declares a name like any other.
MEMBER_FIELD = re.compile(r"^\s*(?:mutable\s+|static\s+|inline\s+|constexpr\s+|const\s+)*"
                          r"[A-Za-z_][\w:]*(?:\s*<[^;{}]*>)?(?:\s*[*&]+)?\s+"
                          r"([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=|\{|;)", re.MULTILINE)

# A method or constructor: a name immediately before an opening parenthesis, anywhere in the body.
MEMBER_METHOD = re.compile(r"\b([A-Za-z_]\w*)\s*\(")

# An enumerator, which `Mode::Docked` reaches -- one per line, ending in a comma or an initialiser.
MEMBER_ENUM = re.compile(r"^\s*([A-Za-z_]\w*)\s*(?:=[^,;]*)?,\s*$", re.MULTILINE)

# `Elite::Type name`, with an optional template argument list, an optional `*` or `&`, and the
# punctuation that ends a declaration. `Elite::Ship work{}` and `Elite::Canvas& _canvas)` both count.
APP_DECLARATION = re.compile(r"\bElite::([A-Za-z_]\w*)\s*(?:<[^;{}()]*>)?\s*[*&]?\s*&?\s*"
                             r"([A-Za-z_]\w*)\s*(?=[;={,)]|\{)")

APP_ACCESS = re.compile(r"\b([A-Za-z_]\w*)\s*(?:\.|->)\s*([A-Za-z_]\w*)")

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
    "FlightStatus", "FlightState", "Commander", "Rng", "TokenPrinter", "CharacterPrinter",
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


def type_bodies(_text: str):
    """Yield (name, bases, body) for every struct or class with a brace body."""
    for match in re.finditer(r"\b(?:struct|class)\s+([A-Za-z_]\w*)\s*(?:final\s*)?(?::([^{;]*))?\{", _text):
        depth = 0
        start = match.end() - 1
        for index in range(start, len(_text)):
            if _text[index] == "{":
                depth += 1
            elif _text[index] == "}":
                depth -= 1
                if depth == 0:
                    yield match.group(1), (match.group(2) or ""), _text[start + 1:index]
                    break


def declared_members(_logic: Path) -> dict[str, set[str]]:
    """Every member name each GameLogic type has, its bases' included.

    A type declared twice -- a forward declaration and a definition -- unions rather than replaces,
    so a partial parse can only ever ADD members and never take one away. That is the direction a
    check like this has to fail in.
    """
    members: dict[str, set[str]] = {}
    bases: dict[str, set[str]] = {}

    for header in sorted(_logic.glob("*.h")):
        text = strip_comments(header.read_text(encoding="utf-8", errors="replace"))
        for name, inherits, body in type_bodies(text):
            found = set(MEMBER_FIELD.findall(body))
            found |= set(MEMBER_METHOD.findall(body))
            found |= set(MEMBER_ENUM.findall(body))
            members.setdefault(name, set()).update(found)
            bases.setdefault(name, set()).update(
                re.findall(r"(?:public|private|protected)?\s*(?:Elite::)?([A-Za-z_]\w*)", inherits))

    # Close over the bases. Four passes is deeper than this codebase goes and terminates regardless.
    for _ in range(4):
        for name, inherited in bases.items():
            for base in inherited:
                if base in members:
                    members[name] |= members[base]

    return members


def check_members(_sources: list[Path], _members: dict[str, set[str]]) -> tuple[int, list[str]]:
    """Every `name.member` on an `Elite::Type name` the app declares, against that type's members."""
    checked = 0
    wrong: list[str] = []

    for source in _sources:
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))

        # One scope per file, and an identifier two declarations disagree about takes the union.
        # Crude, and crude in the safe direction: it can only let a bad access through.
        identifiers: dict[str, set[str]] = {}
        for match in APP_DECLARATION.finditer(text):
            if match.group(1) in _members:
                identifiers.setdefault(match.group(2), set()).add(match.group(1))

        for match in APP_ACCESS.finditer(text):
            name, member = match.group(1), match.group(2)
            if name not in identifiers:
                continue
            allowed: set[str] = set()
            for owner in identifiers[name]:
                allowed |= _members[owner]
            checked += 1
            if member not in allowed:
                owners = "/".join(sorted(identifiers[name]))
                wrong.append(f"  FAIL  {source.name} reads {name}.{member}, and Elite::{owners} has no such member")

    return checked, wrong


def self_test() -> int:
    """Plant an access that cannot resolve and check the member check says so.

    Without this the member check is a function nobody has watched fail, which is the state the
    arity check was in when it was written and the reason it went a slice before anyone trusted it.
    """
    members = declared_members(LOGIC)
    if "Commander" not in members:
        print("FAIL  the member map did not parse Elite::Commander")
        return 1

    import tempfile

    with tempfile.TemporaryDirectory() as folder:
        planted = Path(folder) / "Planted.cpp"
        planted.write_text("void f(Elite::Commander& commander) { commander.thisIsNotAMember = 1; }\n", encoding="utf-8")
        checked, wrong = check_members([planted], members)

    if checked == 0:
        print("FAIL  the self-test's access was not resolved at all")
        return 1
    if not wrong:
        print("FAIL  the self-test's bad member was not reported")
        return 1

    real = check_members(sorted(list(APP.glob("*.cpp")) + list(APP.glob("*.h"))), members)[1]
    if real:
        print("FAIL  the tree itself does not pass the member check")
        for line in real:
            print(line)
        return 1

    print(f"OK    self-test passed: a planted member was caught, {len(members)} types parsed, the tree is clean")
    return 0


def main() -> int:
    if "--self-test" in sys.argv:
        return self_test()

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

    # ---- and every member it names on a variable whose type this can read --------------------
    members = declared_members(LOGIC)
    membersChecked, badMembers = check_members(sources, members)
    wrong.extend(badMembers)

    print(f"app sources      {len(sources)}")
    print(f"Elite:: names    {len(used)}")
    print(f"calls checked    {checked}")
    print(f"members checked  {membersChecked}")

    for line in wrong:
        print(line)

    if wrong and not missing:
        print(f"FAIL  {len(wrong)} call(s) or member(s) the app names do not match GameLogic")
        return 1

    if missing:
        for name, where in missing.items():
            print(f"  FAIL  Elite::{name} is used by {', '.join(sorted(set(where)))} "
                  f"and is not declared in GameLogic/*.h")
        print(f"FAIL  {len(missing)} name(s) the app uses no longer exist")
        return 1

    if wrong:
        print(f"FAIL  {len(wrong)} call(s) or member(s) the app names do not match GameLogic")
        return 1

    print("OK    every Elite:: name the app uses is declared, every call it makes has the right")
    print("      number of arguments, and every member it names exists")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
