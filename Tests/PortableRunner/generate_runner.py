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

# GameLogic is compiled whole, and so is every test-project source that is not a suite: the
# interpreter, the loader that finds the assembled game, the golden-canvas reader, the universe
# image, and whatever the modernisation adds beside them. They used to be a hand-kept list here,
# which was a second place a non-suite source had to be named after the .vcxproj, with no check
# between the two: a file added to the project alone failed on this leg with a link error and
# nowhere else (Design/Modernize.md, the CI review of 2026-09-06). A glob has no second place.
# The suites arrive through the generated units, so they are excluded by their name.
def oracle_sources(_repo: Path) -> list[Path]:
    tests = _repo / "Tests" / "GameLogicTests"
    return sorted(path for path in tests.glob("*.cpp") if not path.name.endswith("Tests.cpp") and path.name != "pch.cpp")


# The executable's own files that the suite covers, which is every one that does not call into
# Windows. `SaveStore` earned its place by being written, committed and left uncompiled for a day
# because the only machine that could build it was a Windows one -- and the shim beside this
# script turned out to need ten lines to make that untrue.
#
# `Presentation` and `KeyMap` are here by design rather than by accident: slice 2e's shell splits
# its decisions from its API calls precisely so that the decisions can be tested on a machine with
# no GPU and no Windows SDK. What is left in Window.cpp, ScreenPresenter.cpp and Main.cpp is
# Direct3D, a message pump and a thread, and those are verified by compiling.
# `SidSynth` joined them 2026-09-06 (plan section 6.156). It is the 6581 emulation and it has no
# oracle -- the game never rendered audio, only wrote registers -- so the only thing that can catch
# a change in it is a hash of what it produces, and a hash is useless on a leg that cannot build it.
# Its header includes <array>, <cstddef> and <cstdint> and nothing else, so there was never a
# platform reason for it to be out.
EXECUTABLE_SOURCES = ["SaveStore.cpp", "Presentation.cpp", "KeyMap.cpp", "SidSynth.cpp", "SettingsFile.cpp"]


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
        "// Coverage (M6-0-f): defined in runner_main.cpp, and no-ops unless --coverage was given.",
        "void CoverageBegin();",
        "void CoverageEnd(const char* _test);",
        "void MeasureEnd(const char* _test);",
        "bool Excluded(const char* _test);",
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
                "    if (Excluded(NAME) || (_filter != nullptr && std::strstr(NAME, _filter) == nullptr)) { ++_skip; }",
                "    else",
                "    {",
                "      const auto started = std::chrono::steady_clock::now();",
                "      CoverageBegin();",
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
                "      CoverageEnd(NAME);",
        "      MeasureEnd(NAME);",
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
        '#include "Oracle.h"',
        "",
        "#include <cstddef>",
        '#include "OracleImage.h"',
        "",
        "#include <cstdio>",
        "#include <cstdlib>",
        "#include <cstring>",
        "#include <fstream>",
        "#include <memory>",
        "#include <set>",
        "#include <string>",
        "",
        f"static constexpr int EXPECTED_TESTS = {_total};",
        "",
        "/*",
        " * Coverage (M6-0-f): `--coverage FILE` before the filter writes one line per test -- the test's",
        " * name, a tab, the labels its oracle runs EXECUTED, a tab, and the labels its traps ANSWERED FOR.",
        " * `tools/inventory.py --coverage FILE` reads it against the ledger's Port rows. The three images",
        " * (the game, the loader, the sprites) are all asked, because each keeps its own marks.",
        " */",
        "static FILE* g_coverage = nullptr;",
        "",
        "void CoverageBegin()",
        "{",
        "  if (g_coverage != nullptr)",
        "  {",
        "    Elite::Testing::OracleImage::RecordCoverage(true);",
        "  }",
        "}",
        "",
        "/*",
        " * `--measure` (M6-a-2): the recorder's running totals, sampled per test so the corpus can be",
        " * read by suite as well as in aggregate. `MeasureEnd` prints one line per test; the whole-run",
        " * totals and the histogram are printed at the end of main.",
        " */",
        "Elite::Testing::RecordingOracle* g_recorder = nullptr;",
        "static unsigned long long g_lastCalls = 0;",
        "static unsigned long long g_lastBytes = 0;",
        "",
        "/*",
        " * A SKIP LIST, read from OUTPOST_TEST_SKIP -- one Suite.Method per line (M6-b-4).",
        " *",
        " * The filter is a single substring and cannot say \"everything except these\". R-f folds the",
        " * tests over two thousand oracle calls into digests, so their records never reach the",
        " * fixture, and the question R-h leaves open is what the REST carry. Measuring that exactly",
        " * means running the light tests alone: every distinct record they need, counted once,",
        " * whichever of them asks for it first. Attributing per-test deltas from one pass would",
        " * instead credit a shared record to whoever ran first, which is an estimate and not an",
        " * answer.",
        " */",
        "static std::set<std::string> g_skip;",
        "",
        "bool Excluded(const char* _test)",
        "{",
        "  return !g_skip.empty() && g_skip.count(std::string(_test)) != 0;",
        "}",
        "",
        "static void LoadSkipList()",
        "{",
        "  const char* path = std::getenv(\"OUTPOST_TEST_SKIP\");",
        "  if (path == nullptr)",
        "  {",
        "    return;",
        "  }",
        "  std::ifstream in(path);",
        "  if (!in)",
        "  {",
        "    std::printf(\"ERROR cannot read the skip list %s\\n\", path);",
        "    std::exit(2);",
        "  }",
        "  std::string line;",
        "  while (std::getline(in, line))",
        "  {",
        "    while (!line.empty() && (line.back() == '\\r' || line.back() == ' ')) { line.pop_back(); }",
        "    if (!line.empty()) { g_skip.insert(line); }",
        "  }",
        "  std::printf(\"skip list: %zu tests from %s\\n\", g_skip.size(), path);",
        "}",
        "",
        "void MeasureEnd(const char* _test)",
        "{",
        "  if (g_recorder == nullptr)",
        "  {",
        "    return;",
        "  }",
        "  const Elite::Testing::RecordingOracle::Totals& totals = g_recorder->Read();",
        "  const unsigned long long calls = static_cast<unsigned long long>(totals.calls) - g_lastCalls;",
        "  const unsigned long long bytes = static_cast<unsigned long long>(totals.bytes) - g_lastBytes;",
        "  g_lastCalls = static_cast<unsigned long long>(totals.calls);",
        "  g_lastBytes = static_cast<unsigned long long>(totals.bytes);",
        "  if (calls != 0ull)",
        "  {",
        '    std::printf("MEASURE\\t%s\\t%llu\\t%llu\\n", _test, calls, bytes);',
        "  }",
        "}",
        "",
        "void CoverageEnd(const char* _test)",
        "{",
        "  if (g_coverage == nullptr)",
        "  {",
        "    return;",
        "  }",
        "  Elite::Testing::OracleImage::RecordCoverage(false);",
        "  std::fprintf(g_coverage, \"%s\\t\", _test);",
        "  const Elite::Testing::OracleImage* images[] = {&Elite::Testing::OracleImage::Instance(),",
        "                                                &Elite::Testing::OracleImage::LoaderInstance(),",
        "                                                &Elite::Testing::OracleImage::SpriteInstance()};",
        "  Elite::Testing::OracleImage::Coverage all;",
        "  for (const Elite::Testing::OracleImage* image : images)",
        "  {",
        "    const Elite::Testing::OracleImage::Coverage taken = image->TakeCoverage();",
        "    all.executed.insert(all.executed.end(), taken.executed.begin(), taken.executed.end());",
        "    all.trapped.insert(all.trapped.end(), taken.trapped.begin(), taken.trapped.end());",
        "  }",
        "  for (const std::string& label : all.executed)",
        "  {",
        "    std::fprintf(g_coverage, \"%s \", label.c_str());",
        "  }",
        "  std::fputc('\\t', g_coverage);",
        "  for (const std::string& label : all.trapped)",
        "  {",
        "    std::fprintf(g_coverage, \"%s \", label.c_str());",
        "  }",
        "  std::fputc('\\n', g_coverage);",
        "  std::fflush(g_coverage);",
        "}",
        "",
    ]
    lines += [f"void {entry}(const char*, int&, int&, int&);" for entry in _entries]
    lines += [
        "",
        "int main(int _argc, char** _argv)",
        "{",
        "  LoadSkipList();",
        "  // `--coverage FILE` or `--measure` first if wanted, then one optional substring of",
        "  // \"Suite.Method\".",
        "  int next = 1;",
        "  if (_argc > 2 && std::strcmp(_argv[1], \"--coverage\") == 0)",
        "  {",
        "    g_coverage = std::fopen(_argv[2], \"w\");",
        "    if (g_coverage == nullptr)",
        "    {",
        "      std::printf(\"ERROR cannot write the coverage file %s\\n\", _argv[2]);",
        "      return 2;",
        "    }",
        "    next = 3;",
        "  }",
        "",
        "  /*",
        "   * `--measure` (Design/Modernize.md M6-a-2): run the whole suite through the recorder in",
        "   * its counting mode and report what a fixture of every call would cost. It is the",
        "   * measurement section 4.10's record-size threshold has to be chosen from, and it keeps",
        "   * nothing, so it costs one pass and no memory.",
        "   */",
        "  std::unique_ptr<Elite::Testing::RecordingOracle> recorder;",
        "  const char* fixture = nullptr;",
        "  if (_argc > next && std::strcmp(_argv[next], \"--measure\") == 0)",
        "  {",
        "    recorder = std::make_unique<Elite::Testing::RecordingOracle>(Elite::Testing::RecordingOracle::Mode::Measure);",
        "    Elite::Testing::Oracle::Install(recorder.get());",
        "    // M6-b-1: the read census rides along with the measuring pass, because both want one",
        "    // whole run and neither keeps anything. It is off in every other run.",
        "    Elite::Testing::Cpu6502::ReadCensus::Instance().on = true;",
        "    g_recorder = recorder.get();",
        "    ++next;",
        "  }",
        "  else if (_argc > next + 1 && std::strcmp(_argv[next], \"--record\") == 0)",
        "  {",
        "    fixture = _argv[next + 1];",
        "    recorder = std::make_unique<Elite::Testing::RecordingOracle>(Elite::Testing::RecordingOracle::Mode::Keep);",
        "    Elite::Testing::Oracle::Install(recorder.get());",
        "    next += 2;",
        "  }",
        "",
        "  const char* filter = _argc > next ? _argv[next] : nullptr;",
        "  int pass = 0;",
        "  int fail = 0;",
        "  int skip = 0;",
        "",
    ]
    lines += [f"  {entry}(filter, pass, fail, skip);" for entry in _entries]
    lines += [
        "",
        "  if (recorder != nullptr)",
        "  {",
        "    Elite::Testing::Oracle::Install(nullptr);",
        "    if (fixture != nullptr)",
        "    {",
        "      std::string trouble;",
        "      if (!Elite::Testing::WriteFixture(*recorder, fixture, trouble))",
        "      {",
        '        std::printf("ERROR %s\\n", trouble.c_str());',
        "        return 2;",
        "      }",
        '      std::printf("\\nfixture: %s\\n", fixture);',
        "    }",
        "    const Elite::Testing::RecordingOracle::Totals& totals = recorder->Read();",
        '    std::printf("\\nrecorder: %llu calls, %llu distinct, %llu bytes, largest %llu, %llu memory writes, %llu collisions\\n",',
        "                static_cast<unsigned long long>(totals.calls), static_cast<unsigned long long>(totals.distinct),",
        "                static_cast<unsigned long long>(totals.bytes), static_cast<unsigned long long>(totals.largest),",
        "                static_cast<unsigned long long>(totals.memoryWrites), static_cast<unsigned long long>(totals.collisions));",
        "    unsigned long long edge = Elite::Testing::RecordingOracle::SMALLEST_BUCKET;",
        "    for (std::size_t bucket = 0; bucket < Elite::Testing::RecordingOracle::BUCKETS; ++bucket)",
        "    {",
        '      std::printf("HISTOGRAM\\t<=%llu\\t%llu\\t%llu\\n", edge,',
        "                  static_cast<unsigned long long>(totals.bucketCalls[bucket]),",
        "                  static_cast<unsigned long long>(totals.bucketBytes[bucket]));",
        "      edge *= 2ull;",
        "    }",
        "",
        "    /*",
        "     * M6-b-1: how much of what the oracle answered came out of the assembled original",
        "     * rather than out of what the test wrote (Design/Modernize.md §1 R-g).",
        "     *",
        "     * The page map is the actionable half. A read of a byte nothing wrote is only a",
        "     * problem where those bytes are the original\'s DATA -- a table, a blueprint, a string",
        "     * -- so the owner needs to see which 256-byte pages the answers lean on, not just how",
        "     * many bytes there are.",
        "     */",
        "    const Elite::Testing::Cpu6502::ReadCensus& census = Elite::Testing::Cpu6502::ReadCensus::Instance();",
        '    std::printf("\\ncensus: %llu data reads, %llu of them off the image, %llu distinct addresses\\n",',
        "                static_cast<unsigned long long>(totals.reads), static_cast<unsigned long long>(totals.imageReads),",
        "                static_cast<unsigned long long>(census.addresses.count()));",
        '    std::printf("census: %llu calls read the image, %llu answered from their inputs alone\\n",',
        "                static_cast<unsigned long long>(totals.callsOnImage), static_cast<unsigned long long>(totals.callsPure));",
        '    std::printf("carry: %llu of %llu record bytes are verbatim image content, in %llu runs, longest %llu\\n",',
        "                static_cast<unsigned long long>(totals.carryBytes), static_cast<unsigned long long>(totals.recordBytes),",
        "                static_cast<unsigned long long>(totals.carryRuns), static_cast<unsigned long long>(totals.longestCarry));",
        '    std::printf("census: %llu of the image reads found a byte that is not zero, over %llu addresses, in %llu calls\\n",',
        "                static_cast<unsigned long long>(totals.imageContentReads),",
        "                static_cast<unsigned long long>(census.contentAddresses.count()),",
        "                static_cast<unsigned long long>(totals.callsOnContent));",
        "    for (unsigned page = 0; page < 256u; ++page)",
        "    {",
        "      unsigned long long held = 0;",
        "      unsigned long long content = 0;",
        "      for (unsigned within = 0; within < 256u; ++within)",
        "      {",
        "        held += census.addresses.test((page << 8) | within) ? 1ull : 0ull;",
        "        content += census.contentAddresses.test((page << 8) | within) ? 1ull : 0ull;",
        "      }",
        "      if (held != 0ull)",
        "      {",
        '        std::printf("PAGE\\t%02X\\t%llu\\t%llu\\n", page, held, content);',
        "      }",
        "    }",
        "  }",
        '  std::printf("\\n%d passed, %d failed", pass, fail);',
        '  if (skip > 0) { std::printf(", %d not matching the filter", skip); }',
        '  std::printf("  (of %d)\\n", EXPECTED_TESTS);',
        "",
        "  if (g_coverage != nullptr)",
        "  {",
        "    std::fclose(g_coverage);",
        "  }",
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
    for source in oracle_sources(_repo):
        rule(source, "oracle")
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
