#!/usr/bin/env python3
"""Check the Visual Studio project files against what is actually on disk.

Three ways a .vcxproj drifts from the tree, all of them met here:

  * a path that does not resolve. `ClCompile Include` is relative to the PROJECT, not to the
    repository, so a file added from outside the project's own directory is one `..` away from an
    MSBuild error that only a Windows runner sees. That is what this exists for: the mistake takes
    a second to make, four minutes of Windows CI to report, and this catches it in milliseconds on
    a machine with no MSBuild at all.
  * a source on disk that no project names. The portable runner GLOBS `GameLogic/*.cpp`, so a file
    someone forgot to add to the project compiles and passes there and is simply absent from the
    Windows build -- a green suite testing less than it says. Shaders count: an `.hlsl` the project
    does not name is not compiled by anything at all, and the first sign of it is a black window.
  * a .filters entry that disagrees with its .vcxproj. A filters entry for a file the project does
    not build is dead; a project entry with no filter puts the file loose at the root of the tree
    in the IDE.

Run from the repository root:

    python tools/check_projects.py
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MSBUILD = "{http://schemas.microsoft.com/developer/msbuild/2003}"
# The item types that name a FILE this repository owns. FxCompile and None are here for the
# shaders: `.hlsl` is compiled by FXC rather than by CL, and `.hlsli` is carried rather than
# compiled, so without these two a shader on disk that no project names would be invisible to
# check (2) below -- which is the exact failure this script exists to catch for `.cpp`.
ITEMS = ("ClCompile", "ClInclude", "FxCompile", "None")

# Each project, and the directory whose sources it is expected to name in full. A project may name
# files from elsewhere -- the test project builds Outpost/SaveStore.cpp so that it is covered --
# and those are checked for existence but not for completeness, because nothing says the whole of
# that directory belongs to this project.
PROJECTS = [
    ("GameLogic/GameLogic.vcxproj", "GameLogic"),
    ("Tests/GameLogicTests/GameLogicTests.vcxproj", "Tests/GameLogicTests"),
    ("Outpost/Outpost.vcxproj", "Outpost"),
    ("NeuronCore/NeuronCore.vcxproj", "NeuronCore"),
]


def listed(_project: Path) -> list[tuple[str, str]]:
    """The (item type, Include path) pairs a project or filters file names, in document order."""
    root = ElementTree.parse(_project).getroot()
    found: list[tuple[str, str]] = []
    for kind in ITEMS:
        for element in root.iter(f"{MSBUILD}{kind}"):
            include = element.get("Include")
            if include:
                found.append((kind, include))
    return found


def resolve(_project: Path, _include: str) -> Path:
    """An Include path as MSBuild resolves it: relative to the project's own directory."""
    return (_project.parent / _include.replace("\\", "/")).resolve()


def precompiled_header_first() -> list[str]:
    """Every .cpp in a project built with /Yu opens with `#include "pch.h"`.

    MSVC's precompiled headers are not an optimisation you can leave out: with `<PrecompiledHeader>
    Use</PrecompiledHeader>` the compiler DISCARDS everything before the `#include "pch.h"` line and
    fails with C1010 if the file has none.  g++ has no such rule, so a new source file without it
    compiles on the Ubuntu leg and breaks the Windows build -- which is what happened to
    `Tactics.cpp` on 2026-09-05, the third Windows-only compile error to reach CI through the
    permissive leg (plan section 6.116, section 6.123).

    The first line is what is checked and not merely the presence of the include, because that is
    what MSVC requires: an include above it is silently thrown away.
    """
    problems: list[str] = []
    for project, _owned in PROJECTS:
        text = (REPO / project).read_text(encoding="utf-8", errors="replace")
        if "<PrecompiledHeader>Use</PrecompiledHeader>" not in text:
            continue
        directory = (REPO / project).parent
        for source in sorted(directory.glob("*.cpp")):
            first = ""
            for line in source.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.strip():
                    first = line.strip()
                    break
            if first != '#include "pch.h"':
                problems.append(f"{source.relative_to(REPO)}: first line is {first!r}, and MSVC needs"
                                ' \'#include "pch.h"\' -- it discards everything above it (C1010)')
    return problems


def main() -> int:
    failures: list[str] = []
    checked = 0

    for relative, owned in PROJECTS:
        project = REPO / relative
        if not project.is_file():
            failures.append(f"{relative}: not found")
            continue

        entries = listed(project)
        checked += len(entries)

        # (1) every path resolves.
        on_disk: set[Path] = set()
        for kind, include in entries:
            path = resolve(project, include)
            if not path.is_file():
                failures.append(f"{relative}: <{kind} Include=\"{include}\"> does not resolve to a file"
                                f"\n      MSBuild reads it relative to {project.parent.relative_to(REPO)}/,"
                                f" which gives {path}")
                continue
            on_disk.add(path)

        # (2) every source in the project's own directory is named by it.
        directory = REPO / owned
        sources = (sorted(directory.glob("*.cpp")) + sorted(directory.glob("*.h"))
                   + sorted(directory.glob("*.hlsl")) + sorted(directory.glob("*.hlsli")))
        for source in sources:
            if source.resolve() not in on_disk:
                failures.append(f"{relative}: {source.relative_to(REPO)} is on disk and the project does"
                                " not name it, so MSVC will not build it")

        # (3) the filters file agrees with the project.
        filters = project.with_suffix(".vcxproj.filters")
        if filters.is_file():
            in_filters = {include for _, include in listed(filters)}
            in_project = {include for _, include in entries}
            for include in sorted(in_project - in_filters):
                failures.append(f"{relative}: \"{include}\" is in the project and not in the filters,"
                                " so it appears loose in the IDE")
            for include in sorted(in_filters - in_project):
                failures.append(f"{filters.name}: \"{include}\" is filtered and the project does not"
                                " build it")

    failures.extend(precompiled_header_first())

    print(f"projects          {len(PROJECTS)}")
    print(f"item entries      {checked}")
    for line in failures:
        print(f"  FAIL  {line}")
    if failures:
        print(f"FAIL  {len(failures)} project problem(s)")
        return 1
    print("OK    every project path resolves, every source is named, filters agree,")
    print("      and every precompiled-header source includes pch.h first")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
