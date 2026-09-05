#!/usr/bin/env python3
"""Run the recorded mutants against the suite, so a mutation tally is a command and not a claim.

WHY THIS EXISTS -- Risk R13. Nearly every slice in the plan reports a tally: "28 of 28 caught",
"97 mutations, 97 caught", "79 mutations, 59 caught". Those numbers carry more weight than any
other single figure in the corpus, because they are what turns "the suite is green" into "the suite
would notice". And not one of them could be re-run. `AGENTS.md` section 6 records the METHOD in
full; nothing recorded WHICH mutants were run, because they were hand edits made in a scratch
worktree and thrown away. So a tally was an assertion nobody could check -- not a reviewer, not a
later session, not the person who wrote it -- and section 6.119 is the demonstration that one can
be confidently wrong: three published tallies came from a harness reading the wrong line.

The mutants live in `tools/mutants.json`, one list per unit. This applies them one at a time and
says what happened.

    python tools/mutate.py --list                 what is recorded, and for which slice
    python tools/mutate.py --unit tactics         run one unit's mutants
    python tools/mutate.py --id ta-253            run one mutant
    python tools/mutate.py                        run everything (slow: builds once per mutant)

FIVE THINGS IN HERE ARE SCAR TISSUE, and each one is a way a mutation run has already lied.

1. THE BASELINE IS PROVEN BEFORE ANY MUTANT IS BELIEVED. A worktree whose `Upstream/` submodule was
   empty made every oracle test skip and `OracleIsPresent` fail by design; the harness read only
   the suite's last line, saw one failure on every run, and reported every mutant as caught
   (section 6.119). So the unmutated tree is run FIRST and must come back with zero failures. If it
   does not, this stops and says so rather than producing a number.

2. A TIMEOUT IS A CATCH. Turning `count - 1` into `count - 2` in a loop that stops at zero makes an
   odd count run for ever: the suite times out, no summary line is printed, and a harness looking
   for one calls it a tooling failure. A timeout is the strongest possible catch and is recorded as
   one.

3. A MUTANT THAT DOES NOT APPLY IS AN ERROR, NOT A CATCH. Every `find` must match its file exactly
   once. A `find` that matches nothing -- because the code was refactored underneath it -- would
   otherwise run the unmutated suite, see it pass, and report a survivor; or worse, run a DIFFERENT
   mutant's edit and report on that. Both directions are refused loudly.

4. THE WORKTREE HAS NO SYMLINKS IN IT. `AGENTS.md` section 6's recipe symlinks the submodule and the
   reference files into a detached worktree, and warns that every `git checkout -f` eats the
   symlink and it must be re-made. This copies instead: `Design/Reference/*.txt` is a handful of
   text files and the oracle's whole `versions/c64` tree is 4.4 MB. Copying removes the trap by
   construction rather than documenting it, which is what section 6.119 asked for.

5. A UNIT'S TEST FILTER IS VERIFIED BEFORE IT IS TRUSTED. A filter that selects the wrong tests is
   worse than no filter: it produces a confident number about code it never ran. The first real
   run had `hyperspace` filtered on "Hyperspace", the jump's tests live in a class called
   `TheJump`, and the filter matched three tests in two other files whose method names happen to
   contain the word -- so two mutants the suite certainly catches were reported as survivors. The
   number of tests a unit's filters select is now data, checked against the unmutated build.

WHAT A FAILURE MEANS. A run fails when a mutant's outcome differs from the `expect` recorded beside
it, in either direction. A new survivor is a gap in the tests. A recorded survivor that starts
being caught is debt that has been paid, and the file should say so -- an unrecorded improvement is
still a document disagreeing with the tree.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MUTANTS = REPO / "tools" / "mutants.json"

# Long enough that a slow leg is not called a catch, short enough that a mutant which made a loop
# infinite does not hold the run for ever. A timeout IS a catch, so erring long is the safe side.
BUILD_TIMEOUT = 30 * 60
TEST_TIMEOUT = 20 * 60

CAUGHT, SURVIVES, EQUIVALENT, UNCOMPILABLE = "caught", "survives", "equivalent", "uncompilable"


@dataclass
class Mutant:
    unit: str
    filter: list[str]
    tests: int
    id: str
    file: str
    find: str
    replace: str
    expect: str
    note: str
    selftest: bool


@dataclass
class Outcome:
    result: str
    detail: str
    seconds: float


# ---- the recorded list ---------------------------------------------------------------------------


def load_units() -> list[dict]:
    data = json.loads(MUTANTS.read_text(encoding="utf-8"))
    return data["units"]


def select(_units: list[dict], _unit: str | None, _ident: str | None) -> list[Mutant]:
    chosen: list[Mutant] = []
    for unit in _units:
        if _unit and unit["name"] != _unit:
            continue
        for mutant in unit["mutants"]:
            # A selftest mutant is never filtered out: --id runs the one asked for AND the proof
            # that a catch would have been seen, because one answer is worth nothing without it.
            if _ident and mutant["id"] != _ident and not mutant.get("selftest", False):
                continue
            chosen.append(
                Mutant(
                    unit=unit["name"],
                    filter=list(unit["filter"]),
                    tests=unit["tests"],
                    id=mutant["id"],
                    file=mutant["file"],
                    find=mutant["find"],
                    replace=mutant["replace"],
                    expect=mutant.get("expect", CAUGHT),
                    note=mutant.get("note", ""),
                    selftest=bool(mutant.get("selftest", False)),
                )
            )
    # Selftests first, so a harness that cannot observe a catch says so before spending twenty
    # minutes producing numbers.
    return sorted(chosen, key=lambda mutant: not mutant.selftest)


# ---- the worktree --------------------------------------------------------------------------------

# What the oracle opens, and nothing else: the label map and block manifest, plus the assembled
# output the manifest points into. Tests/GameLogicTests/OracleImage.cpp is the authority on this
# list, and it is short enough to copy.
ORACLE_FILES = [
    "Design/Reference/Labels.txt",
    "Design/Reference/Binaries.txt",
    "Design/Reference/LoaderLabels.txt",
    "Design/Reference/LoaderBinaries.txt",
]
# The whole C64 version directory, 4.4 MB, and not just `3-assembled-output`. The manifest's own
# rows reach sideways out of it -- `../1-source-files/images/C.CODIALS.bin` is the dashboard image
# -- so copying only the assembled blocks produced a worktree where `OracleIsPresent` failed and
# every oracle test skipped, which is §6.119's exact scenario arriving from a new direction. The
# baseline gate refused it, which is what the gate is for. Copying the directory whole means a
# later oracle that reads one more file does not silently start skipping.
ORACLE_DIR = "Upstream/elite-source-code-library/versions/c64"


def make_worktree(_at: Path) -> None:
    """A detached worktree at HEAD with the oracle copied in -- no symlinks anywhere."""
    if _at.exists():
        subprocess.run(["git", "worktree", "remove", "--force", str(_at)], cwd=REPO, capture_output=True, text=True)
        shutil.rmtree(_at, ignore_errors=True)

    _at.parent.mkdir(parents=True, exist_ok=True)
    made = subprocess.run(["git", "worktree", "add", "--detach", str(_at), "HEAD"], cwd=REPO, capture_output=True, text=True)
    if made.returncode != 0:
        sys.exit(f"error: could not create the worktree\n{made.stdout}{made.stderr}")

    for relative in ORACLE_FILES:
        source = REPO / relative
        if source.is_file():
            target = _at / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)

    source_dir = REPO / ORACLE_DIR
    if source_dir.is_dir():
        shutil.copytree(source_dir, _at / ORACLE_DIR, dirs_exist_ok=True)


def remove_worktree(_at: Path) -> None:
    subprocess.run(["git", "worktree", "remove", "--force", str(_at)], cwd=REPO, capture_output=True, text=True)
    shutil.rmtree(_at, ignore_errors=True)


def check_oracle_present() -> None:
    """Say up front what section 6.119 found out the expensive way."""
    missing = [name for name in ORACLE_FILES[:2] if not (REPO / name).is_file()]
    if missing or not (REPO / ORACLE_DIR).is_dir():
        sys.exit(
            "error: the oracle is not assembled in THIS tree, so every oracle test would skip and\n"
            "       every mutant would look caught (Risk R9, plan section 6.119).\n"
            "       Run: python tools/labels.py --assemble"
        )


# ---- the runners ---------------------------------------------------------------------------------


class PortableRunner:
    """Tests/PortableRunner -- g++ and make. The fast leg, and the one CI uses."""

    name = "portable"

    def __init__(self, _root: Path):
        self.root = _root

    @staticmethod
    def available() -> bool:
        return shutil.which("g++") is not None and shutil.which("make") is not None

    def run(self, _filter: str | None) -> tuple[str, str, int]:
        """(result, detail, tests-that-ran); result is pass/fail/timeout/build-error/no-summary."""
        command = ["bash", str(self.root / "Tests" / "PortableRunner" / "run_tests.sh")]
        if _filter:
            command.append(_filter)

        environment = dict(os.environ)
        environment["OUTPOST_PORTABLE_OUT"] = str(self.root / "x64" / "Debug")

        try:
            done = subprocess.run(command, cwd=self.root, capture_output=True, text=True,
                                  timeout=BUILD_TIMEOUT + TEST_TIMEOUT, env=environment)
        except subprocess.TimeoutExpired:
            return "timeout", "the run did not finish", 0

        return classify_portable(done.stdout + done.stderr, done.returncode)


def classify_portable(_output: str, _code: int) -> tuple[str, str, int]:
    """Read the run's own summary rather than its exit code.

    The exit code cannot tell a compile error from a failing test, and both matter here: one is a
    mutant the compiler refused and one is a mutant the suite caught. The summary line is what
    distinguishes them, and its ABSENCE is itself information.
    """
    summary = [line for line in _output.splitlines() if " passed, " in line and " failed" in line]
    if not summary:
        if "error:" in _output or "Error " in _output:
            return "build-error", first_error(_output), 0
        return "no-summary", "the runner printed no summary line", 0

    line = summary[-1].strip()
    try:
        passed = int(line.split(" passed")[0].split()[-1])
        failed = int(line.split(" passed, ")[1].split(" failed")[0])
    except (IndexError, ValueError):
        return "no-summary", line, 0

    if passed == 0 and failed == 0:
        return "no-summary", f"nothing ran: {line}", 0
    return ("fail" if failed else "pass"), line, passed + failed


def first_error(_output: str) -> str:
    for line in _output.splitlines():
        if "error" in line.lower():
            return line.strip()[:160]
    return "build failed"


class MsBuildRunner:
    """MSVC -- the authority (ADR-004 section 1), and the only runner on a machine with no g++."""

    name = "msbuild"

    def __init__(self, _root: Path, _msbuild: str, _vstest: str):
        self.root = _root
        self.msbuild = _msbuild
        self.vstest = _vstest

    @staticmethod
    def find() -> tuple[str, str] | None:
        roots = [
            Path(os.environ.get("ProgramFiles", r"C:\Program Files")),
            Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")),
        ]
        for root in roots:
            base = root / "Microsoft Visual Studio"
            if not base.is_dir():
                continue
            for edition in sorted(base.rglob("MSBuild/Current/Bin/MSBuild.exe")):
                vstest = edition.parents[3] / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "TestWindow" / "vstest.console.exe"
                if vstest.is_file():
                    return str(edition), str(vstest)
        return None

    def run(self, _filter: str | None) -> tuple[str, str, int]:
        project = self.root / "Tests" / "GameLogicTests" / "GameLogicTests.vcxproj"
        build = [
            self.msbuild, str(project), "/p:Configuration=Release", "/p:Platform=x64",
            "/m", "/nologo", "/v:minimal",
        ]
        try:
            built = subprocess.run(build, cwd=self.root, capture_output=True, text=True, timeout=BUILD_TIMEOUT)
        except subprocess.TimeoutExpired:
            return "timeout", "the build did not finish", 0

        if built.returncode != 0:
            return "build-error", first_error(built.stdout + built.stderr), 0

        # Building the .vcxproj on its own does NOT put the DLL where building the solution does:
        # MSBuild honours the project's own OutDir, so it lands under Tests/GameLogicTests/x64/
        # rather than at the repository root's x64/. Handing vstest a path that is not there costs
        # a run that prints no counts and reads exactly like a mutant nothing caught, which is the
        # shape of failure this tool exists to refuse -- so the DLL is FOUND rather than assumed.
        candidates = sorted(self.root.rglob("GameLogicTests.dll"))
        if not candidates:
            return "build-error", "the build reported success and produced no GameLogicTests.dll", 0
        dll = max(candidates, key=lambda path: path.stat().st_mtime)

        test = [self.vstest, str(dll), "/Platform:x64"]
        if _filter:
            test.append(f"/TestCaseFilter:FullyQualifiedName~{_filter}")

        try:
            ran = subprocess.run(test, cwd=self.root, capture_output=True, text=True, timeout=TEST_TIMEOUT)
        except subprocess.TimeoutExpired:
            return "timeout", "the tests did not finish", 0

        return classify_vstest(ran.stdout + ran.stderr)


def classify_vstest(_output: str) -> tuple[str, str, int]:
    """vstest prints 'Passed! ... Total tests: N' or 'Failed! ... Failed: M'."""
    total = passed = failed = None
    for line in _output.splitlines():
        stripped = line.strip()
        for label, setter in (("Total tests:", "total"), ("Passed:", "passed"), ("Failed:", "failed")):
            if stripped.startswith(label):
                try:
                    value = int(stripped.split(":", 1)[1].split()[0])
                except (IndexError, ValueError):
                    continue
                if setter == "total":
                    total = value
                elif setter == "passed":
                    passed = value
                else:
                    failed = value

    if total is None and passed is None and failed is None:
        # Say WHAT it printed instead. "no counts" on its own sends a reader to the wrong place --
        # it looks like a flaky runner and is usually a path, a filter or a missing dependency.
        noise = [line.strip() for line in _output.splitlines() if line.strip()]
        return "no-summary", "vstest printed no counts: " + (noise[-1][:160] if noise else "no output at all"), 0
    if (total or 0) == 0 and (passed or 0) == 0:
        return "no-summary", "no test matched the filter", 0

    line = f"{passed or 0} passed, {failed or 0} failed (of {total or 0})"
    return ("fail" if (failed or 0) else "pass"), line, (passed or 0) + (failed or 0)


def run_filters(_runner, _filters: list[str]) -> tuple[str, str, int]:
    """Run every filter a unit names and combine the answers.

    A unit needs MORE THAN ONE filter whenever its tests live in several `TEST_CLASS`es, which is
    normal: `TacticsTests.cpp` holds `TheTacticsVectors`, `TheShipAi` and `TheDockingComputer` and
    no substring reaches all three. One filter per class, and any failure among them is a catch.

    The build happens on the first call and the rest are test runs of a second or two, so the extra
    filters cost almost nothing next to the compile.
    """
    worst, details, ran = "pass", [], 0
    order = {"build-error": 4, "no-summary": 3, "timeout": 2, "fail": 1, "pass": 0}

    for name in _filters or [None]:
        result, detail, count = _runner.run(name)
        ran += count
        details.append(detail)
        if order[result] > order[worst]:
            worst = result

    return worst, "; ".join(details), ran


def check_filters(_runner, _chosen: list[Mutant]) -> list[str]:
    """Every unit's filters select exactly the number of tests the unit says they do.

    THIS GATE EXISTS BECAUSE THE FIRST REAL RUN NEEDED IT. `hyperspace` was recorded with the filter
    "Hyperspace" and the tests for the jump live in a class called `TheJump`; the filter matched
    three tests in `ChartTests` and `LaunchTests` that have "Hyperspace" in their method names and
    touch none of the mutated code. Two mutants that the suite certainly catches were reported as
    survivors, with nothing anywhere saying why.

    A wrong filter is worse than a missing one: it produces a confident number about code it never
    ran. So the count the filters select is DATA in `mutants.json` and is verified against the
    unmutated build before any mutant is applied.
    """
    complaints: list[str] = []
    seen: set[str] = set()

    for mutant in _chosen:
        if mutant.unit in seen:
            continue
        seen.add(mutant.unit)

        result, detail, ran = run_filters(_runner, mutant.filter)
        print(f"coverage   {mutant.unit}: {ran} tests from {mutant.filter}")
        if result != "pass":
            complaints.append(f"{mutant.unit}: its filters do not run green on the unmutated tree -- {detail}")
        elif ran != mutant.tests:
            complaints.append(
                f"{mutant.unit}: its filters select {ran} tests and mutants.json says {mutant.tests}."
                " A filter that selects the wrong tests reports survivors for code it never ran."
            )

    return complaints


def pick_runner(_root: Path, _wanted: str | None):
    if _wanted in (None, "portable") and PortableRunner.available():
        return PortableRunner(_root)
    if _wanted == "portable":
        sys.exit("error: --runner portable needs g++ and make on PATH")

    found = MsBuildRunner.find()
    if found:
        return MsBuildRunner(_root, *found)
    sys.exit(
        "error: no runner available. Either put g++ and make on PATH for Tests/PortableRunner,\n"
        "       or install Visual Studio for the MSBuild leg."
    )


# ---- applying one mutant ---------------------------------------------------------------------------


def apply(_root: Path, _mutant: Mutant) -> str:
    """Write the mutated file and return the original text, or exit if the edit is not exact."""
    path = _root / _mutant.file
    if not path.is_file():
        sys.exit(f"error: {_mutant.id}: {_mutant.file} does not exist in the worktree")

    # Read and write in TEXT mode on purpose. `.gitattributes` is `* text=auto`, so these files are
    # CRLF in a Windows checkout and LF on the Ubuntu leg; universal newlines make a multi-line
    # `find` match on both, and the write puts the platform's endings back. A binary read here would
    # make every multi-line mutant a Windows-only one, which is the §6.116 failure in miniature.
    original = path.read_text(encoding="utf-8", errors="strict")
    hits = original.count(_mutant.find)
    if hits != 1:
        sys.exit(
            f"error: {_mutant.id}: its `find` matches {hits} times in {_mutant.file} and must match once.\n"
            f"       A mutant that applies nowhere runs the UNMUTATED suite and reports a survivor;\n"
            f"       one that applies twice reports on an edit nobody wrote. Fix tools/mutants.json.\n"
            f"       find: {_mutant.find}"
        )

    path.write_text(original.replace(_mutant.find, _mutant.replace), encoding="utf-8")
    return original


def restore(_root: Path, _mutant: Mutant, _original: str) -> None:
    (_root / _mutant.file).write_text(_original, encoding="utf-8")


def outcome_of(_result: str, _detail: str, _seconds: float) -> Outcome:
    if _result in ("fail", "timeout"):
        return Outcome(CAUGHT, _detail if _result == "fail" else "TIMED OUT -- which is a catch", _seconds)
    if _result == "build-error":
        return Outcome(UNCOMPILABLE, _detail, _seconds)
    if _result == "no-summary":
        return Outcome("error", _detail, _seconds)
    return Outcome(SURVIVES, _detail, _seconds)


def matches(_outcome: Outcome, _expect: str) -> bool:
    """An `equivalent` mutant must SURVIVE -- that is what equivalent means."""
    if _expect == EQUIVALENT:
        return _outcome.result == SURVIVES
    return _outcome.result == _expect


def warn_if_dirty(_chosen: list[Mutant]) -> None:
    """The worktree is HEAD, so uncommitted work is NOT what gets measured.

    That is the right default -- a mutation run wants a state somebody can name -- but it is a
    quiet way to produce a tally for code that is not the code being written. Anything modified
    that a selected mutant touches, or any change to the suite itself, is worth a sentence before
    twenty minutes of building.
    """
    status = subprocess.run(["git", "status", "--porcelain"], cwd=REPO, capture_output=True, text=True)
    if status.returncode != 0:
        return

    dirty = {line[3:].strip().strip('"') for line in status.stdout.splitlines() if line.strip()}
    if not dirty:
        return

    interesting = sorted(
        path for path in dirty
        if path in {mutant.file for mutant in _chosen} or path.startswith("Tests/") or path.startswith("GameLogic/")
    )
    if interesting:
        print("note: the worktree is built from HEAD, and these are modified but NOT committed --")
        print("      the tally below will not describe them:")
        for path in interesting[:12]:
            print(f"        {path}")
        print()


def check_applicable(_chosen: list[Mutant]) -> int:
    """Every recorded mutant still points at code that exists, without building anything.

    This is the half of the tool that can run on every push. A mutation PASS is minutes per mutant
    and belongs to whoever is finishing a slice; a mutant whose `find` has rotted out of the file is
    a stale record, and a stale record is what Risk R13 is about. Catching that costs milliseconds,
    so it is a repository check and the run is not.

    It also refuses a mutant that changes nothing, because such a mutant can only ever "survive"
    and would sit in the list looking like measured evidence.
    """
    complaints: list[str] = []

    for mutant in _chosen:
        path = REPO / mutant.file
        where = f"{mutant.unit}/{mutant.id}"

        if not path.is_file():
            complaints.append(f"{where}: {mutant.file} does not exist")
            continue

        hits = path.read_text(encoding="utf-8", errors="replace").count(mutant.find)
        if hits != 1:
            complaints.append(f"{where}: `find` matches {hits} times in {mutant.file}, and must match exactly once")
        if mutant.find == mutant.replace:
            complaints.append(f"{where}: `find` and `replace` are identical, so the mutant changes nothing")
        if mutant.expect not in (CAUGHT, SURVIVES, EQUIVALENT, UNCOMPILABLE):
            complaints.append(f"{where}: unknown expect '{mutant.expect}'")

    # Every unit must carry one, because a unit without a selftest can report fifteen survivors
    # from a harness that never rebuilt and nothing would say so.
    for unit in {mutant.unit for mutant in _chosen}:
        if not any(mutant.selftest for mutant in _chosen if mutant.unit == unit):
            complaints.append(f"{unit}: no mutant is marked `selftest`, so a run of it cannot prove it observes a catch")

    print(f"recorded mutants {len(_chosen)}")
    if complaints:
        print("FAIL  mutants that no longer apply to the tree:")
        for complaint in complaints:
            print(f"      {complaint}")
        return 1

    print(f"OK    all {len(_chosen)} recorded mutants still apply")
    return 0


# ---- the run -------------------------------------------------------------------------------------


def main(_argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--list", action="store_true", help="print the recorded mutants and stop")
    parser.add_argument("--check", action="store_true", help="every mutant still applies to the tree; no build, no worktree")
    parser.add_argument("--unit", help="only this unit")
    parser.add_argument("--id", dest="ident", help="only this mutant")
    parser.add_argument("--runner", choices=["portable", "msbuild"], help="which test runner (default: portable if available)")
    parser.add_argument("--worktree", help="where to put the scratch worktree")
    parser.add_argument("--keep", action="store_true", help="leave the worktree behind for inspection")
    arguments = parser.parse_args(_argv)

    units = load_units()

    if arguments.list:
        for unit in units:
            print(f"{unit['name']}  (slice {unit['slice']}, filter '{unit['filter']}')")
            for mutant in unit["mutants"]:
                print(f"    {mutant['id']:<14s} {mutant.get('expect', CAUGHT):<13s} {mutant.get('note', '')[:88]}")
            print()
        return 0

    chosen = select(units, arguments.unit, arguments.ident)
    if not chosen:
        print("no mutants selected -- `--list` shows what is recorded")
        return 1

    if arguments.check:
        return check_applicable(chosen)

    check_oracle_present()
    warn_if_dirty(chosen)

    scratch = Path(arguments.worktree) if arguments.worktree else REPO.parent / f".mutate-{os.getpid()}"
    print(f"worktree   {scratch}")
    make_worktree(scratch)

    try:
        runner = pick_runner(scratch, arguments.runner)
        print(f"runner     {runner.name}")

        # ---- the baseline, which is this harness's own OracleIsPresent (section 6.119) ----------
        print("\nbaseline   running the UNMUTATED suite; nothing below is believed until it is green")
        started = time.time()
        result, detail, ran = runner.run(None)
        print(f"baseline   {result}: {detail}  ({ran} tests, {time.time() - started:.0f}s)")

        if result != "pass":
            print(
                "\nFAIL  the baseline is not green, so no mutation result from this tree means anything.\n"
                "      A harness that reads only the summary line would now report every mutant as\n"
                "      caught, which is exactly what produced three wrong tallies (section 6.119)."
            )
            return 1

        # ---- and the filters select what they claim to (see check_filters) ---------------------
        wrong = check_filters(runner, chosen)
        if wrong:
            print("\nFAIL  a unit's test filters do not match what tools/mutants.json records:")
            for complaint in wrong:
                print(f"      {complaint}")
            return 1

        # ---- the mutants ------------------------------------------------------------------------
        rows: list[tuple[Mutant, Outcome, bool]] = []
        for index, mutant in enumerate(chosen, 1):
            print(f"\n[{index}/{len(chosen)}] {mutant.unit}/{mutant.id}  (expect {mutant.expect})")
            original = apply(scratch, mutant)
            started = time.time()
            try:
                result, detail, _ = run_filters(runner, mutant.filter)
            finally:
                restore(scratch, mutant, original)

            outcome = outcome_of(result, detail, time.time() - started)
            agreed = matches(outcome, mutant.expect)
            rows.append((mutant, outcome, agreed))
            print(f"          {outcome.result:<13s} {'ok' if agreed else 'UNEXPECTED'}  {outcome.detail}  ({outcome.seconds:.0f}s)")

            if mutant.selftest and not agreed:
                print(
                    "\nFAIL  the harness self-test did not do what it must, so nothing else in this run\n"
                    "      means anything and the rest is not attempted. This mutant is an unmissable\n"
                    "      change; if the suite did not catch it, then the edit did not reach the binary,\n"
                    "      the build did not happen, or the failure was not observed -- which is Risk R13\n"
                    "      realised exactly as section 6.119 describes it."
                )
                return 1

    finally:
        if not arguments.keep:
            remove_worktree(scratch)

    # ---- the tally, which is the point ----------------------------------------------------------
    print("\n" + "-" * 96)
    caught = sum(1 for _, outcome, _ in rows if outcome.result == CAUGHT)
    survived = sum(1 for _, outcome, _ in rows if outcome.result == SURVIVES)
    other = len(rows) - caught - survived
    print(f"{len(rows)} mutants: {caught} caught, {survived} survived, {other} neither")

    unexpected = [(mutant, outcome) for mutant, outcome, agreed in rows if not agreed]
    if unexpected:
        print("\nFAIL  outcomes that differ from tools/mutants.json:")
        for mutant, outcome in unexpected:
            print(f"      {mutant.unit}/{mutant.id}: expected {mutant.expect}, got {outcome.result} -- {outcome.detail}")
        print("\n      A NEW SURVIVOR is a gap in the tests: probe the comparison, count the distinct")
        print("      values that reach it, and add a ladder (plan section 6.132).")
        print("      A RECORDED SURVIVOR NOW CAUGHT is debt that has been paid -- record it as paid.")
        return 1

    print("OK    every mutant did what tools/mutants.json says it does")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
