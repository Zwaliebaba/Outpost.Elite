#!/usr/bin/env python3
r"""Fetch the SingleStepTests 8088 suite, convert it, and run it against Machine's interpreter (ADR-003 item 1).

  python Tools/CpuConformance.py [NAME...] [--listing FILE] [--cache DIR] [--runner PATH]
                                 [--compiler CXX] [--failures N] [--jobs N]

The suite is SingleStepTests 8088 v2 (https://github.com/SingleStepTests/8088, MIT licence): 10,000
tests per opcode, fewer for some, recorded from a real 8088. One test is one instruction: registers and
memory before, registers and memory after. It is about 1 GB compressed, so it is never committed
(ADR-003): this script downloads what it needs into a cache OUTSIDE the repository, default
~/.cache/outpost-sst (or $OUTPOST_SST_CACHE, or --cache), and refuses a cache inside it.

What to run:
  NAME...          suite files by name: 00, F6.6, D0.4 ... (a group opcode has one file per reg field)
  --listing FILE   the opcodes in a Tools/MapReference.py --listing, i.e. what the game executes
  (neither)        the whole suite

For each file it fetches v2/<NAME>.json.gz and the suite's v2/metadata.json, then writes a line-based
text form the C++ runner reads (see CpuConformance/TestReader.h). The metadata gives, per opcode and
reg field, the flags the 8088 leaves undefined; each test carries that mask and only those flags are
excluded from the comparison. Converted files are cached beside the downloads.

The runner is CpuConformance: --runner names it; otherwise on Windows the solution's x64\Release or
x64\Debug build is used, and elsewhere it is compiled into the cache with Tools/BuildPortable.py. The
runner prints the first --failures failing tests of each file in detail; this script then prints a
pass/fail line per file and the totals. Exit status: 0 all passed, 1 failures, 2 usage, network or
build error. Standard library only (AGENTS.md R14 binds the product, and this is a tool).
"""

import argparse
import concurrent.futures
import gzip
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE_URL = "https://raw.githubusercontent.com/SingleStepTests/8088/main/v2/"
METADATA = "metadata.json"
FORMAT_HEADER = "# SingleStepTests 8088 v2, converted by Tools/CpuConformance.py, format 1"
PREFIXES = frozenset({0x26, 0x2E, 0x36, 0x3E, 0xF0, 0xF1, 0xF2, 0xF3})
REGISTERS = ("ax", "bx", "cx", "dx", "cs", "ss", "ds", "es", "sp", "bp", "si", "di", "ip", "flags")
LISTING_LINE = re.compile(r"^  [0-9A-F]{4}  ([0-9a-f]+)\s")
FILES_PER_RUN = 24
DOWNLOAD_ATTEMPTS = 3


def default_cache() -> Path:
  configured = os.environ.get("OUTPOST_SST_CACHE")
  return Path(configured) if configured else Path.home() / ".cache" / "outpost-sst"


# ── Download ─────────────────────────────────────────────────────────────────────────────────

class Suite:
  """The cache: downloads, which names exist upstream, and the metadata."""

  def __init__(self, _cache: Path) -> None:
    self.cache = _cache
    self.downloads = _cache / "v2"
    self.converted = _cache / "converted"
    self.known_path = _cache / "known.json"
    self.downloads.mkdir(parents=True, exist_ok=True)
    self.converted.mkdir(parents=True, exist_ok=True)
    try:
      self.known: dict[str, bool] = json.loads(self.known_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
      self.known = {}
    self.metadata = json.loads(self.fetch(METADATA).read_text(encoding="utf-8"))

  def fetch(self, _file: str) -> Path:
    """The cached copy of a file in v2/, downloaded if need be. Raises FileNotFoundError on a 404.

    A transfer can be cut short without an error, so a download is kept only when it has the length
    the server announced and, for a .gz, decompresses to the end.
    """
    target = self.downloads / _file
    if target.exists():
      return target
    partial = target.with_name(target.name + ".part")
    failure: Exception | None = None
    for _attempt in range(DOWNLOAD_ATTEMPTS):
      try:
        with urllib.request.urlopen(BASE_URL + _file, timeout=120) as response, partial.open("wb") as stream:
          announced = response.headers.get("Content-Length")
          received = 0
          while chunk := response.read(1 << 20):
            stream.write(chunk)
            received += len(chunk)
        if announced is not None and received != int(announced):
          raise OSError(f"received {received} of {announced} bytes")
        if _file.endswith(".gz"):
          with gzip.open(partial, "rb") as check:
            while check.read(1 << 24):
              pass
        partial.replace(target)
        return target
      except urllib.error.HTTPError as error:
        partial.unlink(missing_ok=True)
        if error.code == 404:
          raise FileNotFoundError(_file) from error
        failure = error
      except (OSError, EOFError, urllib.error.URLError) as error:
        partial.unlink(missing_ok=True)
        failure = error
    raise OSError(f"{_file}: download failed {DOWNLOAD_ATTEMPTS} times; last error: {failure}")

  def exists(self, _name: str) -> bool:
    """Whether the suite has <name>.json.gz, asking upstream once and remembering the answer."""
    if _name not in self.known:
      try:
        self.fetch(f"{_name}.json.gz")
        self.known[_name] = True
      except FileNotFoundError:
        self.known[_name] = False
      self.known_path.write_text(json.dumps(self.known, indent=1, sort_keys=True), encoding="utf-8")
    return self.known[_name]

  def opcode_names(self, _opcode: str) -> list[str]:
    """The suite files for one opcode: one per reg field for most group opcodes, else one."""
    entry = self.metadata["opcodes"].get(_opcode, {})
    if entry.get("status") == "prefix":
      return []
    if "reg" in entry and self.exists(f"{_opcode}.0"):
      return [f"{_opcode}.{reg}" for reg in sorted(entry["reg"]) if self.exists(f"{_opcode}.{reg}")]
    return [_opcode] if self.exists(_opcode) else []

  def all_names(self) -> list[str]:
    names: list[str] = []
    for opcode in sorted(self.metadata["opcodes"]):
      names.extend(self.opcode_names(opcode))
    return names

  def listing_names(self, _listing: Path) -> list[str]:
    """The suite files covering every instruction in a MapReference listing."""
    wanted: set[tuple[str, int]] = set()
    for line in _listing.read_text(encoding="utf-8", errors="replace").splitlines():
      match = LISTING_LINE.match(line)
      if match is None:
        continue
      code = bytes.fromhex(match.group(1))
      index = 0
      while index < len(code) and code[index] in PREFIXES:
        index += 1
      if index < len(code):
        reg = (code[index + 1] >> 3) & 7 if index + 1 < len(code) else 0
        wanted.add((f"{code[index]:02X}", reg))
    names: set[str] = set()
    for opcode, reg in wanted:
      files = self.opcode_names(opcode)
      grouped = [name for name in files if name == f"{opcode}.{reg}"]
      names.update(grouped if grouped else [name for name in files if "." not in name])
      if not files:
        print(f"note: the suite has no tests for opcode {opcode}, which the listing uses")
    return sorted(names)


# ── Conversion ───────────────────────────────────────────────────────────────────────────────

def flags_mask(_opcodes: dict, _bytes: list[int]) -> int:
  """The metadata's flags-mask for the instruction: every flag except those it leaves undefined."""
  index = 0
  while index < len(_bytes) and _bytes[index] in PREFIXES:
    index += 1
  if index >= len(_bytes):
    return 0xFFFF
  entry = _opcodes.get(f"{_bytes[index]:02X}", {})
  if "reg" in entry and index + 1 < len(_bytes):
    entry = entry["reg"].get(str((_bytes[index + 1] >> 3) & 7), {})
  return int(entry.get("flags-mask", 0xFFFF))


def convert(_source: Path, _target: Path, _opcodes: dict) -> int:
  """Writes the runner's text form of one suite file; returns the number of tests."""
  with gzip.open(_source, "rt", encoding="utf-8") as stream:
    tests = json.load(stream)
  lines = [FORMAT_HEADER]
  for test in tests:
    initial = test["initial"]
    final = test["final"]
    registers = dict(initial["regs"])
    expected = {**registers, **final["regs"]}
    memory = {address: value for address, value in initial["ram"]}
    expected_memory = {**memory, **{address: value for address, value in final["ram"]}}
    name = " ".join(str(test["name"]).split())
    lines.append(f"t {test['idx']:x} {flags_mask(_opcodes, test['bytes']):x} {name}")
    lines.append("b " + "".join(f"{value:02x}" for value in test["bytes"]))
    lines.append("i " + " ".join(f"{registers[register]:x}" for register in REGISTERS))
    lines.append("m " + " ".join(f"{address:x} {value:x}" for address, value in memory.items()))
    lines.append("f " + " ".join(f"{expected[register]:x}" for register in REGISTERS))
    lines.append("n " + " ".join(f"{address:x} {value:x}" for address, value in expected_memory.items()))
  partial = _target.with_name(_target.name + ".part")
  partial.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
  partial.replace(_target)
  return len(tests)


def is_current(_target: Path, _source: Path) -> bool:
  if not _target.exists() or _target.stat().st_mtime < _source.stat().st_mtime:
    return False
  with _target.open(encoding="utf-8") as stream:
    return stream.readline().rstrip("\n") == FORMAT_HEADER


# ── Runner ───────────────────────────────────────────────────────────────────────────────────

def find_runner(_arguments: argparse.Namespace, _cache: Path) -> Path:
  if _arguments.runner is not None:
    return _arguments.runner
  if os.name == "nt":
    for configuration in ("Release", "Debug"):
      candidate = ROOT / "x64" / configuration / "CpuConformance.exe"
      if candidate.exists():
        return candidate
    raise RuntimeError("no CpuConformance.exe under x64\\Release or x64\\Debug; build the solution, or pass --runner")
  out = _cache / "build"
  command = [sys.executable, str(ROOT / "Tools" / "BuildPortable.py"), "CpuConformance", "--out", str(out),
             "--compiler", _arguments.compiler]
  result = subprocess.run(command, capture_output=True, text=True)
  if result.returncode != 0:
    raise RuntimeError(f"building the runner failed:\n{result.stdout}{result.stderr}")
  return out / "CpuConformance"


def run_batch(_runner: Path, _files: list[Path], _failures: int) -> str:
  result = subprocess.run([str(_runner), "--failures", str(_failures), *map(str, _files)], capture_output=True,
                          text=True)
  if result.returncode == 2 or result.stderr:
    return result.stdout + result.stderr
  return result.stdout


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("names", nargs="*", help="suite files to run, e.g. 00 F6.6 (default: all)")
  parser.add_argument("--listing", type=Path, help="run the opcodes a Tools/MapReference.py listing uses")
  parser.add_argument("--cache", type=Path, default=default_cache(), help="download and conversion cache")
  parser.add_argument("--runner", type=Path, help="the CpuConformance executable")
  parser.add_argument("--compiler", default="g++", help="compiler for building the runner off Windows")
  parser.add_argument("--failures", type=int, default=3, help="failing tests to show per file (default 3)")
  parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
  arguments = parser.parse_args()

  cache = arguments.cache.expanduser().resolve()
  if cache == ROOT or ROOT in cache.parents:
    print(f"error: the cache {cache} is inside the repository; the suite is never committed (ADR-003)",
          file=sys.stderr)
    return 2
  try:
    suite = Suite(cache)
    if arguments.names:
      names = arguments.names
    elif arguments.listing is not None:
      names = suite.listing_names(arguments.listing)
    else:
      names = suite.all_names()
    sources = {name: suite.fetch(f"{name}.json.gz") for name in names}
  except FileNotFoundError as error:
    print(f"error: the suite has no {error}", file=sys.stderr)
    return 2
  except (OSError, urllib.error.URLError, ValueError) as error:
    print(f"error: {error}", file=sys.stderr)
    return 2

  targets = {name: suite.converted / f"{name}.txt" for name in names}
  stale = [name for name in names if not is_current(targets[name], sources[name])]
  if stale:
    print(f"converting {len(stale)} file(s)...", flush=True)
    with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, arguments.jobs)) as pool:
      futures = {name: pool.submit(convert, sources[name], targets[name], suite.metadata["opcodes"]) for name in stale}
      for name, future in futures.items():
        try:
          future.result()
        except (OSError, EOFError, ValueError) as error:
          # A corrupt download is removed, so that the next run fetches it again.
          sources[name].unlink(missing_ok=True)
          print(f"error: {name}: cannot convert ({error}); the download was removed, run again", file=sys.stderr)
          return 2

  try:
    runner = find_runner(arguments, cache)
  except RuntimeError as error:
    print(f"error: {error}", file=sys.stderr)
    return 2

  batches = [names[start:start + FILES_PER_RUN] for start in range(0, len(names), FILES_PER_RUN)]
  results: dict[str, tuple[int, int]] = {}
  errors = False
  with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as pool:
    outputs = pool.map(run_batch, [runner] * len(batches), [[targets[name] for name in batch] for batch in batches],
                       [arguments.failures] * len(batches))
    for output in outputs:
      for line in output.splitlines():
        fields = line.split("\t")
        if fields[0] == "RESULT" and len(fields) == 4:
          results[Path(fields[1]).stem] = (int(fields[2]), int(fields[3]))
          continue
        if fields[0] == "ERROR":
          errors = True
        if fields[0] in ("FAIL", "ERROR") and len(fields) >= 2:
          fields[1] = Path(fields[1]).stem
        print("\t".join(fields))

  print()
  print(f"{'file':<8} {'passed':>7} {'total':>7}  result")
  total_passed = total_tests = 0
  failing: list[str] = []
  for name in names:
    if name not in results:
      print(f"{name:<8} {'':>7} {'':>7}  NOT RUN")
      errors = True
      continue
    passed, total = results[name]
    total_passed += passed
    total_tests += total
    verdict = "pass" if passed == total else f"FAIL {100.0 * passed / max(total, 1):.2f}%"
    if passed != total:
      failing.append(name)
    print(f"{name:<8} {passed:>7} {total:>7}  {verdict}")
  print(f"\n{len(names) - len(failing)} of {len(names)} file(s) pass; {total_passed} of {total_tests} test(s) pass")
  if failing:
    print("failing: " + " ".join(failing))
  if errors:
    return 2
  return 0 if not failing else 1


if __name__ == "__main__":
  sys.exit(main())
