#!/usr/bin/env python3
"""Check that no Markdown table row has more cells than its header.

GitHub renders a table with the header's number of columns and SILENTLY DROPS anything past it,
so a row that grew an extra `| ... |` on the end is invisible to everyone reading the rendered
file. That is how thirteen rows of `Source-Inventory.md` came to hold notes nobody could read: a
slice finishes, its result is appended to the row as a new cell, and the raw file looks right
(§6.72).

Nothing here is about style. A short row is fine -- GitHub pads it -- and so is any amount of
prose in a cell. The one thing that loses information is a long row, so that is the one thing
this checks.

    python tools/check_docs.py [path ...]

Defaults to every tracked *.md outside Upstream/.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# `packages/` and `.claude/` are gitignored, so CI never sees them and a local run did: NuGet's own
# `readme.md` has two over-wide table rows, which made `check_all.py` red on any machine that had
# restored the executable's packages. A check that fails on files the repository does not track
# teaches people to skip it.
SKIP = {"Upstream", ".git", ".claude", ".vs", "packages", "node_modules", "out", "build", "x64"}


def split_cells(_line: str) -> list[str]:
    """The cells of a table row, splitting only on pipes outside `backticks`."""
    body = _line.strip()
    if body.startswith("|"):
        body = body[1:]
    if body.endswith("|"):
        body = body[:-1]

    cells: list[str] = []
    in_code = False
    current = ""
    for character in body:
        if character == "`":
            in_code = not in_code
        if character == "|" and not in_code:
            cells.append(current)
            current = ""
        else:
            current += character
    cells.append(current)
    return cells


def is_separator(_line: str) -> bool:
    stripped = _line.strip()
    if not stripped.startswith("|"):
        return False
    return all(set(cell.strip()) <= set("-: ") and "-" in cell for cell in split_cells(stripped))


def check(_path: Path) -> list[str]:
    """One complaint per row that would lose cells when rendered."""
    lines = _path.read_text(encoding="utf-8", errors="replace").split("\n")
    complaints: list[str] = []
    width: int | None = None

    for number, line in enumerate(lines, 1):
        stripped = line.strip()

        if not stripped.startswith("|"):
            width = None  # the table has ended
            continue

        if is_separator(stripped):
            # The header is the line above; its width is the table's.
            width = len(split_cells(lines[number - 2])) if number >= 2 else None
            continue

        if width is None:
            continue  # the header itself, or a table with no separator

        found = len(split_cells(stripped))
        if found > width:
            complaints.append(
                f"{_path.relative_to(REPO)}:{number}: {found} cells but the header has {width}"
                f" -- the last {found - width} would not be rendered"
            )

    return complaints


def main(_argv: list[str]) -> int:
    if _argv:
        paths = [Path(argument) for argument in _argv]
    else:
        paths = sorted(
            path
            for path in REPO.rglob("*.md")
            if not any(part in SKIP for part in path.relative_to(REPO).parts)
        )

    complaints: list[str] = []
    for path in paths:
        complaints += check(path)

    print(f"markdown files   {len(paths)}")
    if complaints:
        print("FAIL  table rows whose last cells would be dropped when rendered:")
        for complaint in complaints:
            print(f"      {complaint}")
        return 1

    print("OK    every table row fits its header")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
