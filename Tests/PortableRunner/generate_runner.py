#!/usr/bin/env python3
"""Generate the translation units and the makefile that run the GameLogic suite without MSVC.

The suite is written against MSVC's CppUnitTest, which registers TEST_CLASS/TEST_METHOD with the
Visual Studio test platform through linker sections that only MSVC emits. Shim/CppUnitTest.h
turns those macros into a plain struct and a plain member function, which leaves one thing
missing: something has to call them. That is what this script writes.

It parses the test files for the two macros and emits, per test file, a translation unit that
#includes the file and calls every method in it. Per file rather than one amalgamated unit,
because each suite defines helpers of its own (OracleMissing, MakeCommander, and so on) in an
anonymous namespace, and merging them would collide.

Parsing rather than a registration trick, because a registration trick would need the test files
to change -- and the whole value of this runner is that they do not. The cost is that the parse
is a regex over line-anchored macros: a TEST_METHOD produced by a macro of our own, or one
commented out with a block comment, would be missed. Both are visible in a diff and neither
occurs; the alternative is a C++ parser.

Usage:
    generate_runner.py <build-dir> <repo-root> <test file>...
"""

import re
import sys
from pathlib import Path

# Line-anchored, because a mention of TEST_METHOD inside a comment or a string is not a test and
# the ones that exist are all declarations at the start of a line.
#
# BOTH allow leading whitespace, and `TEST_CLASS` did not until the tree moved to
# `NamespaceIndentation: All`: every suite is inside `namespace GameLogicTests`, so every
# `TEST_CLASS` gained two spaces and the generator found nothing at all. It said so rather than
# generating an empty runner, which is the one reason the change cost minutes instead of a
# green build with no tests in it.
CLASS_RE = re.compile(r"^\s*TEST_CLASS\((\w+)\)", re.M)
METHOD_RE = re.compile(r"^\s*TEST_METHOD\((\w+)\)", re.M)

# The namespace the suites declare. Every test file in Tests/GameLogicTests/ opens it, so the
# generated caller has to qualify through it; a file that used a different one would produce a
# runner that does not compile, which is the failure mode to prefer over a silent skip.
SUITE_NAMESPACE = "GameLogicTests"

# GameLogic is compiled whole. The three named test-project files are the oracle: the interpreter,
# the loader that finds the assembled game, and the golden-canvas reader. The rest of that
# directory is suites, which arrive through the generated units.
ORACLE_SOURCES = ["Cpu6502.cpp", "OracleImage.cpp", "GoldenCanvas.cpp"]

# The executable's own files that the suite covers, which is every one that does not call into
# Windows. `SaveStore` earned its place by being written, committed and left uncompiled for a day
# because the only machine that could build it was a Windows one -- and the shim beside this
# script turned out to need ten lines to make that untrue.
#
# `Presentation` and `KeyMap` are here by design rather than by accident: slice 2e's shell splits
# its decisions from its API calls precisely so that the decisions can be tested on a machine with
# no GPU and no Windows SDK. What is left in Window.cpp, CanvasPresenter.cpp and Main.cpp is
# Direct3D, a message pump and a thread, and those are verified by compiling.
EXECUTABLE_SOURCES = ["SaveStore.cpp", "Presentation.cpp", "KeyMap.cpp"]


def write_if_changed(_path: Path, _text: str) -> None:
    """Write only when the content differs, so that make sees an unchanged file as unchanged.

    Without this, regenerating before every run touches every generated unit and the incremental
    build is never incremental -- which defeats the reason this runner exists.
    """
    if _path.is_file() and _path.read_text(encoding="utf-8") == _text:
        return
    _path.write_text(_text, encoding="utf-8")


def parse_suites(_text: str) -> list[tuple[str, list[str]]]:
    """The (suite, methods) pairs in one test file, in source order.

    A TEST_CLASS's methods are the TEST_METHODs between it and the next one, so the classes are
    located first and the text sliced between them. A suite with no methods is dropped rather
    than emitted as an empty call block.
    """
    marks = [(m.start(), m.group(1)) for m in CLASS_RE.finditer(_text)]
    suites = []
    for index, (position, name) in enumerate(marks):
        end = marks[index + 1][0] if index + 1 < len(marks) else len(_text)
        methods = METHOD_RE.findall(_text[position:end])
        if methods:
            suites.append((name, methods))
    return suites


def write_unit(_out: Path, _source: Path, _suites: list[tuple[str, list[str]]]) -> tuple[str, int]:
    """One translation unit that runs one test file's suites. Returns its entry point and count.

    Each test is timed and each failure caught, so that one broken assertion reports one test
    instead of ending the run -- which is what the MSVC test platform does, and the property that
    makes a full-suite result comparable between the two runners.
    """
    entry = f"RunAll_{_source.stem}"
    lines = [
        f'#include "{_source}"',
        "",
        "#include <chrono>",
        "#include <cstdio>",
        "#include <cstdlib>",
        "#include <cstring>",
        "#include <exception>",
        "",
        f"void {entry}(const char* _filter, int& _pass, int& _fail, int& _skip)",
        "{",
    ]
    count = 0
    for suite, methods in _suites:
        for method in methods:
            count += 1
            name = f"{suite}.{method}"
            lines += [
                "  {",
                f'    static constexpr const char* NAME = "{name}";',
                "    if (_filter != nullptr && std::strstr(NAME, _filter) == nullptr) { ++_skip; }",
                "    else",
                "    {",
                "      const auto started = std::chrono::steady_clock::now();",
                "      try",
                "      {",
                f"        {SUITE_NAMESPACE}::{suite} test;",
                f"        test.{method}();",
                "        ++_pass;",
                "      }",
                "      catch (const std::exception& failure)",
                "      {",
                '        ++_fail;',
                '        std::printf("FAIL  %s: %s\\n", NAME, failure.what());',
                "      }",
                "      catch (...)",
                "      {",
                '        ++_fail;',
                '        std::printf("FAIL  %s: threw something that is not a std::exception\\n", NAME);',
                "      }",
                "      const double seconds =",
                "        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();",
                "      if (std::getenv(\"OUTPOST_TEST_TIMES\") != nullptr)",
                "      {",
                '        std::printf("TIME %8.2fs  %s\\n", seconds, NAME);',
                "      }",
                "    }",
                "  }",
            ]
    lines += ["}", ""]
    write_if_changed(_out / f"run_{_source.stem}.cpp", "\n".join(lines))
    return entry, count


def write_main(_out: Path, _entries: list[str], _total: int) -> None:
    """The runner's entry point: run every generated unit, then report.

    The exit code is what CI reads, so it is non-zero when anything failed AND when nothing ran
    -- a filter that matches no test, or a parse that found no suites, is a broken run rather
    than a passing one.
    """
    lines = [
        "// Generated by Tests/PortableRunner/generate_runner.py -- do not edit.",
        "#include <cstdio>",
        "#include <cstdlib>",
        "",
        f"static constexpr int EXPECTED_TESTS = {_total};",
        "",
    ]
    lines += [f"void {entry}(const char*, int&, int&, int&);" for entry in _entries]
    lines += [
        "",
        "int main(int _argc, char** _argv)",
        "{",
        "  // One optional argument: a substring of \"Suite.Method\". Everything else runs.",
        "  const char* filter = _argc > 1 ? _argv[1] : nullptr;",
        "  int pass = 0;",
        "  int fail = 0;",
        "  int skip = 0;",
        "",
    ]
    lines += [f"  {entry}(filter, pass, fail, skip);" for entry in _entries]
    lines += [
        "",
        '  std::printf("\\n%d passed, %d failed", pass, fail);',
        '  if (skip > 0) { std::printf(", %d not matching the filter", skip); }',
        '  std::printf("  (of %d)\\n", EXPECTED_TESTS);',
        "",
        "  if (pass + fail == 0)",
        "  {",
        '    std::printf("ERROR no test ran at all -- the filter matched nothing, or the parse found no suites\\n");',
        "    return 2;",
        "  }",
        "  return fail > 0 ? 1 : 0;",
        "}",
        "",
    ]
    write_if_changed(_out / "runner_main.cpp", "\n".join(lines))


def write_makefile(_build: Path, _repo: Path, _here: Path, _generated: list[Path]) -> None:
    """A makefile, so the build is parallel and incremental.

    Parallel matters in CI (roughly forty translation units, none of which share a precompiled
    header) and incremental matters locally, which is the entire point of this runner: an edit to
    one routine should cost one recompile, not forty. `-MMD -MP` is what makes the header
    dependencies real -- without it, editing a header rebuilds nothing and the runner reports the
    previous build's results, which is the worst failure a test harness can have.
    """
    exe = _build.parent / "PortableTests"
    objects = []
    rules = []

    def rule(_source: Path, _tag: str) -> None:
        # Objects are named by directory and stem, because GameLogic/ and Tests/GameLogicTests/
        # are free to hold the same file name and one of them would silently win.
        obj = _build / "obj" / f"{_tag}_{_source.stem}.o"
        objects.append(str(obj))
        rules.append(f"{obj}: {_source}\n\t@echo '  CXX  {_tag}/{_source.name}'\n\t@$(CXX) $(FLAGS) -c $< -o $@\n")

    for source in sorted(_generated):
        rule(source, "generated")
    for source in sorted((_repo / "GameLogic").glob("*.cpp")):
        rule(source, "GameLogic")
    for name in ORACLE_SOURCES:
        rule(_repo / "Tests" / "GameLogicTests" / name, "oracle")
    for name in EXECUTABLE_SOURCES:
        rule(_repo / "Outpost" / name, "Outpost")

    text = [
        "# Generated by Tests/PortableRunner/generate_runner.py -- do not edit.",
        "CXX ?= g++",
        "",
        "# -O1 rather than -O0: the suite runs exhaustive sweeps (65,536 coordinate pairs, 393,216",
        "# keystrokes) and unoptimised they take minutes. -O1 keeps the build fast and the run",
        "# short. It is not -O2 because the point is a fast edit-run loop, not a benchmark.",
        f"FLAGS := -std=c++20 -O1 -g1 -MMD -MP -Wall -Wextra -Wno-unused-parameter \\",
        f"         -I{_here / 'Shim'} -I{_repo / 'GameLogic'} -I{_repo / 'Tests' / 'GameLogicTests'} \\",
        f"         -I{_repo / 'Outpost'}",
        "",
        f"EXE := {exe}",
        "OBJECTS := \\",
    ]
    text += [f"  {obj} \\" for obj in objects[:-1]]
    text += [f"  {objects[-1]}", ""]
    text += [
        ".PHONY: all",
        "all: $(EXE)",
        "",
        "$(EXE): $(OBJECTS)",
        "\t@echo '  LINK $(notdir $@)'",
        "\t@$(CXX) $(OBJECTS) -o $@",
        "",
    ]
    text += rules
    text += ["-include $(OBJECTS:.o=.d)", ""]

    write_if_changed(_build / "Makefile", "\n".join(text))


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2

    build = Path(sys.argv[1]).resolve()
    repo = Path(sys.argv[2]).resolve()
    here = Path(__file__).resolve().parent

    generated_dir = build / "generated"
    generated_dir.mkdir(parents=True, exist_ok=True)
    (build / "obj").mkdir(parents=True, exist_ok=True)

    entries: list[str] = []
    units: list[Path] = []
    total = 0

    for argument in sys.argv[3:]:
        source = Path(argument).resolve()
        suites = parse_suites(source.read_text(encoding="utf-8", errors="replace"))
        if not suites:
            continue
        entry, count = write_unit(generated_dir, source, suites)
        entries.append(entry)
        units.append(generated_dir / f"run_{source.stem}.cpp")
        total += count

    if not entries:
        print("error: no TEST_CLASS with any TEST_METHOD was found in the files given")
        return 1

    write_main(generated_dir, entries, total)
    units.append(generated_dir / "runner_main.cpp")

    # Stale units from a previous run would still be compiled and linked, and would still call
    # tests that no longer exist. Anything not written this time goes.
    keep = {unit.name for unit in units}
    for existing in generated_dir.glob("*.cpp"):
        if existing.name not in keep:
            existing.unlink()

    write_makefile(build, repo, here, units)
    print(f"{len(entries)} test files, {total} tests")
    return 0


if __name__ == "__main__":
    sys.exit(main())
