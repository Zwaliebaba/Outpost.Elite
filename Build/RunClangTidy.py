#!/usr/bin/env python3
"""Run clang-tidy over every .cpp the solution builds, with the switches its project sets for Debug|x64.

  python Build/RunClangTidy.py [--root DIR] [--clang-tidy BINARY] [--jobs N]

This is the gate behind AGENTS.md §1's naming table (R1, R3, R5, R8) and the semantic checks the root
.clang-tidy enables. The solution is the single .slnx/.sln at the root (§3); every ClCompile .cpp of
every project it lists is linted through clang's MSVC driver (`--driver-mode=cl`), with the root
.clang-tidy as the configuration and these switches derived from the project's Debug|x64 settings:

  PreprocessorDefinitions          /D...       (plus UNICODE/_UNICODE from CharacterSet=Unicode)
  AdditionalIncludeDirectories     /I...       ($(SolutionDir) is the root; %(...) is dropped)
  LanguageStandard                 /std:c++latest (or the stated standard)
  ConformanceMode                  /permissive-
  EnableEnhancedInstructionSet     /arch:AVX2
  FloatingPointModel               /fp:precise
  RuntimeLibrary                   /MDd        (or as stated)
  and always                       /EHsc

WarningLevel is deliberately not passed. Compiler warnings are the MSVC build's job (/W4 with /WX, §3);
given /W4, clang's driver turns on clang's own warnings, and `WarningsAsErrors: '*'` would make them
fatal on code MSVC accepts. The manual command in .clang-tidy's header passes no warning level either.

clang-tidy finds the CRT and the Windows SDK through the INCLUDE variable, so it must run from a
Developer PowerShell (AGENTS.md §3) -- or, in CI, after the step that imports the MSVC environment.

Each file's output is printed only when it has findings or errors. Exit status: 0 clean or nothing to
lint, 1 findings (`.clang-tidy` sets WarningsAsErrors: '*', so any finding fails), 2 usage or
environment error (no INCLUDE, no clang-tidy, no .clang-tidy, two solutions, an unreadable project).
"""

import argparse
import concurrent.futures
import os
import posixpath
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Keep in step with CLANG_TIDY_VERSION in .github/workflows/build.yml.
PINNED_VERSION = "22.1.8"
CONFIGURATION = "Debug|x64"

STANDARDS = {"stdcpplatest": "/std:c++latest", "stdcpp20": "/std:c++20", "stdcpp17": "/std:c++17",
             "stdcpp14": "/std:c++14"}
INSTRUCTION_SETS = {"advancedvectorextensions2": "/arch:AVX2", "advancedvectorextensions": "/arch:AVX",
                    "advancedvectorextensions512": "/arch:AVX512"}
FLOATING_POINT = {"precise": "/fp:precise", "strict": "/fp:strict", "fast": "/fp:fast"}
RUNTIMES = {"multithreaded": "/MT", "multithreadeddebug": "/MTd", "multithreadeddll": "/MD",
            "multithreadeddebugdll": "/MDd"}


# ── Solution and projects ────────────────────────────────────────────────────────────────────

def local(_tag: str) -> str:
  return _tag.rsplit("}", 1)[-1]


def solution_projects(_solution: Path) -> list[str]:
  """The project paths a .slnx or .sln lists, relative to the root with forward slashes."""
  if _solution.suffix.casefold() == ".slnx":
    paths = [node.get("Path", "") for node in ElementTree.parse(_solution).iter() if local(node.tag) == "Project"]
  else:
    text = _solution.read_text(encoding="utf-8-sig", errors="replace")
    paths = re.findall(r'^\s*Project\("\{[^}]*\}"\)\s*=\s*"[^"]*",\s*"([^"]*\.vcxproj)"', text, re.M | re.I)
  return [posixpath.normpath(path.replace("\\", "/")) for path in paths if path.casefold().endswith(".vcxproj")]


CONDITION_TOKEN = re.compile(r"\s*('[^']*'|==|!=|<=|>=|<|>|\(|\)|,|!|\$\([^)]*\)|[A-Za-z_][\w.:]*|\d[\w.]*)")


class Condition:
  """An MSBuild Condition, for the forms a .vcxproj uses. Undefined properties are empty; Exists() is false."""

  def __init__(self, _text: str, _properties: dict[str, str]) -> None:
    self.text = _text
    self.properties = _properties
    self.tokens: list[str] = []
    position = 0
    while _text[position:].strip():
      match = CONDITION_TOKEN.match(_text, position)
      if match is None:
        raise ValueError(f"cannot read the condition {_text!r}")
      self.tokens.append(match.group(1))
      position = match.end()
    self.position = 0

  def evaluate(self) -> bool:
    result = self.truth(self.disjunction())
    if self.position != len(self.tokens):
      raise ValueError(f"cannot read the condition {self.text!r}")
    return result

  def peek(self) -> str:
    return self.tokens[self.position] if self.position < len(self.tokens) else ""

  def take(self) -> str:
    token = self.peek()
    if not token:
      raise ValueError(f"the condition {self.text!r} ends early")
    self.position += 1
    return token

  def truth(self, _value: bool | str) -> bool:
    if isinstance(_value, bool):
      return _value
    if _value.casefold() in ("true", "on", "yes", "false", "off", "no"):
      return _value.casefold() in ("true", "on", "yes")
    raise ValueError(f"{_value!r} is not a boolean in the condition {self.text!r}")

  def disjunction(self) -> bool | str:
    value = self.conjunction()
    while self.peek().casefold() == "or":
      self.take()
      right = self.conjunction()
      value = self.truth(value) or self.truth(right)
    return value

  def conjunction(self) -> bool | str:
    value = self.negation()
    while self.peek().casefold() == "and":
      self.take()
      right = self.negation()
      value = self.truth(value) and self.truth(right)
    return value

  def negation(self) -> bool | str:
    if self.peek() == "!":
      self.take()
      return not self.truth(self.negation())
    left = self.operand()
    if self.peek() in ("==", "!=", "<", ">", "<=", ">="):
      operator = self.take()
      right = self.operand()
      if operator in ("==", "!="):
        return (str(left).casefold() == str(right).casefold()) == (operator == "==")
      try:
        a, b = float(str(left)), float(str(right))
      except ValueError:
        return False
      return {"<": a < b, ">": a > b, "<=": a <= b, ">=": a >= b}[operator]
    return left

  def operand(self) -> bool | str:
    token = self.take()
    if token == "(":
      value = self.disjunction()
      if self.take() != ")":
        raise ValueError(f"unbalanced parentheses in the condition {self.text!r}")
      return value
    if token.startswith(("'", "$(")):
      return expand(token.strip("'"), self.properties)
    if self.peek() == "(":
      self.take()
      while self.peek() != ")":
        self.operand()
        if self.peek() == ",":
          self.take()
      self.take()
      if token.casefold() == "exists":
        return False
      raise ValueError(f"the condition {self.text!r} calls {token}(), which this script does not evaluate")
    return token


def expand(_text: str, _properties: dict[str, str]) -> str:
  return re.sub(r"\$\(([A-Za-z_]\w*)\)", lambda _match: _properties.get(_match.group(1).casefold(), ""), _text)


def inherit(_value: str, _name: str, _previous: str | None) -> str:
  """Expand %(Name) inside Name's own value to what it was before, as MSBuild does."""
  if _previous is None:
    return _value
  return re.sub(r"%\(\s*" + re.escape(_name) + r"\s*\)", lambda _match: _previous, _value, flags=re.I)


def translation_units(_project: Path, _root: Path) -> list[tuple[Path, list[str]]]:
  """(source, clang-cl switches) for every ClCompile .cpp the project builds for Debug|x64."""
  configuration, platform = CONFIGURATION.split("|")
  values = {"configuration": configuration, "platform": platform}
  definitions: dict[str, str] = {}
  sources: list[tuple[str, dict[str, str]]] = []
  document = ElementTree.parse(_project).getroot()

  def holds(_node: ElementTree.Element) -> bool:
    condition = (_node.get("Condition") or "").strip()
    return not condition or Condition(condition, values).evaluate()

  for group in document:
    if local(group.tag) == "PropertyGroup" and holds(group):
      for prop in (node for node in group if holds(node)):
        values[local(prop.tag).casefold()] = expand((prop.text or "").strip(), values)
  for group in document:
    if local(group.tag) == "ItemDefinitionGroup" and holds(group):
      for kind in (node for node in group if holds(node) and local(node.tag).casefold() == "clcompile"):
        for meta in (node for node in kind if holds(node)):
          name = local(meta.tag)
          definitions[name.casefold()] = inherit((meta.text or "").strip(), name, definitions.get(name.casefold()))
  for group in document:
    if local(group.tag) == "ItemGroup" and holds(group):
      for item in (node for node in group if holds(node) and local(node.tag).casefold() == "clcompile"):
        metadata = dict(definitions)
        for meta in (node for node in item if holds(node)):
          name = local(meta.tag)
          metadata[name.casefold()] = inherit((meta.text or "").strip(), name, metadata.get(name.casefold()))
        for include in filter(None, (part.strip() for part in (item.get("Include") or "").split(";"))):
          if include.casefold().endswith(".cpp") and metadata.get("excludedfrombuild", "").casefold() != "true":
            sources.append((include, metadata))

  directory = _project.parent
  # Macros an include path may use: the project's own properties, $(SolutionDir) and $(ProjectDir) as a
  # solution build defines them, then the environment (VCInstallDir, from the Developer PowerShell).
  macros = {name.casefold(): value for name, value in os.environ.items()}
  macros.update(values)
  macros.update({"solutiondir": str(_root) + os.sep, "projectdir": str(directory) + os.sep})
  units: list[tuple[Path, list[str]]] = []
  for include, metadata in sources:
    switches = ["--driver-mode=cl", "/EHsc"]
    for key, table in (("languagestandard", STANDARDS), ("enableenhancedinstructionset", INSTRUCTION_SETS),
                       ("floatingpointmodel", FLOATING_POINT)):
      if metadata.get(key, "").casefold() in table:
        switches.append(table[metadata[key].casefold()])
    if metadata.get("conformancemode", "").casefold() == "true":
      switches.append("/permissive-")
    debug = values.get("usedebuglibraries", "").casefold() == "true"
    runtime = metadata.get("runtimelibrary", "MultiThreadedDebugDLL" if debug else "MultiThreadedDLL")
    switches.append(RUNTIMES.get(runtime.casefold(), "/MDd" if debug else "/MD"))
    character_set = values.get("characterset", "").casefold()
    defines = {"unicode": ["UNICODE", "_UNICODE"], "multibyte": ["_MBCS"]}.get(character_set, [])
    defines += [entry.strip() for entry in metadata.get("preprocessordefinitions", "").split(";")]
    switches += [f"/D{define}" for define in defines if define and not define.startswith("%(")]
    for entry in metadata.get("additionalincludedirectories", "").split(";"):
      entry = entry.strip()
      if entry and not entry.startswith("%("):
        path = Path(expand(entry, macros).replace("\\", os.sep))
        switches.append(f"/I{os.path.normpath(path if path.is_absolute() else directory / path)}")
    units.append((directory / include.replace("\\", os.sep), switches))
  return units


# ── Running ──────────────────────────────────────────────────────────────────────────────────

def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: the parent of Build/)")
  parser.add_argument("--clang-tidy", dest="clang_tidy", default="clang-tidy",
                      help="clang-tidy binary (default: clang-tidy on PATH)")
  parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="parallel clang-tidy processes "
                      "(default: one per CPU)")
  arguments = parser.parse_args()
  root = arguments.root.resolve()
  if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(errors="replace")  # a cp1252 console must not crash on a source line it echoes
  if not root.is_dir() or arguments.jobs < 1:
    print("error: --root must be a directory and --jobs at least 1", file=sys.stderr)
    return 2

  solutions = sorted(path for path in root.iterdir() if path.is_file() and path.suffix.casefold() in (".slnx", ".sln"))
  if len(solutions) > 1:
    print(f"error: more than one solution at the root: {', '.join(path.name for path in solutions)}", file=sys.stderr)
    return 2
  if not solutions:
    print("RunClangTidy: no solution at the repository root yet -- nothing to lint.")
    return 0

  units: list[tuple[Path, list[str]]] = []
  try:
    for project in solution_projects(solutions[0]):
      if not (root / project).is_file():
        print(f"note: {solutions[0].name} lists {project}, which does not exist (CheckProjectFiles reports it)")
        continue
      units += translation_units(root / project, root)
  except (ElementTree.ParseError, ValueError) as error:
    print(f"error: cannot read the projects of {solutions[0].name}: {error}", file=sys.stderr)
    return 2
  if not units:
    print(f"RunClangTidy: {solutions[0].name} builds no .cpp yet -- nothing to lint.")
    return 0

  if not os.environ.get("INCLUDE"):
    print("error: INCLUDE is not set, so clang-tidy cannot see the MSVC headers or the Windows SDK. Run this from a "
          "Developer PowerShell for Visual Studio (AGENTS.md §3).", file=sys.stderr)
    return 2
  config = root / ".clang-tidy"
  if not config.is_file():
    print(f"error: {config} does not exist", file=sys.stderr)
    return 2
  binary = shutil.which(arguments.clang_tidy)
  if binary is None:
    print(f"error: clang-tidy not found ('{arguments.clang_tidy}'). Install the pinned one with "
          f"`python -m pip install clang-tidy=={PINNED_VERSION}` or pass --clang-tidy <path>.", file=sys.stderr)
    return 2
  version = subprocess.run([binary, "--version"], capture_output=True, text=True).stdout
  version = next((line.strip() for line in version.splitlines() if "version" in line), version.strip())
  print(f"clang-tidy: {version} ({binary})")
  if PINNED_VERSION not in version:
    print(f"note: CI pins clang-tidy {PINNED_VERSION}; another version can report differently.")
  # Once, up front: a configuration this clang-tidy cannot read would otherwise fail every file alike.
  verify = subprocess.run([binary, f"--config-file={config}", "--verify-config"], capture_output=True, text=True)
  if verify.returncode != 0:
    print(f"error: this clang-tidy cannot use {config}:\n{(verify.stdout + verify.stderr).strip()}", file=sys.stderr)
    return 2

  def lint(_unit: tuple[Path, list[str]]) -> tuple[int, str]:
    source, switches = _unit
    result = subprocess.run([binary, "--quiet", f"--config-file={config}", str(source), "--", *switches], cwd=root,
                            capture_output=True, encoding="utf-8", errors="replace")
    return result.returncode, (result.stdout + result.stderr).strip()

  units.sort(key=lambda _unit: str(_unit[0]))
  failed = 0
  with concurrent.futures.ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
    for (source, _switches), (code, output) in zip(units, pool.map(lint, units), strict=True):
      if code != 0 or re.search(r"\b(warning|error):", output):
        print(f"\n== {Path(os.path.relpath(source, root)).as_posix()} (exit {code})\n{output}")
      failed += code != 0
  if failed:
    print(f"\nRunClangTidy: {failed} of {len(units)} file(s) failed (AGENTS.md §1).")
    return 1
  print(f"RunClangTidy: clean -- {len(units)} file(s).")
  return 0


if __name__ == "__main__":
  sys.exit(main())
