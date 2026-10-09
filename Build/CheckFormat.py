#!/usr/bin/env python3
"""Check that every C++ file in the tree is formatted by clang-format with the root .clang-format (AGENTS.md §4).

  python Build/CheckFormat.py [--root DIR] [--clang-format BINARY] [--fix]

Every .h and .cpp that git would hand to a fresh clone is checked (tracked, plus untracked and not
ignored), except anything under a CompiledShader/ directory, which is build output (AGENTS.md §2).
The style is always the root .clang-format, never one found nearer a file.

  format   §4  The file is not what clang-format would write. `--fix` rewrites it in place.

Each finding is printed as `path:line: format: message`, the line being the first place clang-format
would change. Exit status: 0 clean (or every offender fixed), 1 findings, 2 usage or environment error
(no clang-format, no .clang-format, or clang-format itself failing).

CI runs this on Linux with clang-format 18.1.3, because the output changes between releases; a local
run on another version says so, and the Linux job is the answer that counts.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent

PINNED_VERSION = "18.1.3"
BATCH_SIZE = 50
# clang-format --dry-run reports each place it would change as `<file>:<line>:<col>: error: ...`.
VIOLATION = re.compile(r"^(?P<path>.+?):(?P<line>\d+):\d+: (?:error|warning): code should be clang-formatted")


def list_files(_root: Path) -> list[str]:
  """Every file a clone would have, relative to the root with forward slashes, sorted."""
  try:
    result = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=_root,
                            capture_output=True, check=True)
    names = result.stdout.decode("utf-8", errors="surrogateescape").split("\0")
  except (OSError, subprocess.CalledProcessError):
    print("note: git is not available here, so the tree is walked instead and build output is skipped by name")
    names = []
    skipped = {".git", ".vs", "x64", "compiledshader", "__pycache__"}
    for directory, subdirs, files in os.walk(_root):
      subdirs[:] = [name for name in subdirs if name.casefold() not in skipped]
      names.extend((Path(directory) / name).relative_to(_root).as_posix() for name in files)
  return sorted({name for name in names if name and (_root / name).is_file()})


def cpp_files(_root: Path) -> list[str]:
  return [path for path in list_files(_root)
          if PurePosixPath(path).suffix in (".h", ".cpp") and "CompiledShader" not in PurePosixPath(path).parts[:-1]]


def run_clang_format(_binary: str, _root: Path, _arguments: list[str], _files: list[str]) -> list[str]:
  """Run clang-format over the files in batches; return its diagnostic lines. Raises on a hard failure."""
  lines: list[str] = []
  for start in range(0, len(_files), BATCH_SIZE):
    batch = _files[start:start + BATCH_SIZE]
    result = subprocess.run([_binary, *_arguments, *batch], cwd=_root, capture_output=True, encoding="utf-8",
                            errors="replace")
    output = (result.stderr + result.stdout).splitlines()
    if result.returncode != 0 and not any(VIOLATION.match(line) for line in output):
      raise RuntimeError("\n".join(output) or f"clang-format exited with {result.returncode}")
    lines.extend(output)
  return lines


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: the parent of Build/)")
  parser.add_argument("--clang-format", dest="clang_format", default="clang-format",
                      help="clang-format binary (default: clang-format on PATH)")
  parser.add_argument("--fix", action="store_true", help="rewrite the offending files in place")
  arguments = parser.parse_args()
  root = arguments.root.resolve()
  if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(errors="replace")  # a cp1252 console must not crash on a source line it echoes
  if not root.is_dir():
    print(f"error: {root} is not a directory", file=sys.stderr)
    return 2

  files = cpp_files(root)
  if not files:
    print("CheckFormat: no C++ files in the tree yet -- nothing to check.")
    return 0

  style = root / ".clang-format"
  if not style.is_file():
    print(f"error: {style} does not exist; it is the style every file is checked against", file=sys.stderr)
    return 2
  binary = shutil.which(arguments.clang_format)
  if binary is None:
    print(f"error: clang-format not found ('{arguments.clang_format}'). Install clang-format {PINNED_VERSION} or pass "
          "--clang-format <path>.", file=sys.stderr)
    return 2
  version = subprocess.run([binary, "--version"], capture_output=True, text=True).stdout.strip()
  print(f"clang-format: {version} ({binary})")
  if PINNED_VERSION not in version:
    print(f"note: CI pins clang-format {PINNED_VERSION}; another version can break lines differently, so if this "
          "run and CI disagree, CI is the answer that counts.")

  try:
    lines = run_clang_format(binary, root, ["--dry-run", "--Werror", f"--style=file:{style}"], files)
  except RuntimeError as error:
    print(f"error: clang-format failed:\n{error}", file=sys.stderr)
    return 2
  offenders: dict[str, list[int]] = {}
  for line in lines:
    match = VIOLATION.match(line)
    if match:
      offenders.setdefault(match.group("path").replace("\\", "/"), []).append(int(match.group("line")))

  if arguments.fix and offenders:
    try:
      run_clang_format(binary, root, ["-i", f"--style=file:{style}"], sorted(offenders))
    except RuntimeError as error:
      print(f"error: clang-format failed:\n{error}", file=sys.stderr)
      return 2
    for path in sorted(offenders):
      print(f"rewrote {path}")
    print(f"CheckFormat: rewrote {len(offenders)} of {len(files)} C++ file(s).")
    return 0

  for path, places in sorted(offenders.items()):
    print(f"{path}:{min(places)}: format: not clang-formatted ({len(places)} place(s)); run "
          "`python Build/CheckFormat.py --fix`")
  if offenders:
    print(f"\nCheckFormat: {len(offenders)} of {len(files)} C++ file(s) not formatted (AGENTS.md §4).")
    return 1
  print(f"CheckFormat: clean -- {len(files)} C++ file(s).")
  return 0


if __name__ == "__main__":
  sys.exit(main())
