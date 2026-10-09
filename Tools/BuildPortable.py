#!/usr/bin/env python3
r"""Compile a project and the projects it references with GCC or Clang, outside Visual Studio (ADR-004).

  python Tools/BuildPortable.py PROJECT --out DIR [--compiler g++] [--configuration Release]
                                [--include DIR]... [--source FILE]... [--define NAME[=VALUE]]...
                                [--flag FLAG]... [--jobs N]

This is a development tool, not a build system. The solution is the build, and MSVC with /W4 /WX is
what gates (AGENTS.md §3). What this does is let a Linux session compile and run the parts of the tree
that are standard C++ -- `Machine`, `CpuConformance`, and later headless tools -- so that the reference
binary can be studied where it is being reverse-engineered (ADR-004 item 4). It proves the code
compiles and runs; it is not a second description of the product.

The .vcxproj stays the single source of truth. PROJECT is a .vcxproj path, or a project name looked up
as <root>/<Name>/<Name>.vcxproj. From it and from every project it reaches through ProjectReference,
this reads:

  ClCompile items              the sources, compiled each with its own project's settings
  AdditionalIncludeDirectories $(SolutionDir)<Project> becomes -I<root>/<Project>; %(...) is dropped,
                               and so is anything under $(VCInstallDir), which only MSVC has
  PreprocessorDefinitions      -D for each, from the unconditional item definitions and the ones for
                               --configuration|x64

It compiles at -std=c++23 with -Wall -Wextra -Wpedantic -Werror -Wconversion, -O2 -DNDEBUG for Release
or -O0 -g for Debug, links every object into DIR/<Project>, and prints that path. Objects go under
DIR/obj/<Project>/ and are rebuilt only when the source, a header it included (from -MMD) or the
command line changed. A precompiled header is not used: every .cpp includes "pch.h" first, which a
quoted include finds beside the .cpp, so it compiles as an ordinary header.

--include and --source add directories and sources that are not in the project, for a stand-in of
something only Visual Studio provides -- a CppUnitTest.h and a main() that runs the tests, say. They
belong outside the repository.

What it does not do: follow an Import, evaluate a <Choose>, read any other property, or build a
library. Exit status: 0 built, 1 a compile or link failed, 2 usage or project error.
"""

import argparse
import concurrent.futures
import hashlib
import os
import re
import shlex
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NAMESPACE = "{http://schemas.microsoft.com/developer/msbuild/2003}"
WARNINGS = ["-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wconversion"]
CONFIGURATIONS = {"Release": ["-O2", "-DNDEBUG"], "Debug": ["-O0", "-g"]}
CONDITION = re.compile(r"'\$\(Configuration\)\|\$\(Platform\)'\s*==\s*'([^|']+)\|([^']+)'")


class ProjectError(Exception):
  pass


@dataclass
class Project:
  name: str
  path: Path
  sources: list[Path] = field(default_factory=list)
  includes: list[Path] = field(default_factory=list)
  defines: list[str] = field(default_factory=list)
  references: list[Path] = field(default_factory=list)


def applies(_element: ElementTree.Element, _configuration: str) -> bool:
  """Whether an element's Condition holds for the configuration. Only the configuration test is understood."""
  condition = _element.get("Condition")
  if condition is None:
    return True
  match = CONDITION.fullmatch(condition.strip())
  if match is None:
    raise ProjectError(f"cannot evaluate Condition=\"{condition}\"")
  return match.group(1) == _configuration and match.group(2) == "x64"


def split_list(_value: str) -> list[str]:
  return [part.strip() for part in _value.split(";") if part.strip() and not part.strip().startswith("%(")]


def read_project(_path: Path, _configuration: str) -> Project:
  try:
    tree = ElementTree.parse(_path)
  except (OSError, ElementTree.ParseError) as error:
    raise ProjectError(f"{_path}: {error}") from error
  project = Project(_path.stem, _path)
  directory = _path.parent
  for group in tree.getroot().iter(f"{NAMESPACE}ItemGroup"):
    if not applies(group, _configuration):
      continue
    for item in group:
      if not applies(item, _configuration):
        continue
      include = item.get("Include", "").replace("\\", "/")
      if item.tag == f"{NAMESPACE}ClCompile":
        project.sources.append((directory / include).resolve())
      elif item.tag == f"{NAMESPACE}ProjectReference":
        project.references.append((directory / include).resolve())
  for definitions in tree.getroot().iter(f"{NAMESPACE}ItemDefinitionGroup"):
    if not applies(definitions, _configuration):
      continue
    for compile_settings in definitions.iter(f"{NAMESPACE}ClCompile"):
      for setting in compile_settings:
        if not applies(setting, _configuration):
          continue
        value = setting.text or ""
        if setting.tag == f"{NAMESPACE}AdditionalIncludeDirectories":
          for entry in split_list(value):
            if entry.startswith("$(VCInstallDir)"):
              continue  # the MSVC install: the unit-test framework, which --include stands in for
            if not entry.startswith("$(SolutionDir)") or "$(" in entry[len("$(SolutionDir)"):]:
              raise ProjectError(f"{_path}: cannot resolve the include directory {entry}")
            project.includes.append((ROOT / entry[len("$(SolutionDir)"):].replace("\\", "/")).resolve())
        elif setting.tag == f"{NAMESPACE}PreprocessorDefinitions":
          project.defines.extend(split_list(value))
  return project


def gather(_root: Path, _configuration: str) -> list[Project]:
  """The project and everything it references, each once, the root first."""
  ordered: list[Project] = []
  seen: set[Path] = set()
  pending = [_root.resolve()]
  while pending:
    path = pending.pop(0)
    if path in seen:
      continue
    seen.add(path)
    project = read_project(path, _configuration)
    ordered.append(project)
    pending.extend(project.references)
  return ordered


def needs_build(_object: Path, _depfile: Path, _stamp: Path, _command: list[str]) -> bool:
  digest = hashlib.sha256("\0".join(_command).encode()).hexdigest()
  if not _object.exists() or not _depfile.exists() or not _stamp.exists():
    return True
  if _stamp.read_text(encoding="utf-8") != digest:
    return True
  built = _object.stat().st_mtime
  text = _depfile.read_text(encoding="utf-8", errors="replace").replace("\\\n", " ")
  _, _, dependencies = text.partition(":")
  for dependency in shlex.split(dependencies):
    try:
      if Path(dependency).stat().st_mtime > built:
        return True
    except OSError:
      return True
  return False


def compile_one(_command: list[str], _object: Path, _depfile: Path, _stamp: Path) -> tuple[bool, str]:
  if not needs_build(_object, _depfile, _stamp, _command):
    return True, ""
  _object.parent.mkdir(parents=True, exist_ok=True)
  result = subprocess.run(_command + ["-MMD", "-MF", str(_depfile), "-c", "-o", str(_object)], capture_output=True,
                          text=True)
  if result.returncode == 0:
    _stamp.write_text(hashlib.sha256("\0".join(_command).encode()).hexdigest(), encoding="utf-8")
  return result.returncode == 0, (result.stdout + result.stderr).strip()


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("project", help="a .vcxproj, or a project name under the repository root")
  parser.add_argument("--out", type=Path, required=True, help="directory for objects and the executable")
  parser.add_argument("--compiler", default="g++", help="g++, clang++ or a path to either (default: g++)")
  parser.add_argument("--configuration", choices=sorted(CONFIGURATIONS), default="Release")
  parser.add_argument("--include", type=Path, action="append", default=[], help="an extra include directory")
  parser.add_argument("--source", type=Path, action="append", default=[], help="an extra source file")
  parser.add_argument("--define", action="append", default=[], help="an extra preprocessor definition")
  parser.add_argument("--flag", action="append", default=[], help="an extra compiler flag, e.g. --flag=-Wshadow")
  parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
  arguments = parser.parse_args()

  compiler = shutil.which(arguments.compiler)
  if compiler is None:
    print(f"error: no compiler '{arguments.compiler}'", file=sys.stderr)
    return 2
  project_path = Path(arguments.project)
  if project_path.suffix != ".vcxproj":
    project_path = ROOT / arguments.project / f"{arguments.project}.vcxproj"
  if not project_path.is_file():
    print(f"error: no project at {project_path}", file=sys.stderr)
    return 2
  try:
    projects = gather(project_path, arguments.configuration)
  except ProjectError as error:
    print(f"error: {error}", file=sys.stderr)
    return 2

  out = arguments.out.resolve()
  extra_includes = [path.resolve() for path in arguments.include]
  base = [compiler, "-std=c++23", *WARNINGS, *CONFIGURATIONS[arguments.configuration], *arguments.flag]
  jobs: list[tuple[str, list[str], Path, Path, Path]] = []
  for project in projects:
    flags = [*(f"-I{path}" for path in [*project.includes, *extra_includes]),
             *(f"-D{name}" for name in [*project.defines, *arguments.define])]
    sources = project.sources + ([path.resolve() for path in arguments.source] if project is projects[0] else [])
    for source in sources:
      if not source.is_file():
        print(f"error: {project.path.name} lists {source}, which does not exist", file=sys.stderr)
        return 2
      stem = out / "obj" / project.name / source.stem
      jobs.append((f"{project.name}/{source.name}", [*base, *flags, str(source)], stem.with_suffix(".o"),
                   stem.with_suffix(".d"), stem.with_suffix(".cmd")))

  failed = False
  objects: list[Path] = []
  with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as pool:
    futures = {pool.submit(compile_one, command, obj, dep, stamp): (label, obj)
               for label, command, obj, dep, stamp in jobs}
    for future in concurrent.futures.as_completed(futures):
      label, obj = futures[future]
      ok, output = future.result()
      objects.append(obj)
      if output:
        print(f"{label}:\n{output}")
      if not ok:
        failed = True
        print(f"FAILED {label}", file=sys.stderr)
  if failed:
    return 1

  executable = out / projects[0].name
  result = subprocess.run([compiler, "-o", str(executable), *sorted(map(str, objects))], capture_output=True, text=True)
  if result.returncode != 0:
    print((result.stdout + result.stderr).strip(), file=sys.stderr)
    print(f"FAILED linking {executable}", file=sys.stderr)
    return 1
  print(executable)
  return 0


if __name__ == "__main__":
  sys.exit(main())
