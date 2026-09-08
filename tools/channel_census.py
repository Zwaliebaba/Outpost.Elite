#!/usr/bin/env python3
"""The channel census -- who writes and who reads each zero-page workspace field (Modernize.md M2-a).

`MathWorkspace`, `DrawWorkspace`, `GeometryWorkspace`, `ClipState`, `Projection`, `K3Block` and
`Projection` are the port's zero-page scratch: routines hand values to each other through
them instead of through parameters and results (pattern P2). Before M2 replaces that with
explicit signatures it has to know, for every field, which routines WRITE it, which routines READ
it before writing it in their own body -- a value that arrived from outside, through a caller or
through a callee -- and which only ever use it as scratch of their own. That is this census.

It is static and it is honest about what it sees: a routine's body is read top to bottom, every
access to a workspace parameter's field (or to a `FlightScreen`/`FlightLoop` member that is one)
is classified as a write (`=`, `+=`, `++`, ...) or a read, and a workspace passed whole to another
routine is a "pass" that may do either. A field is "in" for a routine when its first access is a
read that no pass precedes -- it came from the caller -- and "after a call" when a pass precedes
that first read -- it came, most likely, from the callee. It does not follow control flow, so a
read inside an `if` that a write in the other branch never reaches counts as a read-first; the
verdicts in `VERDICTS` are where a human reading corrects that, and the table in
Design/Modernize.md section 4.3 is generated from both.

    python tools/channel_census.py            # the table, as Markdown
    python tools/channel_census.py --report   # every routine's events, per field
    python tools/channel_census.py --check    # the table in Design/Modernize.md matches the tree

A field the tree has and `VERDICTS` lacks is a check failure: the census is complete or it is not.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LOGIC = REPO / "GameLogic"
PLAN = REPO / "Design" / "Modernize.md"
START = "<!--census:start-->"
END = "<!--census:end-->"

# The workspaces and their fields, as the headers declare them.
WORKSPACES: dict[str, list[str]] = {
    "MathWorkspace": ["q", "k2Low"],
    "DrawWorkspace": ["sc"],
    "GeometryWorkspace": ["scaledOrientation", "dotProducts", "faceVisible", "projectedVertices"],
    "ClipState": ["clippingOff"],
    "Projection": ["x", "x1", "y", "y1"],
    "K3Block": ["*"],
}

# What the 6502 called each field, for the table.
LABELS: dict[str, str] = {
    "MathWorkspace.q": "Q", "MathWorkspace.k2Low": "K2",
    "DrawWorkspace.sc": "SC(1 0)",
    "GeometryWorkspace.scaledOrientation": "XX16", "GeometryWorkspace.dotProducts": "XX12", "GeometryWorkspace.faceVisible": "XX2",
    "GeometryWorkspace.projectedVertices": "XX3",
    "ClipState.clippingOff": "dontclip",
    "Projection.x": "K3", "Projection.x1": "K3+1", "Projection.y": "K4", "Projection.y1": "K4+1",
    "K3Block.*": "K3 to K3+9",
}

# `FlightScreen` and `FlightLoop` members that are a workspace, reached as `screen.math.q`.
AGGREGATE_MEMBERS: dict[str, str] = {
    "math": "MathWorkspace", "draw": "DrawWorkspace", "geometry": "GeometryWorkspace", "clip": "ClipState", "projection": "Projection",
    "axes": "K3Block",
}

# The verdicts -- the human reading the census informs, one per field, which M2's slices carry
# out. "local" is scratch within one routine; "result" a value a callee hands back; "parameter"
# a value a caller hands in; "state" a value that outlives the call on purpose, with the reason.
VERDICTS: dict[str, str] = {
    # ---- MathWorkspace: two bytes that outlive their writer on purpose ---------------------------
    "MathWorkspace.q": "**The frame's Q**, and one of the two bytes left (M2-b, §8; risk R22). `MA23`'s altitude check takes whatever the frame last left in `Q` as its radicand's low byte, so `MoveShipTail`, `MovePlanetOrSun`, `DivideByShipZ`, `DrawShip`, `DrawSun`, `DOEXP`'s two routines and the clipper's `LL115` and `LL118` write it for that read alone, as the original's `STA Q`s do. R22 said `LOIN` was a tenth writer this port never modelled and it is not: this build's `LOIN` works in `P2`, `Q2`, `R2` and `S2` at 188-191 and never touches `Q` at 154. Closed 2026-09-06 by measurement -- `TheFramesOwnQReachesTheAltitude` runs the whole frame with the planet in range and compares `ALTIT`.",
    "MathWorkspace.k2Low": "**One byte of state, deliberately** (M2-b, §8). `MV40` never writes `K2` and its `LDA K / CLC / ADC K2` reads this byte for the carry of its first addition, so what it gets is whatever the last planet or sun drawer left there a frame ago. `PL9`, `PL26` and `SUN` store to it where the original's `STA K2` is; the other three bytes of the block are the ellipse's axes and travel as an `EllipseAxes` value since M2-c-3.",
    # ---- DrawWorkspace: the dashboard's screen cursor, all that is left of it after M2-c-2 --------
    "DrawWorkspace.sc": "**State, deliberately** (M2-c leaves it; M4 names it). `DIALS` sets the screen pointer once and `DIL`/`DIL2` advance it seven calls running (its own comment, slice 3d-b): a cursor the dashboard drawer owns, not scratch.",
    # ---- GeometryWorkspace: LL9's four stage results ---------------------------------------------
    "GeometryWorkspace.scaledOrientation": "**Stage result** (M2-c-3 leaves it in the frame; M4 makes it a pipeline). `LL15`/`LL21` fill it, `LL51` and the transpose read it, and the planet drawer uses the same six bytes for the ellipse's four signs -- two meanings, one block, as `RAT` and `RAT2` are.",
    "GeometryWorkspace.dotProducts": "**Stage result** (M2-c-3 leaves it in the frame). `LL51` leaves three dot products that `LL9` parts 4 and 6 read, and `LL83`/`LL115` work in the same bytes while a line is being clipped -- the original's reuse, which nothing reads across. `DIALS` stopped borrowing them in M2-c-1.",
    "GeometryWorkspace.faceVisible": "**Stage result, and one reader outside** (M2-c-3 leaves it). Face visibility, written by part 4 and read by parts 6 and 10; `DOCKIT` reads `XX2+10` as the memory it is (§6.112), which is why the frame is a struct and not four more locals.",
    "GeometryWorkspace.projectedVertices": "**Stage result, and one reader outside** (M2-c-3 leaves it). The projected vertices, filled by part 8 and read by parts 9 to 11; `DOEXP` copies them onto the heap for the burst, which is the frame's second outward reader.",
    # ---- ClipState -------------------------------------------------------------------------------
    "ClipState.clippingOff": "**State one screen writes and the clipper reads** (M2-c-2 leaves it). `TT23` sets it to 199 so the short-range chart can use the whole screen and `RES2` clears it again -- `Main.cpp` and `ResetShipAndBubble` in this port -- so it is not the clipper's scratch and did not become a `ClipResult` field with `XX13` and `SWAP`. `TT23` writes `Yx2M1` in the same two instructions and that byte is on `PlanetSunState`; whichever slice wires `TT23` puts this one beside it.",
    # ---- Projection -----------------------------------------------------------------------------
    "Projection.x": "**State that outlives the call, deliberately** (§4.3's `PROJ` row; ADR-001 §6, `SHPPT`). `Project` writes it half at a time and `DrawShipAsPoint`, the planet drawer's `CircleOffScreen`, `DrawBall`, `DrawEllipse` and `DrawSun` read what the last `Project` left; `DrawPlanetDetail` rewrites it for the crater. Stays a parameter.",
    "Projection.x1": "**State, deliberately**, with `x`: the stale `K3+1` `SHPPT` reads is the ADR row.",
    "Projection.y": "**State, deliberately**, with `x`.",
    "Projection.y1": "**State, deliberately**, with `x`.",
    # ---- K3Block ------------------------------------------------------------------------------------
    "K3Block.*": "**Parameter and result** (M2-c). `SPS1` (`LoadPlanetAxis`, `NormaliseAxes`) leaves the vector `BuildUnitVector` and part 9 read; `TAS2`/`OffsetAxis` take and return it: `UnitVector`/`Vector24`, §4.3's `XX15 after SPS1` row.",
}

WRITE_AFTER = re.compile(r"^\s*(?:\[[^\]]*\]\s*)*(?:=(?!=)|\+=|-=|\|=|&=|\^=|<<=|>>=|\+\+|--)")
WRITE_BEFORE = re.compile(r"(?:\+\+|--)\s*$")
IDENT = r"[A-Za-z_][A-Za-z0-9_]*"


@dataclass
class Routine:
    file: str
    name: str
    signature: str
    body: list[str]
    line: int
    params: dict[str, str] = field(default_factory=dict)  # parameter name -> workspace type


@dataclass
class Event:
    kind: str  # read | write | pass
    line: int
    callee: str = ""


def strip_comments(_line: str) -> str:
    line = re.sub(r"//.*$", "", _line)
    line = re.sub(r"/\*.*?\*/", "", line)
    return line


def routines_of(_path: Path) -> list[Routine]:
    """Every function body in one source file, by its Allman brace at namespace indentation."""
    lines = _path.read_text(encoding="utf-8").split("\n")
    found: list[Routine] = []
    index = 0
    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        indent = len(line) - len(line.lstrip(" "))
        if stripped == "{" and indent in (2, 4):
            # the signature is the run of lines just above, back to a blank, a comment or a brace
            back = index - 1
            head: list[str] = []
            while back >= 0:
                candidate = lines[back]
                text = candidate.strip()
                if not text or text.startswith(("//", "/*", "*", "}", "#")) or text.endswith("*/"):
                    break
                head.insert(0, text)
                back -= 1
            signature = " ".join(head)
            if "(" in signature and not re.match(r"^(?:struct|class|enum|union|namespace|template)\b", signature):
                depth = 0
                body: list[str] = []
                cursor = index
                while cursor < len(lines):
                    code = strip_comments(lines[cursor])
                    depth += code.count("{") - code.count("}")
                    body.append(lines[cursor])
                    if depth == 0:
                        break
                    cursor += 1
                name_match = re.search(r"(" + IDENT + r"(?:::" + IDENT + r")?)\s*\(", signature)
                name = name_match.group(1) if name_match else "?"
                routine = Routine(_path.name, name, signature, body, back + 2)
                for workspace in WORKSPACES:
                    for match in re.finditer(r"\b" + workspace + r"&\s+(" + IDENT + r")", signature):
                        routine.params[match.group(1)] = workspace
                    # a local reference to a workspace (`MathWorkspace& math = screen.math;`) is a
                    # receiver too -- `TACTICS` binds three of them, which the first census missed
                    for line in body:
                        for match in re.finditer(r"\b" + workspace + r"&\s+(" + IDENT + r")\s*=", strip_comments(line)):
                            routine.params[match.group(1)] = workspace
                found.append(routine)
                index = cursor + 1
                continue
        index += 1
    return found


def events_of(_routine: Routine) -> dict[str, list[Event]]:
    """Every access to a workspace field in one body, in order, keyed `Workspace.field`."""
    events: dict[str, list[Event]] = defaultdict(list)
    receivers: list[tuple[str, str]] = [(name, workspace) for name, workspace in _routine.params.items()]
    for offset, raw in enumerate(_routine.body):
        line = strip_comments(raw)
        if not line.strip():
            continue
        number = _routine.line + offset
        # aggregate members: `screen.math.q`, `_loop.screen.draw.x1`, `_loop.clip.xx13`
        for match in re.finditer(r"\b(" + IDENT + r"(?:\." + IDENT + r")*)\.(math|draw|geometry|clip|projection|axes)\b(?=\.|\[|\s*[,)])", line):
            workspace = AGGREGATE_MEMBERS[match.group(2)]
            receiver = match.group(1) + "." + match.group(2)
            classify(events, line, match.end(), receiver, workspace, number)
        for name, workspace in receivers:
            for match in re.finditer(r"\b" + re.escape(name) + r"\b", line):
                classify(events, line, match.end(), name, workspace, number)
    return events


def classify(_events: dict[str, list[Event]], _line: str, _end: int, _receiver: str, _workspace: str, _number: int) -> None:
    rest = _line[_end:]
    before = _line[:_end - len(_receiver.split(".")[-1])]
    if _workspace == "K3Block":
        key = "K3Block.*"
        if rest.startswith("["):
            close = rest.find("]")
            kind = "write" if WRITE_AFTER.match(rest[close + 1:]) else "read"
            _events[key].append(Event(kind, _number))
        elif not rest.startswith("."):
            _events[key].append(Event("pass", _number, callee_of(_line, _end)))
        return
    field_match = re.match(r"\.(" + IDENT + r")", rest)
    if field_match and field_match.group(1) in WORKSPACES[_workspace]:
        key = _workspace + "." + field_match.group(1)
        after = rest[field_match.end():]
        kind = "write" if (WRITE_AFTER.match(after) or WRITE_BEFORE.search(before)) else "read"
        _events[key].append(Event(kind, _number))
    elif not rest.startswith((".", "[")):
        callee = callee_of(_line, _end)
        for name in WORKSPACES[_workspace]:
            _events[_workspace + "." + name].append(Event("pass", _number, callee))


def callee_of(_line: str, _at: int) -> str:
    """The name of the call whose argument list the position is inside, or '?'."""
    depth = 0
    cursor = _at - 1
    while cursor >= 0:
        char = _line[cursor]
        if char == ")":
            depth += 1
        elif char == "(":
            if depth == 0:
                match = re.search(r"(" + IDENT + r"(?:::" + IDENT + r")?)\s*$", _line[:cursor])
                return match.group(1) if match else "?"
            depth -= 1
        cursor -= 1
    return "?"


@dataclass
class Census:
    writers: dict[str, set[str]] = field(default_factory=lambda: defaultdict(set))
    from_caller: dict[str, set[str]] = field(default_factory=lambda: defaultdict(set))
    after_call: dict[str, dict[str, set[str]]] = field(default_factory=lambda: defaultdict(lambda: defaultdict(set)))
    scratch: dict[str, set[str]] = field(default_factory=lambda: defaultdict(set))


def census(_root: Path = LOGIC) -> tuple[Census, list[tuple[Routine, dict[str, list[Event]]]]]:
    result = Census()
    details: list[tuple[Routine, dict[str, list[Event]]]] = []
    for path in sorted(_root.glob("*.cpp")):
        for routine in routines_of(path):
            events = events_of(routine)
            if not events:
                continue
            details.append((routine, events))
            for key, sequence in events.items():
                real = [event for event in sequence if event.kind != "pass"]
                if not real:
                    continue
                if any(event.kind == "write" for event in real):
                    result.writers[key].add(routine.name)
                first = real[0]
                if first.kind == "read":
                    passes_before = [event.callee for event in sequence if event.kind == "pass" and event.line <= first.line]
                    if passes_before:
                        result.after_call[key][routine.name].add(passes_before[-1])
                    else:
                        result.from_caller[key].add(routine.name)
                else:
                    if not any(event.kind == "pass" for event in sequence):
                        result.scratch[key].add(routine.name)
    return result, details


def keys() -> list[str]:
    return [workspace + "." + name for workspace, names in WORKSPACES.items() for name in names]


def table(_census: Census) -> str:
    rows = ["| Field | 6502 | Written by | Read before written, from the caller | Read after a call | Verdict |", "|---|---|---|---|---|---|"]
    for key in keys():
        writers = ", ".join(sorted(_census.writers.get(key, ()))) or "—"
        callers = ", ".join(sorted(_census.from_caller.get(key, ()))) or "—"
        after = ", ".join(f"{who} (after {', '.join(sorted(callees))})" for who, callees in sorted(_census.after_call.get(key, {}).items())) or "—"
        verdict = VERDICTS.get(key, "**UNKNOWN**")
        rows.append(f"| `{key}` | `{LABELS[key]}` | {writers} | {callers} | {after} | {verdict} |")
    return "\n".join(rows)


def report(_details: list[tuple[Routine, dict[str, list[Event]]]]) -> str:
    out: list[str] = []
    for routine, events in _details:
        out.append(f"{routine.file}:{routine.line} {routine.name}")
        for key in sorted(events):
            trail = " ".join(f"{event.kind[0]}{event.line}" + (f"({event.callee})" if event.kind == "pass" else "") for event in events[key])
            out.append(f"    {key}: {trail}")
    return "\n".join(out)


def check(_census: Census) -> list[str]:
    complaints: list[str] = []
    for key in keys():
        if key not in VERDICTS:
            complaints.append(f"{key}: no verdict in tools/channel_census.py")
    text = PLAN.read_text(encoding="utf-8")
    if START not in text or END not in text:
        return complaints + [f"{PLAN.name}: no {START} / {END} markers"]
    recorded = text[text.index(START) + len(START):text.index(END)].strip()
    if recorded != table(_census):
        complaints.append(f"{PLAN.name}: the census table differs from the tree (regenerate with `python tools/channel_census.py`)")
    return complaints


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--report", action="store_true", help="every routine's events, per field")
    parser.add_argument("--check", action="store_true", help="the table in Design/Modernize.md matches the tree")
    args = parser.parse_args()
    result, details = census()
    if args.report:
        print(report(details))
        return 0
    if args.check:
        complaints = check(result)
        for line in complaints:
            print("      " + line)
        if complaints:
            print(f"FAIL  {len(complaints)} census problem(s)")
            return 1
        print(f"OK    the channel census names every one of the {len(keys())} workspace fields and matches Design/Modernize.md")
        return 0
    print(table(result))
    return 0


if __name__ == "__main__":
    sys.exit(main())
