#!/usr/bin/env python3
"""Print only the lines the C64 build assembles from a multi-version library file.

The upstream library is one tree serving ten versions of Elite, and a routine's C64 form is
whatever survives its `IF` / `ELIF` / `ELSE` / `ENDIF` conditionals. Reading those by eye is how
you end up porting the BBC Master's version of a routine by mistake, so this evaluates them.

    python tools/c64_source.py library/common/main/subroutine/sve.asm
    python tools/c64_source.py --code library/common/main/subroutine/tt25.asm

Paths are relative to Upstream/elite-source-code-library/ or to the repository root, and `--code`
drops comments and blank lines so what is left is the instruction stream.

It also says when a file's last instruction is not an `RTS` or a `JMP`, and names the file the
build assembles next. That is the one thing a single file cannot tell you and the one that costs
most to miss: `TAS2` ends `STA XX15+2` and runs straight on into `NORM`, so a port that stopped
where the file does was a different routine that happened to share a name (§6.62).

The symbol values come from the master build (`_VERSION = 8`, `_VARIANT = 1`) and from the
definitions at the top of MasterFile/elite-source.asm.  An unknown symbol is an error rather than
a silent FALSE: guessing there is exactly the mistake this exists to prevent.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UPSTREAM = REPO / "Upstream" / "elite-source-code-library"

# MasterFile/elite-build-options.asm plus the derived flags in elite-source.asm, for _VERSION = 8
# and _VARIANT = 1. Anything not here is not defined for this build.
SYMBOLS: dict[str, bool] = {
    "_DEMO_VERSION": False,
    "_CASSETTE_VERSION": False,
    "_DISC_VERSION": False,
    "_6502SP_VERSION": False,
    "_MASTER_VERSION": False,
    "_ELECTRON_VERSION": False,
    "_ELITE_A_VERSION": False,
    "_NES_VERSION": False,
    "_C64_VERSION": True,
    "_APPLE_VERSION": False,
    "_GMA85_NTSC": True,
    "_GMA86_PAL": False,
    "_GMA_RELEASE": True,
    "_SOURCE_DISK_BUILD": False,
    "_SOURCE_DISK_FILES": False,
    "_SOURCE_DISK": False,
    "_DISC_DOCKED": False,
    "_DISC_FLIGHT": False,
    "_ELITE_A_DOCKED": False,
    "_ELITE_A_FLIGHT": False,
    "_ELITE_A_SHIPS_R": False,
    "_ELITE_A_SHIPS_S": False,
    "_ELITE_A_SHIPS_T": False,
    "_ELITE_A_SHIPS_U": False,
    "_ELITE_A_SHIPS_V": False,
    "_ELITE_A_SHIPS_W": False,
    "_ELITE_A_ENCYCLOPEDIA": False,
    "_ELITE_A_6502SP_IO": False,
    "_ELITE_A_6502SP_PARA": False,
    "_REMOVE_CHECKSUMS": False,
    "_MATCH_ORIGINAL_BINARIES": True,
    "_MAX_COMMANDER": False,
    # Variants of the BBC disc build, which this one is not.
    "_STH_DISC": False,
    "_SRAM_DISC": False,
    "_IB_DISC": False,
    "_SNG45": False,
    "_SNG47": False,
    "_SOURCE_DISC": False,
}

IF_RE = re.compile(r"^\s*IF\s+(.*)$", re.IGNORECASE)
ELIF_RE = re.compile(r"^\s*ELIF\s+(.*)$", re.IGNORECASE)
ELSE_RE = re.compile(r"^\s*ELSE\b", re.IGNORECASE)
ENDIF_RE = re.compile(r"^\s*ENDIF\b", re.IGNORECASE)
WORD_RE = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


def evaluate(_condition: str, _where: str) -> bool:
    """True when the C64 build takes this branch."""
    text = _condition.split("\\", 1)[0].strip()

    def word(match: re.Match[str]) -> str:
        name = match.group(0)
        if name.upper() in ("OR", "AND", "NOT", "TRUE", "FALSE"):
            return {"OR": "or", "AND": "and", "NOT": "not", "TRUE": "True", "FALSE": "False"}[name.upper()]
        if name not in SYMBOLS:
            sys.exit(f"{_where}: unknown build symbol {name!r} in `IF {text}` -- add it to SYMBOLS")
        return str(SYMBOLS[name])

    expression = WORD_RE.sub(word, text)
    try:
        return bool(eval(expression, {"__builtins__": {}}, {}))  # noqa: S307 -- a closed symbol set
    except Exception as error:  # pragma: no cover -- a malformed condition is a source problem
        sys.exit(f"{_where}: cannot evaluate `IF {text}` ({error})")


def filter_file(_path: Path, _code_only: bool) -> list[str]:
    kept: list[str] = []
    # Each frame is [some branch has been taken, this branch is the live one].
    stack: list[list[bool]] = []

    for number, line in enumerate(_path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        where = f"{_path.name}:{number}"

        match = IF_RE.match(line)
        if match:
            live = all(frame[1] for frame in stack) and evaluate(match.group(1), where)
            stack.append([live or not all(frame[1] for frame in stack), live])
            continue

        match = ELIF_RE.match(line)
        if match and stack:
            taken, _ = stack[-1]
            live = (not taken) and evaluate(match.group(1), where)
            stack[-1] = [taken or live, live]
            continue

        if ELSE_RE.match(line) and stack:
            taken, _ = stack[-1]
            stack[-1] = [True, not taken]
            continue

        if ENDIF_RE.match(line) and stack:
            stack.pop()
            continue

        if not all(frame[1] for frame in stack):
            continue

        if _code_only:
            stripped = line.split("\\", 1)[0].rstrip()
            if not stripped.strip():
                continue
            kept.append(stripped)
        else:
            kept.append(line.rstrip())

    if stack:
        sys.exit(f"{_path.name}: {len(stack)} unclosed IF block(s)")
    return kept


def resolve(_argument: str) -> Path:
    for candidate in (UPSTREAM / _argument, REPO / _argument, Path(_argument)):
        if candidate.is_file():
            return candidate
    sys.exit(f"error: {_argument} not found under {UPSTREAM} or {REPO}")


# What ends an instruction stream. Anything else at the bottom of a file means the routine keeps
# going in the next INCLUDE, which the file itself gives no sign of (§6.62).
TERMINATORS = ("RTS", "RTI", "JMP", "BRK")

MASTER = REPO / "MasterFile" / "elite-source.asm"
INCLUDE_RE = re.compile(r'^\s*INCLUDE\s+"([^"]+)"', re.IGNORECASE)


def next_include(_path: Path) -> str | None:
    """The file the C64 build assembles immediately after this one, if it assembles it at all."""
    try:
        includes = [
            match.group(1)
            for match in (INCLUDE_RE.match(line) for line in filter_file(MASTER, True))
            if match
        ]
    except SystemExit:  # pragma: no cover -- a master file that will not parse is a bigger problem
        return None

    try:
        name = str(_path.relative_to(UPSTREAM))
    except ValueError:
        return None

    for index, include in enumerate(includes):
        if include.replace("\\", "/") == name.replace("\\", "/") and index + 1 < len(includes):
            return includes[index + 1]
    return None


def falls_through(_lines: list[str]) -> str | None:
    """The last instruction, when it is not one that ends a routine."""
    for line in reversed(_lines):
        text = line.split("\\", 1)[0].strip()
        if not text or text.startswith("."):
            continue
        opcode = text.split()[0].upper()
        return None if opcode in TERMINATORS else text
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="library files, relative to the upstream tree or the repository")
    parser.add_argument("--code", action="store_true", help="drop comments and blank lines")
    arguments = parser.parse_args()

    for index, argument in enumerate(arguments.paths):
        path = resolve(argument)
        if len(arguments.paths) > 1:
            print(f"{'' if index == 0 else chr(10)}=== {path.relative_to(REPO)}")
        lines = filter_file(path, arguments.code)
        for line in lines:
            print(line)

        # The one thing a single file cannot tell you: whether the routine ends here. §6.11 made
        # "does it actually return?" part of reading a routine, and §6.62 is what it costs when
        # the answer is in a different file -- `TAS2`'s last instruction is `STA XX15+2` and its
        # next one is the first of `NORM`.
        last = falls_through(filter_file(path, True))
        if last is not None:
            following = next_include(path)
            branch = last.split()[0].upper() in (
                "BCC", "BCS", "BEQ", "BMI", "BNE", "BPL", "BVC", "BVS",
            )
            if branch:
                print(f"\n\\ FALLS THROUGH when `{last}` is not taken -- and a branch on a flag the")
                print("\\ instruction above it just set can be unconditional in practice, so read it.")
            else:
                print(f"\n\\ FALLS THROUGH -- the last instruction is `{last}`, not an RTS or a JMP.")
            if following:
                print(f"\\ Execution continues into {following} -- that routine is part of this one.")
            else:
                print("\\ This build does not INCLUDE the file, so what follows is whatever links next.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BrokenPipeError:  # `| head` closes the pipe, which is not an error here
        sys.stderr.close()
        raise SystemExit(0)
