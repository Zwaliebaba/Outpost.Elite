#!/usr/bin/env python3
"""clang-tidy over GameLogic, on Linux, through the portable runner's shim.

WHY THIS EXISTS, and the finding it comes from. `/.clang-tidy` calls itself "the single source of
truth" for this repository's naming and semantics, and AGENTS.md section 1 points at it. Until slice
5d NOTHING RAN IT HERE. The file arrived whole from the sibling repository Outpost.Warzone, and its
status block described that tree's CI, that tree's projects (`NeuronServer`, `NeuronClient`), that
tree's tooling (`Build/CheckProjectFiles.py`) and that tree's reasons -- wire records, mesh
vertices, eighth-metre lattice steps -- none of which exist in this one.  `WarningsAsErrors: '*'`
was a setting with no gate behind it, and the first sweep found thirty-six diagnostics in
`GameLogic/` on a tree the file claimed had been "swept clean".

HOW IT RUNS WHERE THE COMPILER CANNOT. `GameLogic/pch.h` includes `NeuronCore.h`, which includes
`<WinSock2.h>`, so clang-tidy through the real precompiled header fails on Linux for the same reason
`Outpost/` does (Risk R15). The portable runner already solved that: `Tests/PortableRunner/Shim`
holds a `NeuronCore.h` and a `pch.h` that stand in for the Windows ones, and putting the shim first
on the include path is exactly what the runner does to compile the same sources with g++. So this
gate runs on the Linux leg, on every push, over the library that is the modernisation's subject --
and `Outpost/` stays the Windows job's, where its DirectX headers are.

Run it with no arguments to sweep `GameLogic/*.cpp`; pass paths to sweep those instead.
"""

import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PORT = REPO / "GameLogic"
SHIM = REPO / "Tests" / "PortableRunner" / "Shim"

# The runner's own arguments, less the ones that only matter to a link.
COMPILE_ARGS = ["-I", str(SHIM), "-I", str(PORT), "-I", str(REPO), "-std=c++20", "-x", "c++"]


def sources(_paths: list[str]) -> list[Path]:
    if _paths:
        return [Path(path) for path in _paths]
    # pch.cpp is the precompiled header's own translation unit and has no code of its own.
    return sorted(path for path in PORT.glob("*.cpp") if path.name != "pch.cpp")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", help="files to sweep (default: GameLogic/*.cpp)")
    parser.add_argument("--jobs", type=int, default=0, help="parallel clang-tidy processes (default: one per core)")
    args = parser.parse_args()

    tidy = shutil.which("clang-tidy")
    if tidy is None:
        print("skip  clang-tidy is not installed -- nothing was swept")
        return 0

    version = subprocess.run([tidy, "--version"], capture_output=True, text=True).stdout.strip().split("\n")
    print(f"tidy  {version[0] if version else tidy}")

    files = sources(args.paths)

    def sweep(_source: Path) -> list[str]:
        result = subprocess.run([tidy, "--quiet", str(_source), "--", *COMPILE_ARGS], cwd=REPO, capture_output=True, text=True)
        return [
            line.replace(str(REPO) + "/", "")
            for line in result.stdout.split("\n")
            if ": warning: " in line or ": error: " in line
        ]

    # ONE PROCESS PER FILE AND SEVERAL AT ONCE, because a serial sweep of GameLogic is half an hour
    # and `check_all.py` is meant to be run whole before every commit. clang-tidy re-parses the
    # whole translation unit for each file and the files are independent, so this is the one place
    # in the tool set where a pool earns its complexity.
    workers = args.jobs or min(os.cpu_count() or 1, len(files)) or 1
    findings: list[str] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for result in pool.map(sweep, files):
            findings.extend(result)

    print(f"files {len(files):>4} on {workers} worker(s)")
    if findings:
        print(f"\nFAIL  {len(findings)} diagnostic(s) in GameLogic/:")
        for line in dict.fromkeys(findings):
            print(f"      {line}")
        print("\n      /.clang-tidy is the authority. Fix the code, or exclude the check there WITH ITS REASON")
        print("      -- an exclusion whose reason is another repository's is what slice 5d was cleaning up.")
        return 1

    print("\nOK    clang-tidy finds nothing in GameLogic/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
