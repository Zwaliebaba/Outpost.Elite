#!/usr/bin/env python3
"""Run every repository check, in the order CI runs them.

WHY THIS EXISTS. The checks are eight separate scripts and AGENTS.md lists them, and on
2026-09-05 a push went red because the list was retyped from memory with one entry missing --
`check_gamelogic.py` without `--self-test`, which is the one that catches a `GameLogic` file
declaring `far` or `near`. It had caught exactly that, and nobody ran it (plan section 6.127).

A list a person retypes is a list a person gets wrong, which is section 6.123's argument applied to
the checks themselves rather than to the code they check. Run this instead:

    python tools/check_all.py

It exits non-zero if any check does, and prints a one-line summary per check so a failure is
obvious in a scroll-back. It takes no arguments on purpose: a check you can skip is a check that
gets skipped.
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

# The order CI runs them in, so a local failure and a CI failure read the same way.
CHECKS: list[list[str]] = [
    ["inventory.py", "--check-includes"],
    ["check_gamelogic.py"],
    ["check_gamelogic.py", "--self-test"],
    ["check_projects.py"],
    ["check_outpost.py"],
    ["check_docs.py"],
    ["c64_source.py", "--check-all"],
    ["inventory.py", "--strict"],
    ["extract_tables.py", "--check"],
]


def main() -> int:
    failed: list[str] = []

    for check in CHECKS:
        name = " ".join(check)
        result = subprocess.run([sys.executable, str(REPO / "tools" / check[0]), *check[1:]],
                                cwd=REPO, capture_output=True, text=True)
        tail = [line for line in result.stdout.splitlines() if line.strip()]
        summary = tail[-1].strip() if tail else "(no output)"

        if result.returncode != 0:
            failed.append(name)
            print(f"FAIL  {name}")
            # The whole of a failing check's output, because the last line is rarely the reason.
            for line in result.stdout.splitlines():
                print(f"      {line}")
            for line in result.stderr.splitlines():
                print(f"      {line}")
        else:
            print(f"ok    {name:34s} {summary}")

    if failed:
        print(f"\nFAIL  {len(failed)} of {len(CHECKS)} checks: {', '.join(failed)}")
        return 1

    print(f"\nOK    all {len(CHECKS)} repository checks pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
