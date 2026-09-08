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

And it checks that every bare `m_member` and `_parameter` the app mentions is one it DECLARES.
That half exists because a scripted rename can rewrite the uses before the declarations and leave
neither: Resolution.md RS-0 turned `_screen` into `_picture` in the bodies and not in the
signatures, and five C2065s reached the Windows job through a tree all sixteen checks called clean.

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

# The same, for the app's OWN types, which carry no namespace where the app names them. `Outpost::`
# is optional because `Main.cpp` writes `GameShell shell;` and `Shell.h` writes `Outpost::Window&`.
OWN_DECLARATION = re.compile(r"\b(?:Outpost::)?([A-Z][A-Za-z_]\w*)\s*(?:<[^;{}()]*>)?\s*[*&]?\s*&?\s*"
                             r"([a-z_]\w*)\s*(?=[;={,)]|\{)")

# ANY `Type name`, whatever the type is. It exists so the app-type pass can tell an identifier it
# knows from one it only THINKS it knows: `ScreenPresenter.cpp` declares two different `view`s, an
# `Outpost::Viewport` and a `D3D12_SHADER_RESOURCE_VIEW_DESC`, and a check with one scope per file
# would otherwise read the Direct3D one's members against the Viewport's.
ANY_DECLARATION = re.compile(r"\b([A-Za-z_][\w:]*)\s*(?:<[^;{}()]*>)?\s*[*&]?\s*&?\s*"
                             r"([a-z_]\w*)\s*(?=[;={,)]|\{)")

# `Elite::Testing` is the test namespace and nothing in the app should reach it; anything nested
# below a name this finds is checked by its own first segment.
IGNORED = {"Testing"}

# What `ANY_DECLARATION` matches that is not a type: the words that can stand where one does.
KEYWORDS = {"return", "const", "static", "constexpr", "inline", "if", "for", "while", "else",
            "case", "auto", "using", "typedef", "struct", "class", "enum", "namespace", "template",
            "public", "private", "protected", "virtual", "override", "noexcept", "explicit", "new",
            "delete", "sizeof", "co_return", "switch", "do", "goto", "friend", "mutable", "operator"}


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


def declared_members(_logic: Path, _globs: tuple[str, ...] = ("*.h",)) -> dict[str, set[str]]:
    """Every member name each type in a directory has, its bases' included.

    A type declared twice -- a forward declaration and a definition -- unions rather than replaces,
    so a partial parse can only ever ADD members and never take one away. That is the direction a
    check like this has to fail in.

    `_globs` is headers alone for the two member passes, because a variable is declared with a type
    a header names. The CHAIN check asks for `*.cpp` too, and `Main.cpp`'s composition struct is
    why: `Game` is declared in an anonymous namespace in the file that walks it (section 8).
    """
    members: dict[str, set[str]] = {}
    bases: dict[str, set[str]] = {}

    sources: list[Path] = []
    for glob in _globs:
        sources += list(_logic.glob(glob))

    for header in sorted(sources):
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


# `Type name;` inside a class body: what a member's own type is, for the chain check below. It is
# the same shape as `MEMBER_FIELD` and keeps the type as well as the name.
MEMBER_TYPED = re.compile(r"^\s*(?:mutable\s+|static\s+|inline\s+|constexpr\s+|const\s+)*"
                          r"([A-Za-z_][\w:]*)\s*(?:<[^;{}()\n]*>)?\s*[*&]?\s*&?\s+"
                          r"([A-Za-z_]\w*)\s*[;={\[]", re.MULTILINE)

# `a.b.c` and longer -- a chain of at least two hops, which `APP_ACCESS` reads as two separate
# pairs and therefore resolves neither of.
CHAINED_ACCESS = re.compile(r"\b([A-Za-z_]\w*)((?:\s*(?:\.|->)\s*[A-Za-z_]\w*){2,})")


def declared_member_types(_roots: list[Path]) -> dict[str, dict[str, str]]:
    """For each type, what type each of its members is -- `Game.shell` is a `GameShell`.

    The chain check needs this and the flat member map cannot supply it. Namespaces, references and
    template arguments are stripped: what a chain needs is the type's NAME, and `Elite::Universe&`
    and `Universe` are the same name for that purpose.
    """
    types: dict[str, dict[str, str]] = {}
    for root in _roots:
        for source in sorted(list(root.glob("*.h")) + list(root.glob("*.cpp"))):
            text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))
            for name, _inherits, body in type_bodies(text):
                for spelt, member in MEMBER_TYPED.findall(body):
                    bare = spelt.rsplit("::", 1)[-1]
                    if bare in KEYWORDS:
                        continue
                    types.setdefault(name, {}).setdefault(member, bare)
    return types


def check_chains(_sources: list[Path], _members: dict[str, set[str]],
                 _memberTypes: dict[str, dict[str, str]]) -> tuple[int, list[str]]:
    """`a.b.c` -- the access through an expression the five halves above all decline to read.

    `Main.cpp` reached `_game.shell.FlushKeyboard()` for one commit after M3-b-3d renamed that
    method to `Keyboard::Flush`, and every check here passed it: `APP_ACCESS` sees `_game.shell`
    and `shell.FlushKeyboard` as two unrelated pairs, and `shell` is not a variable this file
    declares, so neither was resolved. That is the sixth Windows-only break and the second one a
    rename in `GameLogic` caused (plan section 8).

    So this walks the chain. `_game` is declared `Game& _game`, `Game::shell` is a `GameShell`,
    and `GameShell` is a type whose members are known -- three lookups, each of which may fail, and
    a failure means the chain is not checked rather than that it is wrong. `Main.cpp`'s own
    composition struct is why the type map reads `*.cpp` as well as `*.h`: `Game` is declared in an
    anonymous namespace in the file that uses it.

    A HOP THROUGH A CALL ENDS THE CHAIN. `_game.flight.Loop().options` names a return type this
    cannot read, so the walk stops at the parenthesis and checks what it got to, which for that
    example is `flight` on `Game` and nothing after it.
    """
    checked = 0
    wrong: list[str] = []

    for source in _sources:
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))

        scope: dict[str, set[str]] = {}
        for pattern in (APP_DECLARATION, OWN_DECLARATION):
            for match in pattern.finditer(text):
                scope.setdefault(match.group(2), set()).add(match.group(1))

        # The same declining `check_members` does, for the same file: `ScreenPresenter.cpp` has an
        # `Outpost::Viewport view` and a `D3D12_SHADER_RESOURCE_VIEW_DESC view`, and one scope per
        # file cannot tell which `view.something` is which.
        for match in ANY_DECLARATION.finditer(text):
            spelt = match.group(1).rsplit("::", 1)[-1]
            if spelt not in _members and spelt not in KEYWORDS:
                scope.pop(match.group(2), None)

        for match in CHAINED_ACCESS.finditer(text):
            root = match.group(1)
            if root not in scope:
                continue
            hops = re.findall(r"[A-Za-z_]\w*", match.group(2))

            # A name two declarations disagree about is checked against neither: the chain needs one
            # type to walk from, and the union of two is not a type.
            owners = scope[root]
            if len(owners) != 1:
                continue
            owner = next(iter(owners))

            for index, hop in enumerate(hops):
                known = _members.get(owner)
                if known is None:
                    break
                checked += 1
                if hop not in known:
                    reached = root + "." + ".".join(hops[:index + 1])
                    wrong.append(f"  FAIL  {source.name} reads {reached}, and {owner} has no member {hop}")
                    break
                nextType = _memberTypes.get(owner, {}).get(hop)
                if nextType is None:
                    break
                owner = nextType

    return checked, wrong


def check_members(_sources: list[Path], _members: dict[str, set[str]], _pattern=APP_DECLARATION,
                  _prefix: str = "Elite::") -> tuple[int, list[str]]:
    """Every `name.member` on a `Type name` the app declares, against that type's members.

    IT RUNS TWICE, over two namespaces, and the second pass is the one a Windows build asked for.
    The first checks what the app names on an `Elite::` type -- a member `GameLogic` removed and the
    app still reads. The second checks what it names on its OWN types, which the first cannot see
    because `Outpost::FlightSession` carries no `Elite::`: `Main.cpp` read `flight.Video()` for a
    commit after slice M3-b-3a's scripted deletion took the accessor with the seams around it, and
    `check_outpost.py` passed every one of its five halves (plan section 8).

    The app-type pass declines to guess in one place the `Elite::` pass need not: a type the parse
    did not find has no member set, so nothing on a variable of it is checked. That is the same
    direction as everything else here -- it can only let a bad access through.
    """
    checked = 0
    wrong: list[str] = []

    for source in _sources:
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))

        # One scope per file, and an identifier two declarations disagree about takes the union.
        # Crude, and crude in the safe direction: it can only let a bad access through.
        identifiers: dict[str, set[str]] = {}
        for match in _pattern.finditer(text):
            if match.group(1) in _members:
                identifiers.setdefault(match.group(2), set()).add(match.group(1))

        # A name this file also declares with a type the parse does not know is not checked at all.
        unknown: set[str] = set()
        if _pattern is OWN_DECLARATION:
            for match in ANY_DECLARATION.finditer(text):
                spelt = match.group(1).rsplit("::", 1)[-1]
                if spelt not in _members and spelt not in KEYWORDS:
                    unknown.add(match.group(2))

        for match in APP_ACCESS.finditer(text):
            name, member = match.group(1), match.group(2)
            if name not in identifiers or name in unknown:
                continue
            allowed: set[str] = set()
            for owner in identifiers[name]:
                allowed |= _members[owner]
            checked += 1
            if member not in allowed:
                owners = "/".join(sorted(identifiers[name]))
                wrong.append(f"  FAIL  {source.name} reads {name}.{member}, and {_prefix}{owners} has no such member")

    return checked, wrong


def initialiser_lists(_body: str, _name: str) -> list[tuple[int, int]]:
    """The (start, end) of every `Name(...) : here {` in a class body, as offsets into it.

    A BRACE INITIALISER IS NOT THE BODY, and telling them apart is the whole of this function.
    `ports{a, b}` opens a brace at depth zero exactly as the constructor's body does, and the first
    draft stopped there -- so a list whose members were all brace-initialised read as empty and the
    check passed a tree it should have failed. The difference is what comes immediately before:
    an initialiser's brace follows its NAME, and the body's follows whitespace.
    """
    spans: list[tuple[int, int]] = []
    for ctor in re.finditer(rf"\b{re.escape(_name)}\s*\([^;{{}}]*\)\s*(?:noexcept\s*)?:", _body):
        depth = 0
        for index in range(ctor.end(), len(_body)):
            char = _body[index]
            if char in "([":
                depth += 1
            elif char in ")]":
                depth -= 1
            elif char == "{":
                if depth == 0 and not (index > 0 and (_body[index - 1].isalnum() or _body[index - 1] == "_")):
                    spans.append((ctor.end(), index))
                    break
                depth += 1
            elif char == "}":
                depth -= 1
    return spans


def check_initialisers(_sources: list[Path]) -> tuple[int, list[str]]:
    """Every name an app constructor's member-initialiser list initialises, against its own members.

    WHY THIS EXISTS, and it is the third answer to the same question. The name check reads
    `Elite::Name`, the arity check reads a call's arguments, and the member check reads
    `name.member` -- and none of the three can see a BARE IDENTIFIER, which is what a constructor's
    initialiser list is made of. M3-a-2 and M3-a-3 each moved a batch of `Outpost::Game`'s members
    into `Elite::Universe`, each left an initialiser behind naming one that had gone, and each cost
    a red Windows build several minutes after a push that all fourteen checks had passed
    (§8, "the Windows job on M3-a-2" and "M3-a-3"). Twice is a pattern.

    It reads only what MSVC reports as C2614 -- "X is not a base or member" -- because that is the
    part a regex can settle: the initialised NAME is either a member of the type, a base of it, or
    the type itself (a delegating constructor). What it still cannot see is a bare identifier used
    as an ARGUMENT inside one of those initialisers, which is C2065 and needs a real parser; the
    C2614s came with C2065s both times, so catching the one catches the commit.
    """
    checked = 0
    wrong: list[str] = []

    for source in _sources:
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))
        for name, inherits, body in type_bodies(text):
            lists = initialiser_lists(body, name)

            # The member set is read from the body with those lists BLANKED OUT, because
            # `a(1), gone(2)` reads exactly like two method declarations to a regex -- which is how
            # the first draft of this check declared every initialiser a member of itself.
            without = body
            for start, end in lists:
                without = without[:start] + (" " * (end - start)) + without[end:]

            declared = set(MEMBER_FIELD.findall(without)) | set(MEMBER_METHOD.findall(without))
            declared |= set(re.findall(r"(?:public|private|protected)?\s*(?:[A-Za-z_]\w*::)?([A-Za-z_]\w*)", inherits))
            declared.add(name)

            for start, end in lists:
                for entry in re.finditer(r"(?:^|,)\s*([A-Za-z_]\w*)\s*[({]", body[start:end]):
                    checked += 1
                    if entry.group(1) not in declared:
                        wrong.append(f"  FAIL  {source.name}: {name}'s constructor initialises "
                                     f"{entry.group(1)}, which is not one of its members or bases")

    return checked, wrong


def check_bare_identifiers(_sources: list[Path], _ownMembers: dict[str, set[str]]) -> tuple[int, list[str]]:
    """Every `m_member` and every `_parameter` the app MENTIONS is one it also DECLARES.

    WHY THIS EXISTS, and it is the fourth answer to the question the other three leave open: what
    catches a BARE IDENTIFIER that resolves to nothing? The name check reads `Elite::Name`, the
    arity check reads a call's arguments, the member check reads `name.member` and the initialiser
    check reads a constructor's list -- and none of them can see `m_picture` used where the class
    declares `m_screen`, or `_picture.Resolve(...)` in a function whose parameter is `_screen`.

    That is C2065, "undeclared identifier", and it is what a scripted rename produces when the rule
    that rewrites the USES runs before the rule that would have rewritten the DECLARATION. It cost
    a red Windows build on Resolution.md RS-0, five errors across three files, several minutes after
    a push that all sixteen checks had passed -- the third break through the hole this file exists
    to close, after `DockedShip` and `ClearMessageRows` (see the header).

    Two halves, and each is deliberately conservative:

      - A `m_name` is DECLARED if any type in `Outpost/` declares it, rather than the type whose
        method uses it. Attributing a use to its enclosing class needs a parser; the union catches
        a name that exists nowhere, which is the failure, and never flags one that exists.
      - A `_name` is DECLARED IN ITS FILE if the file has an occurrence with a type in front of it
        -- `Type& _name,` -- rather than in the function that uses it. Same trade, same reason.

    So it reports a name the app declares NOWHERE. A name declared in the wrong place still
    compiles on the Windows job and is still this file's blind spot; only a compiler closes that.
    """
    checked = 0
    wrong: list[str] = []

    declared: set[str] = set()
    for members in _ownMembers.values():
        declared |= {name for name in members if name.startswith("m_")}

    for source in _sources:
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))

        for name in sorted(set(re.findall(r"\bm_[A-Za-z_]\w*", text))):
            checked += 1
            if name not in declared:
                wrong.append(f"  FAIL  {source.name} names {name}, which no type in Outpost/ declares")

        # A parameter is spelled with a type in front of it exactly once, at its declaration.
        parameters = set(re.findall(r"[A-Za-z_>\]]\s*[*&]*\s*(_[a-z]\w*)\s*[,)=\[]", text))
        for name in sorted(set(re.findall(r"\b_[a-z]\w*", text))):
            checked += 1
            if name not in parameters:
                wrong.append(f"  FAIL  {source.name} names {name}, which it declares as no parameter")

    return checked, wrong


def check_braces(_sources: list["Path"]) -> tuple[int, list[str]]:
    """Every app source's braces balance, which the Linux leg cannot otherwise know.

    `Outpost/` compiles on the Windows job alone (R15), so a scripted edit that deletes one brace
    too many is invisible here until CI reports it -- which is exactly what M3-b-2a did, cutting
    `SyncVideoRegisters`'s body away with the three sound methods below it and producing sixteen
    `local function definitions are illegal` errors from one missing `}`. Names and arity cannot
    see that; counting delimiters can, and it is the cheapest half of a parse there is.

    Strings, character literals and comments are removed first, because a brace inside any of them
    is not a brace. `'{'` in this very file's source would otherwise be one.
    """
    counted = 0
    wrong: list[str] = []
    for source in _sources:
        text = source.read_text(encoding="utf-8", errors="replace")
        text = strip_comments(text)
        text = re.sub(r"'(?:\\.|[^'\\])'", "''", text)
        text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
        counted += 1
        for opener, closer, what in (("{", "}", "brace"), ("(", ")", "parenthesis")):
            depth = text.count(opener) - text.count(closer)
            if depth != 0:
                more = "opening" if depth > 0 else "closing"
                wrong.append(f"  FAIL  {source.name} has {abs(depth)} more {more} {what}(s) than the other kind")
    return counted, wrong


# A declaration WITH AN INITIALISER: optional leading keywords, a type that may carry a namespace
# and a template argument list, a name, and then `=` or `{`. `auto` falls out of the keyword group
# and stands as the type, which is what `auto held = ...` needs. `held = 1;` and `ship.speed = 1;`
# do not match, because both want a second identifier where they have punctuation or nothing.
SWITCH_DECLARATION = re.compile(r"^[ \t]*(?!return\b|case\b|default\b|else\b|break\b|delete\b)"
                                r"(?:(?:const|constexpr|static|volatile|unsigned|signed)\s+)*"
                                r"[A-Za-z_][\w:]*\s*(?:<[^;{}()\n]*>)?\s*[*&]?\s+"
                                r"([A-Za-z_]\w*)\s*(?:=(?!=)|\{)", re.MULTILINE)

# What ends a case and so proves the declaration above one is skipped by a label.
SWITCH_LABEL = re.compile(r"^[ \t]*(?:case\b|default\s*:)", re.MULTILINE)


def switch_bodies(_text: str):
    """Each `switch (...) { ... }` body in _text, without its braces."""
    for match in re.finditer(r"\bswitch\s*\(", _text):
        condition = balanced(_text, match.end() - 1)
        if condition is None:
            continue
        opener = _text.find("{", match.end() + len(condition))
        if opener < 0:
            continue
        depth = 0
        for index in range(opener, len(_text)):
            if _text[index] == "{":
                depth += 1
            elif _text[index] == "}":
                depth -= 1
                if depth == 0:
                    yield _text[opener + 1:index]
                    break


def check_switch_scopes(_sources: list[Path]) -> tuple[int, list[str]]:
    """No `case` label jumps past a declaration with an initialiser.

    A `switch` body is ONE scope, so a variable declared in one case is in scope in the next and
    C++ will not let a label jump past its initialisation. Every compiler rejects it; nothing on
    the Linux leg compiles `Outpost/`, so the first sign is `error C2360` from the Windows job --
    which is what M3-b-3c produced by deleting a pacing struct and the braces that had held it,
    leaving `const Elite::ForcedKey begun = ...` bare inside the loop's switch. The brace check
    above could not see it: the braces balanced, which was the whole problem.

    The report is deliberately narrow. A declaration is only named when a `case` or `default`
    label follows it AT THE SAME DEPTH, because that is the shape the compiler objects to; the
    last case of a switch may declare what it likes. Depth is counted over both kinds of bracket,
    so a declaration inside a nested block or a lambda is somebody else's business.
    """
    counted = 0
    wrong: list[str] = []
    for source in _sources:
        text = strip_comments(source.read_text(encoding="utf-8", errors="replace"))
        text = re.sub(r"'(?:\\.|[^'\\])'", "''", text)
        text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
        for body in switch_bodies(text):
            counted += 1
            depth = 0
            depths = []
            for character in body:
                if character in "{(":
                    depths.append(depth)
                    depth += 1
                elif character in "})":
                    depth -= 1
                    depths.append(depth)
                else:
                    depths.append(depth)

            labels = [label.start() for label in SWITCH_LABEL.finditer(body) if depths[label.start()] == 0]
            for match in SWITCH_DECLARATION.finditer(body):
                if depths[match.start()] != 0:
                    continue
                if not any(label > match.start() for label in labels):
                    continue
                wrong.append(f"  FAIL  {source.name} declares {match.group(1)} directly in a switch body "
                             f"and a case label follows it; brace the case (C2360)")
    return counted, wrong


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

    # ---- the app-type pass, planted with the access a Windows build found --------------------
    #
    # `Main.cpp` read `flight.Video()` for one commit after slice M3-b-3a's scripted deletion took
    # the accessor out with the seams around it. Five halves of this check passed it; only MSVC
    # said `error C2039`. So the plant is that access, restored.
    ownMembers = declared_members(APP)
    if "FlightSession" not in ownMembers:
        print("FAIL  the member map did not parse Outpost::FlightSession")
        return 1

    with tempfile.TemporaryDirectory() as folder:
        planted = Path(folder) / "Planted.cpp"
        planted.write_text("void f() { Outpost::FlightSession flight; (void)flight.Video(); }\n", encoding="utf-8")
        ownChecked, ownWrong = check_members([planted], ownMembers, OWN_DECLARATION, "Outpost::")

    if ownChecked == 0:
        print("FAIL  the self-test's app-type access was not resolved at all")
        return 1
    if not ownWrong:
        print("FAIL  the self-test's missing app member was not reported")
        return 1

    realOwn = check_members(sorted(list(APP.glob("*.cpp")) + list(APP.glob("*.h"))), ownMembers,
                            OWN_DECLARATION, "Outpost::")[1]
    if realOwn:
        print("FAIL  the tree itself does not pass the app-type member check")
        for line in realOwn:
            print(line)
        return 1

    # ---- and the bare-identifier check, planted with the rename that broke RS-0 ---------------
    #
    # Resolution.md RS-0 renamed `Elite::Screen` to `Elite::Picture` with a scripted replacement
    # whose rules ran in the wrong order: the uses became `_picture` and `m_picture` and the
    # declarations stayed `_screen` and `m_screen`. Five C2065s on the Windows job, several minutes
    # after every other check had passed. Both halves are planted here.
    with tempfile.TemporaryDirectory() as folder:
        planted = Path(folder) / "Planted.cpp"
        planted.write_text("void Present(const Picture& _screen) { _picture.Resolve(); }\n"
                           "void Turn() { return m_absent->Draw(); }\n", encoding="utf-8")
        bareChecked, bareWrong = check_bare_identifiers([planted], {"Held": {"m_present"}})

    if bareChecked == 0:
        print("FAIL  the self-test's bare identifiers were not read at all")
        return 1
    if not any("_picture" in line for line in bareWrong):
        print("FAIL  the self-test's undeclared parameter was not reported")
        return 1
    if not any("m_absent" in line for line in bareWrong):
        print("FAIL  the self-test's undeclared member was not reported")
        return 1
    if any("_screen" in line for line in bareWrong):
        print("FAIL  the self-test reported a parameter that IS declared")
        return 1

    realBare = check_bare_identifiers(sorted(list(APP.glob("*.cpp")) + list(APP.glob("*.h"))), declared_members(APP))[1]
    if realBare:
        print("FAIL  the tree itself does not pass the bare-identifier check")
        for line in realBare:
            print(line)
        return 1

    # ---- and the initialiser check, planted the same way -------------------------------------
    with tempfile.TemporaryDirectory() as folder:
        planted = Path(folder) / "Planted.cpp"
        # A BRACE INITIALISER BEFORE THE BAD ONE, on purpose: `kept{}` opens a brace at depth zero
        # exactly as the body does, and the first draft of `initialiser_lists` stopped there and
        # reported nothing. A self-test whose list is all parentheses would have passed it.
        planted.write_text("struct Held { int a; int b; Held() : a(1), kept{2}, gone(3) {} };\n", encoding="utf-8")
        started, bad = check_initialisers([planted])

    if started == 0:
        print("FAIL  the self-test's initialiser list was not read at all")
        return 1
    if not bad:
        print("FAIL  the self-test's initialiser of a name that is not a member was not reported")
        return 1
    if started < 3:
        print(f"FAIL  the self-test's list has three entries and the check read {started}")
        return 1

    stale = check_initialisers(sorted(list(APP.glob("*.cpp")) + list(APP.glob("*.h"))))[1]
    if stale:
        print("FAIL  the tree itself does not pass the initialiser check")
        for line in stale:
            print(line)
        return 1

    # ---- and the brace check, planted the same way -------------------------------------------
    with tempfile.TemporaryDirectory() as folder:
        planted = Path(folder) / "Planted.cpp"
        # A BRACE INSIDE A STRING AND A CHARACTER LITERAL, on purpose: a check that counted them
        # would report this file as unbalanced and would report a real one as fine whenever the two
        # mistakes cancelled. The missing `}` is the body of `lost`, which is what M3-b-2a deleted.
        planted.write_text('void lost() { const char* s = "}"; char c = \'{\';\n'
                           'void next() { }\n', encoding="utf-8")
        counted, unbalanced = check_braces([planted])

    if counted == 0:
        print("FAIL  the self-test's source was not read at all")
        return 1
    if not unbalanced:
        print("FAIL  the self-test's missing brace was not reported")
        return 1

    lopsided = check_braces(sorted(list(APP.glob("*.cpp")) + list(APP.glob("*.h"))))[1]
    if lopsided:
        print("FAIL  the tree itself does not pass the brace check")
        for line in lopsided:
            print(line)
        return 1

    # ---- and the switch-scope check, planted with the shape the Windows job rejected ---------
    with tempfile.TemporaryDirectory() as folder:
        planted = Path(folder) / "Planted.cpp"
        # A BRACED CASE ABOVE THE BARE ONE, on purpose: `kept` is legal exactly because it has a
        # scope, so a check that only looked for `Type name =` inside a switch would report it and
        # the tree would learn to ignore the check. And `last` below the final label is legal too.
        planted.write_text("void f(int mode) {\n"
                           "  switch (mode) {\n"
                           "  case 1: { const int kept = 1; (void)kept; return; }\n"
                           "  case 2:\n"
                           "    const int gone = 2;\n"
                           "    (void)gone;\n"
                           "    return;\n"
                           "  case 3:\n"
                           "    const int last = 3;\n"
                           "    (void)last;\n"
                           "    return;\n"
                           "  }\n"
                           "}\n", encoding="utf-8")
        switches, skipped = check_switch_scopes([planted])

    if switches == 0:
        print("FAIL  the self-test's switch body was not read at all")
        return 1
    if len(skipped) != 1 or "gone" not in skipped[0]:
        print(f"FAIL  the self-test expected the bare declaration alone and got {skipped}")
        return 1

    jumped = check_switch_scopes(sorted(list(APP.glob("*.cpp")) + list(APP.glob("*.h"))))[1]
    if jumped:
        print("FAIL  the tree itself does not pass the switch-scope check")
        for line in jumped:
            print(line)
        return 1

    # ---- and the chain check, planted with the access the Windows job rejected ----------------
    with tempfile.TemporaryDirectory() as folder:
        planted = Path(folder) / "Planted.cpp"
        # THE CHAIN IS TWO HOPS AND THE FIRST ONE IS GOOD, on purpose: a check that reported the
        # whole expression whenever any part of it failed to resolve would report every call in
        # `Main.cpp`, and one that stopped at the first hop would never reach the bad one.
        planted.write_text("namespace Outpost { struct Held { int kept = 0; }; struct Holder { Held held; }; }\n"
                           "void f(Outpost::Holder& _it) { (void)_it.held.kept; (void)_it.held.gone; }\n",
                           encoding="utf-8")
        plantedTypes = declared_member_types([Path(folder)])
        plantedMembers = declared_members(Path(folder), ("*.cpp",))
        hops, broken = check_chains([planted], plantedMembers, plantedTypes)

    if hops == 0:
        print("FAIL  the self-test's chain was not walked at all")
        return 1
    if len(broken) != 1 or "gone" not in broken[0]:
        print(f"FAIL  the self-test expected the second hop alone and got {broken}")
        return 1

    appTypes = declared_member_types([LOGIC, APP])
    appMembers = declared_members(LOGIC, ("*.h", "*.cpp"))
    for name, own in declared_members(APP, ("*.h", "*.cpp")).items():
        appMembers.setdefault(name, set()).update(own)
    dangling = check_chains(sorted(list(APP.glob("*.cpp")) + list(APP.glob("*.h"))), appMembers, appTypes)[1]
    if dangling:
        print("FAIL  the tree itself does not pass the chain check")
        for line in dangling:
            print(line)
        return 1

    print(f"OK    self-test passed: a planted Elite:: member, a planted Outpost:: member, a planted "
          f"initialiser, a planted bare identifier, a planted missing brace, a planted unbraced "
          f"case and a planted broken chain were caught, "
          f"{len(members) + len(ownMembers)} types parsed, the tree is clean")
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

    # ---- and the same for the app's OWN types, which the pass above cannot see ----------------
    ownMembers = declared_members(APP)
    ownChecked, badOwn = check_members(sources, ownMembers, OWN_DECLARATION, "Outpost::")
    membersChecked += ownChecked
    wrong.extend(badOwn)

    # ---- and every name the app's own constructors initialise -------------------------------
    initialisersChecked, badInitialisers = check_initialisers(sources)
    wrong.extend(badInitialisers)

    # ---- and every bare m_member and _parameter it mentions -----------------------------------
    bareChecked, badBare = check_bare_identifiers(sources, ownMembers)
    wrong.extend(badBare)

    # ---- and that every one of them still balances its delimiters ----------------------------
    bracesChecked, unbalanced = check_braces(sources)
    wrong.extend(unbalanced)

    # ---- and that no case label in them jumps past a declaration -----------------------------
    switchesChecked, skipped = check_switch_scopes(sources)
    wrong.extend(skipped)

    # ---- and every member it names through an expression rather than a variable ---------------
    everything = declared_members(LOGIC, ("*.h", "*.cpp"))
    for name, own in declared_members(APP, ("*.h", "*.cpp")).items():
        everything.setdefault(name, set()).update(own)
    memberTypes = declared_member_types([LOGIC, APP])
    chainsChecked, badChains = check_chains(sources, everything, memberTypes)
    wrong.extend(badChains)

    print(f"app sources      {len(sources)}")
    print(f"Elite:: names    {len(used)}")
    print(f"calls checked    {checked}")
    print(f"members checked  {membersChecked}")
    print(f"initialisers     {initialisersChecked}")
    print(f"bare names       {bareChecked}")
    print(f"braces balanced  {bracesChecked}")
    print(f"switch bodies    {switchesChecked}")
    print(f"chained members  {chainsChecked}")

    for line in wrong:
        print(line)

    if wrong and not missing:
        print(f"FAIL  {len(wrong)} call(s), member(s), initialiser(s), delimiter(s), case scope(s) or chain(s) do not match")
        return 1

    if missing:
        for name, where in missing.items():
            print(f"  FAIL  Elite::{name} is used by {', '.join(sorted(set(where)))} "
                  f"and is not declared in GameLogic/*.h")
        print(f"FAIL  {len(missing)} name(s) the app uses no longer exist")
        return 1

    if wrong:
        print(f"FAIL  {len(wrong)} call(s), member(s), initialiser(s), delimiter(s), case scope(s) or chain(s) do not match")
        return 1

    print("OK    every Elite:: name the app uses is declared, every call it makes has the right")
    print("      number of arguments, every member it names exists at the end of every chain it")
    print("      walks, every constructor initialises only its own members, and no case label")
    print("      jumps past a declaration")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
