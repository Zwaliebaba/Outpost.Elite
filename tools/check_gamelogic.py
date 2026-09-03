#!/usr/bin/env python3
"""Guard the rules that make GameLogic testable (AGENTS.md section 5, ADR-002, ADR-005).

GameLogic must be deterministic and platform-free: no wall clock, no operating-system
randomness, no file or registry access, no Win32 calls, and no float or double anywhere. Those
rules are what let the oracle suites and the replay hashes mean anything, and every one of them
is the sort of thing that gets added in a hurry and noticed months later when a replay stops
reproducing on one machine.

So this reads the source rather than trusting anyone to remember.

    python tools/check_gamelogic.py              # scan GameLogic/
    python tools/check_gamelogic.py --self-test  # prove the scanner still detects what it claims

Comments and string literals are stripped before scanning. Without that, a comment explaining
why floats are banned would itself trip the float rule, and the honest response to a checker
that cries wolf is that people stop reading it.

Note what is deliberately NOT banned: including NeuronCore.h through pch.h. That header reaches
Windows headers, and GameLogic includes it for the assert family. The rule is about CALLS.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TARGET = REPO / "GameLogic"

# Headers that bring in exactly what this library must not have.
BANNED_INCLUDES = {
    "chrono": "the wall clock",
    "random": "operating-system randomness",
    "ctime": "the wall clock",
    "time.h": "the wall clock",
    "fstream": "file access",
    "filesystem": "file access",
    "iostream": "stream IO",
    "thread": "threading",
    "mutex": "threading",
}

# Identifiers that betray the same things even without the header, plus the float ban.
# The Win32 list is representative rather than exhaustive: it is a backstop for the rule in
# AGENTS.md section 5, not a substitute for it.
BANNED_IDENTIFIERS = {
    "float": "floating point is banned in GameLogic (ADR-002)",
    "double": "floating point is banned in GameLogic (ADR-002)",
    "rand": "operating-system randomness",
    "srand": "operating-system randomness",
    "clock": "the wall clock",
    "time": "the wall clock",
    "fopen": "file access",
    "fread": "file access",
    "fwrite": "file access",
    "getenv": "environment access",
    "printf": "stream IO",
    "QueryPerformanceCounter": "the wall clock",
    "GetTickCount": "the wall clock",
    "GetTickCount64": "the wall clock",
    "timeGetTime": "the wall clock",
    "GetSystemTime": "the wall clock",
    "GetLocalTime": "the wall clock",
    "CreateFileW": "file access",
    "CreateFileA": "file access",
    "ReadFile": "file access",
    "WriteFile": "file access",
    "RegOpenKeyExW": "registry access",
    "MessageBoxW": "a Win32 call",
    "OutputDebugStringA": "a Win32 call",
    "OutputDebugStringW": "a Win32 call",
}

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def strip_comments_and_strings(_text: str) -> str:
    """Blank out comments and string/char literals, preserving line structure."""
    out = []
    index = 0
    length = len(_text)

    while index < length:
        char = _text[index]
        nxt = _text[index + 1] if index + 1 < length else ""

        if char == "/" and nxt == "/":
            while index < length and _text[index] != "\n":
                index += 1
            continue

        if char == "/" and nxt == "*":
            index += 2
            while index < length - 1 and not (_text[index] == "*" and _text[index + 1] == "/"):
                if _text[index] == "\n":
                    out.append("\n")
                index += 1
            index = min(index + 2, length)
            continue

        if char in ('"', "'"):
            quote = char
            index += 1
            while index < length and _text[index] != quote:
                if _text[index] == "\\":
                    index += 1
                if index < length and _text[index] == "\n":
                    out.append("\n")
                index += 1
            index += 1
            out.append('""')
            continue

        out.append(char)
        index += 1

    return "".join(out)


def scan_text(_name: str, _text: str) -> list[str]:
    """Findings for one file's contents. Empty means clean."""
    findings: list[str] = []

    for header in INCLUDE_RE.findall(_text):
        stem = header.strip().lower()
        if stem in BANNED_INCLUDES:
            findings.append(f"{_name}: includes <{header}> -- {BANNED_INCLUDES[stem]}")

    code = strip_comments_and_strings(_text)

    for identifier, why in BANNED_IDENTIFIERS.items():
        for match in re.finditer(r"\b" + re.escape(identifier) + r"\b", code):
            line = code.count("\n", 0, match.start()) + 1
            findings.append(f"{_name}:{line}: names '{identifier}' -- {why}")

    return findings


def self_test() -> int:
    """Prove the scanner detects what it claims, without touching the tree."""
    bad_cases = [
        ("clock", "#include <chrono>\nvoid F() { }\n"),
        ("float", "void F() { float x = 1; (void)x; }\n"),
        ("double", "double G() { return 0; }\n"),
        ("randomness", "void F() { int v = rand(); (void)v; }\n"),
        ("win32", "void F() { QueryPerformanceCounter(nullptr); }\n"),
        ("file access", "#include <fstream>\n"),
    ]
    good_cases = [
        ("a comment mentioning float", "// float and double are banned here\nvoid F() { }\n"),
        ("a string mentioning rand", 'const char* K = "rand";\n'),
        ("ordinary integer code", "#include <cstdint>\nstd::uint8_t F() { return 1; }\n"),
        ("a name merely containing a banned word", "void FloatingHelper() { int timestamp = 0; (void)timestamp; }\n"),
    ]

    failures = 0

    for label, text in bad_cases:
        if not scan_text("synthetic", text):
            print(f"SELF-TEST FAIL  the scanner missed {label}")
            failures += 1

    for label, text in good_cases:
        found = scan_text("synthetic", text)
        if found:
            print(f"SELF-TEST FAIL  the scanner cried wolf on {label}: {found[0]}")
            failures += 1

    if failures:
        print(f"FAIL  {failures} self-test case(s) wrong")
        return 1

    print(f"OK    self-test passed: {len(bad_cases)} detected, {len(good_cases)} correctly ignored")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true", help="check the scanner itself, then exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if not TARGET.is_dir():
        print(f"error: {TARGET} not found -- run from the repository root")
        return 1

    sources = sorted(list(TARGET.glob("*.h")) + list(TARGET.glob("*.cpp")))
    if not sources:
        print(f"error: no sources under {TARGET}")
        return 1

    findings: list[str] = []
    for source in sources:
        findings.extend(scan_text(source.name, source.read_text(encoding="utf-8", errors="replace")))

    print(f"scanned   {len(sources)} file(s) in {TARGET.name}/")

    if findings:
        print(f"findings  {len(findings)}\n")
        for finding in findings:
            print(f"  {finding}")
        print("\nFAIL  GameLogic must stay deterministic and platform-free (AGENTS.md section 5)")
        return 1

    print("OK        no clock, no randomness, no float, no file or registry access, no Win32 call")
    return 0


if __name__ == "__main__":
    sys.exit(main())
