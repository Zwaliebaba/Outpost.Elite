#!/usr/bin/env python3
"""Run every test suite the build produced, through vstest.console.exe, in parallel shards.

  python Build/RunTests.py [--configuration Debug] [--jobs N] [--vstest BINARY] [--dry-run]

The suites are every *Tests.vcxproj in the tree (AGENTS.md §3), as x64/<configuration>/<name>.dll, and
every one must have been built: vstest reports "no tests found" as a pass, so a suite that did not
build is not a suite that passed. No suites at all is not an error.

The tests run as concurrent vstest processes, one per shard. Each shard is a group of test classes,
matched on the test's fully qualified name, and one more shard takes every test the groups do not name,
so a class added or renamed still runs. A group that runs no test fails, which is how a group left
naming a renamed class is found. The groups are a balance of measured times, not a rule: moving a class
between them changes how long CI takes, never what it runs. Each shard writes TestResults/<shard>.trx,
and its counts are read back from it.

Exit status: 0 every shard passed or there is nothing to run, 1 a shard failed, 2 usage or
environment error (a suite not built, no vstest).
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# The groups, by Debug time measured 2026-10-10 over GameLogicTests' 173 tests (381 s in all, g++ -O0;
# MSVC's Debug is about 1.6 times slower): CorpusTests 83 s and FlightTests 81 s are the two heaviest
# classes, and each group comes to 90-105 s with the shard that takes the rest. MachineTests runs in
# well under a second and is left to the rest. Four even shards keep every core busy to the end on
# a runner with two or four; each test is one thread, so more cores than shards are idle.
GROUPS = [
  ("corpus", ["CorpusTests", "StartUpTests"]),
  ("flight", ["FlightTests", "DockedTests"]),
  ("screens", ["GalaxyTests", "InputTests", "DockingTests", "MarketTests", "SaveLoadTests"]),
]
REST = "rest"
RESULTS = "TestResults"


def find_suites(_configuration: str) -> list[Path]:
  """The DLL of every *Tests.vcxproj under the root, outside hidden directories; exits if one is missing."""
  projects = sorted(path for path in ROOT.rglob("*Tests.vcxproj")
                    if not any(part.startswith(".") for part in path.relative_to(ROOT).parts))
  suites = []
  for project in projects:
    dll = ROOT / "x64" / _configuration / f"{project.stem}.dll"
    if not dll.is_file():
      print(f"error: {dll.relative_to(ROOT)} was not built. A suite that does not exist is not a suite that passed.",
            file=sys.stderr)
      sys.exit(2)
    suites.append(dll)
  return suites


def find_vstest() -> str | None:
  """vstest.console.exe from the newest Visual Studio, by vswhere."""
  vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
  if not vswhere.is_file():
    return shutil.which("vstest.console.exe")
  found = subprocess.run([str(vswhere), "-latest", "-products", "*", "-find", r"**\vstest.console.exe"], capture_output=True,
                         text=True, check=False)
  lines = [line.strip() for line in found.stdout.splitlines() if line.strip()]
  return lines[0] if lines else None


def shard_filters() -> list[tuple[str, str]]:
  """Each shard's name and its /TestCaseFilter expression: the groups, then the rest."""
  shards = [(name, "|".join(f"FullyQualifiedName~{test_class}" for test_class in classes)) for name, classes in GROUPS]
  every_class = [test_class for _, classes in GROUPS for test_class in classes]
  shards.append((REST, "&".join(f"FullyQualifiedName!~{test_class}" for test_class in every_class)))
  return shards


def command(_vstest: str, _suites: list[Path], _name: str, _filter: str) -> list[str]:
  return [_vstest, *(str(suite) for suite in _suites), "/Platform:x64", f"/TestCaseFilter:{_filter}",
          f"/Logger:trx;LogFileName={_name}.trx", f"/ResultsDirectory:{RESULTS}"]


def counts(_name: str) -> dict[str, int] | None:
  """The Counters of TestResults/<_name>.trx (total, executed, passed, failed, ...), or None if unreadable."""
  try:
    root = ElementTree.parse(ROOT / RESULTS / f"{_name}.trx").getroot()
  except (OSError, ElementTree.ParseError):
    return None
  for element in root.iter():
    if element.tag.endswith("Counters"):
      return {key: int(value) for key, value in element.attrib.items() if value.isdigit()}
  return None


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--configuration", default="Debug", help="the build configuration whose suites to run (default: Debug)")
  parser.add_argument("--jobs", type=int, default=len(GROUPS) + 1,
                      help=f"shards run at once (default: all {len(GROUPS) + 1}); 1 runs them one after another")
  parser.add_argument("--vstest", help="vstest.console.exe to use instead of the one vswhere finds")
  parser.add_argument("--dry-run", action="store_true", help="print each shard's command and run nothing")
  arguments = parser.parse_args()
  if arguments.jobs < 1:
    print("error: --jobs must be at least 1", file=sys.stderr)
    return 2

  suites = find_suites(arguments.configuration)
  if not suites:
    print("No *Tests.vcxproj in the tree yet. Nothing to run.")
    return 0
  vstest = arguments.vstest or find_vstest()
  if not vstest:
    print("error: vstest.console.exe not found.", file=sys.stderr)
    return 2

  # vstest's output is passed through as it is, and a Windows console's code page cannot print all of it.
  sys.stdout.reconfigure(encoding="utf-8", errors="replace")
  shards = [(name, command(vstest, suites, name, expression)) for name, expression in shard_filters()]
  if arguments.dry_run:
    for name, arguments_list in shards:
      print(f"{name}: {subprocess.list2cmdline(arguments_list)}")
    return 0

  # Each shard's output goes to a file of its own and is printed whole when the shard ends, so that
  # concurrent shards do not interleave.
  print(f"{len(shards)} shards, {min(arguments.jobs, len(shards))} at a time, on {os.cpu_count()} logical processors.", flush=True)
  failed = []
  total = 0
  pending = list(shards)
  running = []
  with tempfile.TemporaryDirectory(prefix="RunTests-", ignore_cleanup_errors=True) as scratch:
    try:
      while pending or running:
        while pending and len(running) < arguments.jobs:
          name, arguments_list = pending.pop(0)
          (ROOT / RESULTS / f"{name}.trx").unlink(missing_ok=True)  # a shard that dies must not be read from an old run
          log = open(Path(scratch) / f"{name}.log", "w+", encoding="utf-8", errors="replace")
          process = subprocess.Popen(arguments_list, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
          running.append((name, process, log, time.monotonic()))
        time.sleep(1)
        for entry in list(running):
          name, process, log, started = entry
          if process.poll() is None:
            continue
          running.remove(entry)
          log.seek(0)
          output = log.read()
          log.close()
          found = counts(name)
          summary = "no results" if found is None else f"{found.get('executed', 0)} run, {found.get('passed', 0)} passed"
          print(f"== shard {name}: exit {process.returncode} after {time.monotonic() - started:.0f} s, {summary} ==")
          print(output, flush=True)
          if process.returncode != 0:
            failed.append(name)
          elif found is None or found.get("executed", 0) == 0:
            print(f"error: shard {name} ran no test: does a class it names still exist?", file=sys.stderr)
            failed.append(name)
          else:
            total += found.get("executed", 0)
    finally:
      # Whatever stopped the loop, no shard is left running or holding its log open.
      for _, process, log, _ in running:
        process.kill()
        process.wait()
        log.close()

  if failed:
    print(f"Shards failed: {', '.join(failed)}.", file=sys.stderr)
    return 1
  print(f"All {len(shards)} shards passed: {total} tests run.")
  return 0


if __name__ == "__main__":
  sys.exit(main())
