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
    "MathWorkspace": ["p", "p1", "p2", "q", "r", "s", "t", "t1", "u", "cnt", "tgt", "cnt2", "xx", "xxNext", "yy", "yyNext", "k", "k2"],
    "DrawWorkspace": ["x1", "y1", "x2", "y2", "sc", "swap", "xx15Plus4", "xx15Plus5"],
    "GeometryWorkspace": ["xx16", "xx12", "xx2", "xx3", "xx4", "xx17", "xx18", "xx20", "v"],
    "ClipState": ["xx13", "dontclip"],
    "Projection": ["x", "x1", "y", "y1"],
    "K3Block": ["*"],
}

# What the 6502 called each field, for the table.
LABELS: dict[str, str] = {
    "MathWorkspace.p": "**Left for M2-c's third commit.** The kernel's operand and low byte are values since M2-b (`Product{high, low, carry}` out of the multipliers, `SignMag16` into `ADD`); what is left is `PL26` parking the crater's offset between two subtractions -- the planet drawer's own local.", "MathWorkspace.p1": "P+1", "MathWorkspace.p2": "P+2", "MathWorkspace.q": "Q", "MathWorkspace.r": "R",
    "MathWorkspace.s": "S", "MathWorkspace.t": "T", "MathWorkspace.t1": "T1", "MathWorkspace.u": "U", "MathWorkspace.cnt": "CNT",
    "MathWorkspace.tgt": "**Parameter** (M2-c-3). `DrawEllipse` reads what `DrawHalfEllipse`, `DrawPlanetDetail` and `DrawSun` set: what the walk counts up to.", "MathWorkspace.cnt2": "CNT2", "MathWorkspace.xx": "XX", "MathWorkspace.xxNext": "XX+1",
    "MathWorkspace.yy": "**Parameter** (M2-c-3). `ClipSunRow` reads `YY(1 0)` from `DrawSun`/`EraseSun`, which is the centre it clips against; the stardust's went with M2-c-1.", "MathWorkspace.yyNext": "YY+1", "MathWorkspace.k": "K(3 2 1 0)",
    "MathWorkspace.k2": "K2(3 2 1 0)",
    "DrawWorkspace.x1": "**Parameter and result** (M2-c-2). `XX15` is the clipper's line: `LL145` takes six bytes and returns four, `DrawBallLine` reads what it left, and `LOIN` and the pixel helpers take a `Line` value since M2-c-1. What is left here is the clipper's own six.", "DrawWorkspace.y1": "Y1", "DrawWorkspace.x2": "X2", "DrawWorkspace.y2": "Y2",
    "DrawWorkspace.sc": "SC(1 0)", "DrawWorkspace.swap": "SWAP",
    "DrawWorkspace.xx15Plus4": "**Parameter** (M2-c-2), the fifth byte of the clipper's `Line16`.", "DrawWorkspace.xx15Plus5": "XX15+5",
    "GeometryWorkspace.xx16": "**Stage result** (M2-c-3). `ScaleOrientation` (part 3) fills it, `DotProducts` and `TransposeOrientation` read it: `ScaledOrientation`, kept by `LL9`'s frame.", "GeometryWorkspace.xx12": "XX12", "GeometryWorkspace.xx2": "XX2", "GeometryWorkspace.xx3": "XX3",
    "GeometryWorkspace.xx4": "XX4", "GeometryWorkspace.xx17": "XX17", "GeometryWorkspace.xx18": "XX18", "GeometryWorkspace.xx20": "XX20",
    "GeometryWorkspace.v": "V(1 0)",
    "ClipState.xx13": "**Result** (M2-c-2). Written by the clipper, read by `DrawBallLine` after `ClipLine` (which end is on screen): `ClipResult::ends`.", "ClipState.dontclip": "dontclip",
    "Projection.x": "K3", "Projection.x1": "K3+1", "Projection.y": "K4", "Projection.y1": "K4+1",
    "K3Block.*": "**Parameter and result** (M2-c-3). `SPS1` and `TAS2` return the `UnitVector` since M2-c-1 and take the block by reference for the nine bytes they shift; `TAS2`'s tenth byte is its own local. `TAS1`/`VCSUB`/`DCS1` fill and offset the block, which is what M2-c-3 turns into a `Vector24`.",
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
    # ---- MathWorkspace: the arithmetic kernel's channels, taken to values by M2-b ------------------
    "MathWorkspace.p": "**Done (M2-b): the kernel's operand and low byte are values** -- `Product{high, low, carry}` out of the multipliers, `SignMag16` into `ADD`. What is left is the planet drawer parking a crater offset and the dashboard its scratch: M2-c's locals.",
    "MathWorkspace.p1": "**Left for M2-c.** The kernel's three-byte operands are `SignMag24` values since M2-b; what remains is `CHKON`'s (`CircleOffScreen`) horizontal extent, which `DrawSun` reads after `EraseSun` -- the planet drawer's own channel.",
    "MathWorkspace.p2": "**Left for M2-c**, with `p1`.",
    "MathWorkspace.q": "**The frame's Q** (M2-b, §8; risk R22). The kernel takes its multiplier and divisor as values. Two readers are left: the clipper's slope helpers hand it between `MeasureSlope`, `PrepareSlope`, `MultiplySlope` and `DivideSlope` (M2-c's `Slope`), and the altitude check in `EndFlightFrame` takes whatever the frame last left in `Q` as its radicand's low byte -- `MoveShipTail`, `MovePlanetOrSun`, `DivideByShipZ`, `DrawShip` and `DrawSun` write it for that read alone, as the original's `STA Q`s did, and `LOIN`'s is the one this port has never modelled. `DrawDials`/`DrawBar`/`DrawIndicator` and the cloud's `DrawExplosionCloud`/`DrawParticles` are the dashboard's and the explosion's own parameter (M2-c).",
    "MathWorkspace.r": "**Left for M2-c.** The kernel's `Quotient` and `Product` went with M2-b; what remains is the clipper's slope (`MeasureSlope` → `PrepareSlope` → `MultiplySlope`/`DivideSlope`, `StepAlongX`'s x) and `HITCH`'s (`IsHit`) sum of squares -- one `Slope` value and one local.",
    "MathWorkspace.s": "**Left for M2-c**, the high half of the clipper's `(S R)` and `IsHit`'s; the kernel's `SignedSum::sign` and `SignMag16::hi` since M2-b.",
    "MathWorkspace.t": "**Left for M2-c's second and third commits.** `StepAlongX`/`StepAlongY` read `T` after `PrepareSlope` (the slope helpers' shared value, M2-c-2's `Slope`) and `DrawBallLine` reads what `DrawBall` set (`BLINE`'s step, M2-c-3's parameter); every other writer initialises it. The kernel's `T` is a local since M2-b, and `HLOIN`'s and `BOX2`'s are locals since M2-c-1 (§8: the port wrote `T2`).",
    "MathWorkspace.t1": "**One parameter, else local** (M2-c-3). `DrawShip` and `SpawnChildShip` use it as their own scratch; `DIALS`'s threshold became `DrawBar`'s parameter in M2-c-1 and the kernel's `T1` is a local since M2-b.",
    "MathWorkspace.u": "**Local**. Two writers, no reader that did not write it first; `LL61`'s incoming `U` is a parameter since M2-b.",
    "MathWorkspace.cnt": "**Local, and one parameter** (M2-c-3). Every writer initialises it (its own comment, §6.49); the hand-over is `DrawBall` → `DrawBallLine`, `CIRCLE2` giving `BLINE` its segment count.",
    "MathWorkspace.tgt": "**Parameter** (M2-c). `DrawEllipse` reads what `DrawHalfEllipse`, `DrawPlanetDetail` and `DrawSun` set: what the walk counts up to.",
    "MathWorkspace.cnt2": "**Parameter** (M2-c-3). `DrawEllipse` reads the starting angle `SetMeridianAngle` and `DrawPlanetDetail` set; `ShowTitleShip`'s use is its own local.",
    "MathWorkspace.xx": "**Parameter** (M2-c-3). `XX(1 0)` is the sun's half-width handed to `ClipSunRow` and the sliver it draws; the stardust's is a `SignMag16` local of each mover since M2-c-1.",
    "MathWorkspace.xxNext": "**Parameter** (M2-c-3), the high byte of the same.",
    "MathWorkspace.yy": "**Parameter** (M2-c). `ClipSunRow` reads `YY(1 0)` from `DrawSun`/`EraseSun`; the stardust writes and reads its own.",
    "MathWorkspace.yyNext": "**Parameter** (M2-c-3), the high byte of the same.",
    "MathWorkspace.k": "**Result and parameter** (M2-c). `DivideByShipZ` leaves the quotient in `K` and `DivideAxisByZ`, `DivideToScreenOffset` and `DrawPlanetOrSun` read it after; `CircleOffScreen`, `DrawBall` and `DrawBar` read what their callers set (the radius, the bar's colours): a `KBlock` value in and out. `MV40`, `MAS1` and `TAS1` hold theirs as `KBlock` locals since M2-b.",
    "MathWorkspace.k2": "**Parameter, and one byte of state** (M2-c; M2-b, §8). `DrawEllipse` reads the two axes `LoadTwoAxes` set; `DrawSun` and `DrawPlanetDetail` write their own. `MV40` holds its `K2` as a local since M2-b -- except the bottom byte, which it never writes and reads for the carry of its first addition: whatever the last drawer left there, on purpose.",
    # ---- DrawWorkspace: the line being drawn, M2-c ------------------------------------------------
    "DrawWorkspace.x1": "**Parameter and result** (M2-c). `XX15` is the line: `LOIN` and the pixel helpers take its four ends (`Line`), the clipper takes six bytes and returns four (`Line16` in, `Line` out), `DotProducts` and `TAS2` take a vector in the same bytes, the stardust its point. No read outlives a call except through `SWAP`.",
    "DrawWorkspace.y1": "**Parameter and result** (M2-c-2), with `x1`.",
    "DrawWorkspace.x2": "**Parameter and result** (M2-c-2), with `x1`.",
    "DrawWorkspace.y2": "**Parameter and result** (M2-c-2), with `x1`.",
    "DrawWorkspace.col": "**Parameter** (M2-c). `PlotDash` reads the colour mask its caller set (`DrawBar`, `DrawCompassDot`, `DrawScannerBlip`).",
    "DrawWorkspace.zz": "**Parameter** (M2-c). `PlotPixel` reads the distance its caller set; `PlotRelativePixel` reads it after `PlotPixel`, which does not write it.",
    "DrawWorkspace.t2": "**Local** to `DrawHorizontalLine` and `DrawBorder`.",
    "DrawWorkspace.r2": "**Local** to `DrawHorizontalLine`.",
    "DrawWorkspace.sc": "**State, deliberately** (M2-c leaves it; M4 names it). `DIALS` sets the screen pointer once and `DIL`/`DIL2` advance it seven calls running (its own comment, slice 3d-b): a cursor the dashboard drawer owns, not scratch.",
    "DrawWorkspace.swap": "**Result** (M2-c-2). Written by the clipper and by `LOIN`, read by `DrawBallLine` after `ClipLine` and by `EraseBall` after `DrawLine` (§6.46). `LOIN` returns it as `DrawnLine::swapped` since M2-c-1; the byte the clipper leaves is `ClipResult::swapped`.",
    "DrawWorkspace.xx15Plus4": "**Parameter** (M2-c), the fifth byte of the clipper's `Line16`.",
    "DrawWorkspace.xx15Plus5": "**Parameter** (M2-c-2), the sixth; `ClipLine` reads it back from `ClipLineKeepingSwap` for `LL147`'s accumulator, which is that entry point's own result.",
    # ---- GeometryWorkspace: LL9's frame, M2-c -----------------------------------------------------
    "GeometryWorkspace.xx16": "**Stage result** (M2-c). `ScaleOrientation` (part 3) fills it, `DotProducts` and `TransposeOrientation` read it: `ScaledOrientation`, kept by `LL9`'s frame.",
    "GeometryWorkspace.xx12": "**Stage result and the clipper's local** (M2-c-2 and M2-c-3). `DotProducts` leaves three dot products that part 4 reads; the clipper's helpers use the same bytes as their own scratch, which the frame separates. `DIALS` stopped borrowing them in M2-c-1.",
    "GeometryWorkspace.xx2": "**Stage result** (M2-c-3). Face visibility, written by part 4 and read by `EitherFaceVisible`; `RunDockingComputer` reads `XX2+10` as the memory it is (§6.112), which the frame keeps addressable for that one reader.",
    "GeometryWorkspace.xx3": "**Stage result** (M2-c-3). The projected vertices, `LL9`'s own, handed to `DrawExplosionCloud` for the burst.",
    "GeometryWorkspace.xx4": "**Local** of `LL9`'s frame (the distance).",
    "GeometryWorkspace.xx17": "**Local** of `LL9`'s frame (the loop counter).",
    "GeometryWorkspace.xx18": "**Local** of `LL9`'s frame (the halved position).",
    "GeometryWorkspace.xx20": "**Local** of `LL9`'s frame (the loop bound).",
    "GeometryWorkspace.v": "**Local** of `LL9`'s frame (the walker, an index since M1-e).",
    # ---- ClipState -------------------------------------------------------------------------------
    "ClipState.xx13": "**Result** (M2-c). Written by the clipper, read by `DrawBallLine` after `ClipLine` (which end is on screen): `ClipResult::ends`.",
    "ClipState.dontclip": "**Constant in this port.** Only `ZERO` writes it (to zero) and only `LL145` reads it; nothing sets bit 7, because the one routine that does in the original is not part of this build's paths. A `bool` parameter of the clipper, `false` at every caller, until a caller appears.",
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
