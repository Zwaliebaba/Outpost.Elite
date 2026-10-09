#!/usr/bin/env python3
r"""Check the build shape, the project registries and the source rules no compiler or linter can see.

  python Build/CheckProjectFiles.py [--root DIR]

Every file git would hand to a fresh clone is considered (tracked, plus untracked and not ignored), so
ignored build output is never scanned -- except that a build-output file somebody force-added is
reported. Each finding is printed as `path:line: RULE: message`. Exit status: 0 clean, 1 findings,
2 usage or environment error.

The rules, and the part of AGENTS.md each one enforces:

  build-output     §2       Nothing under x64/, .vs/ or a CompiledShader/ directory is tracked, and no
                            *.user file.
  solution         §3       At most one .slnx/.sln at the root; it lists every .vcxproj in the tree, and
                            every project it lists exists.
  x64-only         §3       Each project defines exactly Debug|x64 and Release|x64; the solution maps no
                            platform but x64.
  stated-settings  §3, R16  PlatformToolset v145, LanguageStandard stdcpplatest, ConformanceMode true,
                            WarningLevel Level4, TreatWarningAsError true, FloatingPointModel Precise and
                            EnableEnhancedInstructionSet AdvancedVectorExtensions2 are stated in the
                            project file for both configurations, and no ClCompile item overrides them.
  debug-release    §3       Debug|x64 and Release|x64 resolve every property, item definition, import and
                            item identically, except the optimization allowlist below and _DEBUG/NDEBUG.
  include-dirs     §3       AdditionalIncludeDirectories names only $(SolutionDir)<OtherProject> and
                            %(AdditionalIncludeDirectories).
  windows-macros   §4       NOMINMAX, WIN32_LEAN_AND_MEAN, NOMCX, NOSERVICE, NOHELP: in no project's
                            PreprocessorDefinitions, #defined by at most one header and by no .cpp.
  third-party      R14      No package-manager manifest, no PackageReference, no Import from packages\.
  flat-dirs        §2       .h/.cpp sit directly in a project directory. The only subdirectories are
                            Shader/ (.hlsl and .hlsli only) and CompiledShader/ (build output).
  file-names       R7, R11  .h/.cpp only; PascalCase stems apart from the Visual Studio wizard names;
                            shaders are <Name>VS.hlsl / <Name>PS.hlsl; shared HLSL is <Name>.hlsli.
  registration     §2       Every .h/.cpp is in its .vcxproj and its .filters, as the right item type;
                            every item exists; nothing is in two projects; every .hlsl is an FxCompile
                            item writing CompiledShader\<Stem>.h as g_<Stem> with the matching ShaderType;
                            every .hlsli is a None item, compiled only through the shaders that include it.
  filters          §2       No Source Files / Header Files / Resource Files filter; every item has a
                            filter; a .h and the .cpp of the same name share one.
  tidy-reach       §2       .clang-tidy's HeaderFilterRegex matches a header in every project directory.
  type-affix       §1 R2    No defined type named IFoo/CFoo/SFoo/EFoo, BaseFoo/AbstractFoo, or ending in
                            Base, Abstract, Impl or _t. Forward declarations are not checked.
  spelling         §1 R11   No identifier (C++ or HLSL) and no source file name uses a spelling from
                            BRITISH_SPELLINGS. Comments and strings are prose and are not checked.
  pragma-warning   §4       No #pragma warning(disable ...) or (suppress ...) in C++ or HLSL.
  project-xml      §2       A project, filters or solution file that cannot be parsed or evaluated.

What it does not do: follow an Import (a setting must be stated in the project to count, R16), or
evaluate a <Choose> block. A project written that way is reported against, never silently trusted.
"""

import argparse
import fnmatch
import json
import os
import posixpath
import re
import subprocess
import sys
import xml.parsers.expat
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parent.parent

RULES = {
  "build-output": "§2",
  "solution": "§3",
  "x64-only": "§3",
  "stated-settings": "§3, R16",
  "debug-release": "§3",
  "include-dirs": "§3",
  "windows-macros": "§4",
  "third-party": "R14",
  "flat-dirs": "§2",
  "file-names": "R7, R11",
  "registration": "§2",
  "filters": "§2",
  "tidy-reach": "§2",
  "type-affix": "§1 R2",
  "spelling": "§1 R11",
  "pragma-warning": "§4",
  "project-xml": "§2",
}

# R11: an identifier or a source file name spells these the SDK's way. Prose is not checked.
BRITISH_SPELLINGS = ("colour", "initialis", "serialis", "normalis", "quantis", "synchronis", "behaviour", "neighbour",
                     "centre", "grey", "cancelled")
# R4 outranks R11 for an SDK's own identifier, which keeps the SDK's spelling. Add one when it turns up.
SDK_IDENTIFIERS = frozenset({"ERROR_CANCELLED"})

CPP_EXTENSIONS = (".h", ".cpp")
BANNED_EXTENSIONS = (".hpp", ".hh", ".hxx", ".cc", ".cxx", ".inl", ".ipp")
WIZARD_NAMES = frozenset({"pch.h", "pch.cpp", "framework.h", "targetver.h", "Resource.h"})
PASCAL_CASE = re.compile(r"[A-Z][A-Za-z0-9]*")
SHADER_NAME = re.compile(r"[A-Z][A-Za-z0-9]*(VS|PS)")
SHADER_TYPES = {"VS": "Vertex", "PS": "Pixel"}
HLSL_EXTENSIONS = (".hlsl", ".hlsli")
BUILD_OUTPUT_DIRS = frozenset({"x64", ".vs", "compiledshader"})

CONFIGURATIONS = ("Debug|x64", "Release|x64")
REQUIRED_PROPERTIES = {"PlatformToolset": "v145"}
REQUIRED_COMPILE_SETTINGS = {
  "LanguageStandard": "stdcpplatest",
  "ConformanceMode": "true",
  "WarningLevel": "Level4",
  "TreatWarningAsError": "true",
  "FloatingPointModel": "Precise",
  "EnableEnhancedInstructionSet": "AdvancedVectorExtensions2",
}
# §3: the whole of what may differ between Debug and Release, besides _DEBUG versus NDEBUG.
OPTIMIZATION_SETTINGS = frozenset(name.casefold() for name in (
  "Optimization", "FunctionLevelLinking", "IntrinsicFunctions", "UseDebugLibraries", "RuntimeLibrary",
  "LinkIncremental", "WholeProgramOptimization", "EnableCOMDATFolding", "OptimizeReferences",
  "LinkTimeCodeGeneration"))

WINDOWS_MACROS = ("NOMINMAX", "WIN32_LEAN_AND_MEAN", "NOMCX", "NOSERVICE", "NOHELP")
DEFINES_WINDOWS_MACRO = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(" + "|".join(WINDOWS_MACROS) + r")\b", re.M)
SILENCED_WARNING = re.compile(r"(?:#[ \t]*pragma[ \t]+warning|__pragma[ \t]*\([ \t]*warning)[ \t]*\([^)\n]*"
                              r"\b(disable|suppress)[ \t]*:")
PACKAGE_MANIFESTS = ("packages.config", "vcpkg.json", "vcpkg-configuration.json", "directory.packages.props",
                     "conanfile.*")
PACKAGES_PATH = re.compile(r"(^|[\\/])packages[\\/]", re.I)
DEFAULT_FILTERS = frozenset({"source files", "header files", "resource files"})

# The Visual Studio unit-test framework ships inside the MSVC install, and a native test project
# cannot include <CppUnitTest.h> without this directory. Allowed in a *Tests project and nowhere else.
UNIT_TEST_INCLUDE = r"$(VCInstallDir)Auxiliary\VS\UnitTest\include"
# Item types whose Include is not a file in the tree.
NON_FILE_ITEMS = frozenset({"projectconfiguration", "filter", "packagereference", "projectcapability"})
REGISTERED_ITEMS = ("clcompile", "clinclude", "fxcompile", "none")
ITEM_ATTRIBUTES = frozenset({"Include", "Exclude", "Remove", "Update", "Condition", "KeepMetadata", "RemoveMetadata",
                             "KeepDuplicates", "MatchOnMetadata", "MatchOnMetadataOptions", "Label"})
# Where FXC may write <Lib>/CompiledShader/<Stem>.h: the project directory, spelled any of these ways.
HEADER_OUTPUT_PREFIXES = ("", "$(projectdir)", "$(msbuildprojectdirectory)/", "$(msbuildthisfiledirectory)")


# ── Findings ─────────────────────────────────────────────────────────────────────────────────

@dataclass(frozen=True, order=True)
class Finding:
  path: str
  line: int
  rule: str
  message: str

  def __str__(self) -> str:
    where = f"{self.path}:{self.line}" if self.line else self.path
    return f"{where}: {self.rule}: {self.message}"


# ── Files ────────────────────────────────────────────────────────────────────────────────────

def list_files(_root: Path) -> list[str]:
  """Every file a clone would have, relative to the root with forward slashes, sorted."""
  try:
    result = subprocess.run(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=_root,
                            capture_output=True, check=True)
    names = result.stdout.decode("utf-8", errors="surrogateescape").split("\0")
  except (OSError, subprocess.CalledProcessError):
    print("note: git is not available here, so the tree is walked instead and build output is skipped by name")
    names = []
    for directory, subdirs, files in os.walk(_root):
      subdirs[:] = [name for name in subdirs if name.casefold() not in BUILD_OUTPUT_DIRS | {".git", "__pycache__"}]
      names.extend((Path(directory) / name).relative_to(_root).as_posix() for name in files
                   if not name.casefold().endswith(".user"))
  return sorted({name for name in names if name and (_root / name).is_file()})


def is_build_output(_path: str) -> bool:
  parts = _path.split("/")
  return any(part.casefold() in BUILD_OUTPUT_DIRS for part in parts[:-1]) or parts[-1].casefold().endswith(".user")


def read_text(_path: Path) -> str:
  return _path.read_text(encoding="utf-8-sig", errors="replace")


def line_of(_text: str, _offset: int) -> int:
  return _text.count("\n", 0, _offset) + 1


# ── C++ and HLSL source ──────────────────────────────────────────────────────────────────────

IDENTIFIER = re.compile(r"[^\W\d]\w*")
STRING_PREFIXES = frozenset({"u8", "u", "U", "L"})
RAW_STRING_PREFIXES = frozenset({"R", "u8R", "uR", "UR", "LR"})


def blank(_text: str) -> str:
  """The same text with everything but its line breaks turned to spaces, so offsets and lines survive."""
  return re.sub(r"[^\n]", " ", _text)


def scan_source(_text: str) -> tuple[str, list[tuple[str, int]]]:
  """Strip comments and literals from C++ or HLSL.

  Returns the source with every comment and string/character literal blanked (line breaks kept), and
  the token stream -- identifiers and punctuation, with their lines -- that the R2 and R11 rules read.
  """
  out: list[str] = []
  tokens: list[tuple[str, int]] = []
  index, line, length = 0, 1, len(_text)
  while index < length:
    char = _text[index]
    if char == "\n":
      out.append(char)
      line += 1
      index += 1
      continue
    if _text.startswith("//", index):
      end = _text.find("\n", index)
      end = length if end < 0 else end
    elif _text.startswith("/*", index):
      end = _text.find("*/", index + 2)
      end = length if end < 0 else end + 2
    elif char == '"' or char == "'":
      end = index + 1
      while end < length and _text[end] != char and _text[end] != "\n":
        end += 2 if _text[end] == "\\" else 1
      end = min(end + 1, length)
    elif char.isdigit() or (char == "." and index + 1 < length and _text[index + 1].isdigit()):
      # A pp-number, so that a digit separator (1'000) is not read as a character literal.
      end = index + 1
      while end < length and (_text[end].isalnum() or _text[end] in "_."
                              or (_text[end] == "'" and end + 1 < length and _text[end + 1].isalnum())
                              or (_text[end] in "+-" and _text[end - 1] in "eEpP")):
        end += 1
      out.append(_text[index:end])
      index = end
      continue
    elif (match := IDENTIFIER.match(_text, index)) is not None:
      word, end = match.group(), match.end()
      if end < length and _text[end] == '"' and word in RAW_STRING_PREFIXES:
        open_paren = _text.find("(", end)
        delimiter = _text[end + 1:open_paren] if open_paren >= 0 else ""
        close = _text.find(")" + delimiter + '"', max(open_paren, end))
        end = length if open_paren < 0 or close < 0 else close + len(delimiter) + 2
        out.append(word + blank(_text[index + len(word):end]))
        line += _text.count("\n", index, end)
        index = end
        continue
      if not (end < length and _text[end] in "\"'" and word in STRING_PREFIXES):
        tokens.append((word, line))
      out.append(word)
      index = end
      continue
    else:
      if _text.startswith("::", index):
        tokens.append(("::", line))
        out.append("::")
        index += 2
        continue
      if not char.isspace():
        tokens.append((char, line))
      out.append(char)
      index += 1
      continue
    # A comment or a literal: blanked, line breaks kept.
    out.append(blank(_text[index:end]))
    line += _text.count("\n", index, end)
    index = end
  return "".join(out), tokens


def skip_balanced(_tokens: list[tuple[str, int]], _start: int, _open: str, _close: str) -> int:
  """The index just past the bracket that closes the one at _start."""
  depth = 0
  for index in range(_start, len(_tokens)):
    if _tokens[index][0] == _open:
      depth += 1
    elif _tokens[index][0] == _close:
      depth -= 1
      if depth == 0:
        return index + 1
  return len(_tokens)


def type_definitions(_tokens: list[tuple[str, int]]) -> list[tuple[str, int]]:
  """(name, line) for every class, struct, union, enum, alias and concept the source DEFINES.

  A forward declaration (`struct ID3D12Device;`) and an elaborated type (`struct Foo* p`) are not
  definitions: a class-key counts only when its name is followed, after any template arguments,
  `final` and base clause, by `{`.
  """
  def text(_index: int) -> str:
    return _tokens[_index][0] if 0 <= _index < len(_tokens) else ""

  found: list[tuple[str, int]] = []
  for index, (token, _line) in enumerate(_tokens):
    if token in ("using", "concept"):
      name = text(index + 1)
      if name != "namespace" and IDENTIFIER.fullmatch(name) and text(index + 2) == "=":
        found.append(_tokens[index + 1])
      continue
    if token not in ("class", "struct", "union", "enum") or (token != "enum" and text(index - 1) == "enum"):
      continue
    cursor = index + 1
    if token == "enum" and text(cursor) in ("class", "struct"):
      cursor += 1
    while True:
      if text(cursor) == "[" and text(cursor + 1) == "[":
        cursor = skip_balanced(_tokens, cursor, "[", "]")
      elif text(cursor) in ("alignas", "__declspec", "__attribute__") and text(cursor + 1) == "(":
        cursor = skip_balanced(_tokens, cursor + 1, "(", ")")
      else:
        break
    if not IDENTIFIER.fullmatch(text(cursor)):
      continue
    name = _tokens[cursor]
    cursor += 1
    while text(cursor) == "::" and IDENTIFIER.fullmatch(text(cursor + 1)):
      name = _tokens[cursor + 1]
      cursor += 2
    if text(cursor) == "<":
      cursor = skip_balanced(_tokens, cursor, "<", ">")
    if text(cursor) == "final":
      cursor += 1
    if text(cursor) == ":":
      while cursor < len(_tokens) and text(cursor) not in ("{", ";", "}"):
        cursor += 1
    if text(cursor) == "{":
      found.append(name)
  return found


def affix_problem(_name: str) -> str | None:
  """Why R2 rejects a type name, or None."""
  if re.match(r"[ICSE][A-Z]", _name):
    return f"starts with the '{_name[0]}' prefix"
  prefix = re.match(r"(Base|Abstract)[A-Z]", _name)
  if prefix:
    return f"starts with '{prefix.group(1)}'"
  for suffix in ("Base", "Abstract", "Impl", "_t"):
    if _name.endswith(suffix):
      return f"ends in '{suffix}'"
  return None


def british_spelling(_word: str) -> str | None:
  folded = _word.casefold()
  return next((spelling for spelling in BRITISH_SPELLINGS if spelling in folded), None)


# ── MSBuild ──────────────────────────────────────────────────────────────────────────────────

@dataclass
class Node:
  """An XML element with the line it starts on, which ElementTree does not keep."""
  tag: str
  attrib: dict[str, str]
  line: int
  children: list["Node"] = field(default_factory=list)
  text: str = ""

  def walk(self) -> list["Node"]:
    nodes = [self]
    for child in self.children:
      nodes.extend(child.walk())
    return nodes

  def value(self) -> str:
    return self.text.strip()


def parse_xml(_path: Path) -> Node:
  parser = xml.parsers.expat.ParserCreate()
  stack: list[Node] = []
  roots: list[Node] = []

  def start(_tag: str, _attrib: dict[str, str]) -> None:
    node = Node(_tag.rsplit(":", 1)[-1], _attrib, parser.CurrentLineNumber)
    (stack[-1].children if stack else roots).append(node)
    stack.append(node)

  def end(_tag: str) -> None:
    stack.pop()

  def data(_text: str) -> None:
    if stack:
      stack[-1].text += _text

  parser.StartElementHandler = start
  parser.EndElementHandler = end
  parser.CharacterDataHandler = data
  with _path.open("rb") as stream:
    parser.ParseFile(stream)
  return roots[0]


CONDITION_TOKEN = re.compile(r"\s*('[^']*'|==|!=|<=|>=|<|>|\(|\)|,|!|\$\([^)]*\)|[A-Za-z_][\w.:]*|\d[\w.]*)")


class Condition:
  """An MSBuild Condition, evaluated the way MSBuild does for the forms a .vcxproj uses.

  Undefined properties expand to the empty string, as in MSBuild, and Exists() is false: imports are
  not followed, so nothing that depends on one can count as stated.
  """

  def __init__(self, _text: str, _properties: dict[str, str]) -> None:
    self.text = _text
    self.properties = _properties
    self.tokens: list[str] = []
    position = 0
    while position < len(_text):
      match = CONDITION_TOKEN.match(_text, position)
      if match is None:
        if _text[position:].strip():
          raise ValueError(f"cannot read the condition {_text!r}")
        break
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

  def expand(self, _text: str) -> str:
    return re.sub(r"\$\(([A-Za-z_]\w*)\)", lambda _match: self.properties.get(_match.group(1).casefold(), ""), _text)

  def truth(self, _value: bool | str) -> bool:
    if isinstance(_value, bool):
      return _value
    if _value.casefold() in ("true", "on", "yes"):
      return True
    if _value.casefold() in ("false", "off", "no"):
      return False
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
        same = str(left).casefold() == str(right).casefold()
        return same if operator == "==" else not same
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
    if token.startswith("'"):
      return self.expand(token[1:-1])
    if token.startswith("$("):
      return self.expand(token)
    if self.peek() == "(":
      self.take()
      arguments: list[str] = []
      while self.peek() != ")":
        arguments.append(str(self.operand()))
        if self.peek() == ",":
          self.take()
      self.take()
      if token.casefold() == "exists":
        return False
      if token.casefold() == "hastrailingslash":
        return bool(arguments) and arguments[0].endswith(("\\", "/"))
      raise ValueError(f"the condition {self.text!r} calls {token}(), which this checker does not evaluate")
    return token


@dataclass
class Item:
  kind: str
  include: str
  line: int
  metadata: dict[str, tuple[str, str, int]]  # casefold name -> (name, value, line), definitions included
  own: dict[str, tuple[str, str, int]]       # the metadata stated on the item itself


@dataclass
class Evaluation:
  """A project as MSBuild resolves it for one configuration, from the project file alone."""
  properties: dict[str, tuple[str, str, int]] = field(default_factory=dict)            # name -> (name, raw, line)
  definitions: dict[tuple[str, str], tuple[str, str, int]] = field(default_factory=dict)  # -> (label, raw, line)
  imports: dict[str, int] = field(default_factory=dict)
  items: list[Item] = field(default_factory=list)


def inherit(_value: str, _name: str, _previous: str | None) -> str:
  """Expand %(Name) inside Name's own value to what it was before, as MSBuild does."""
  if _previous is None:
    return _value
  return re.sub(r"%\(\s*" + re.escape(_name) + r"\s*\)", lambda _match: _previous, _value, flags=re.I)


def evaluate(_project: Node, _configuration: str) -> Evaluation:
  """Properties and imports first, then item definitions, then items -- MSBuild's own order."""
  name, platform = _configuration.split("|")
  values = {"configuration": name, "platform": platform}
  result = Evaluation()

  def holds(_node: Node) -> bool:
    condition = _node.attrib.get("Condition", "").strip()
    return not condition or Condition(condition, values).evaluate()

  for group in _project.children:
    if group.tag == "PropertyGroup" and holds(group):
      for prop in group.children:
        if holds(prop):
          raw = prop.value()
          result.properties[prop.tag.casefold()] = (prop.tag, raw, prop.line)
          values[prop.tag.casefold()] = Condition("", values).expand(raw)
    elif group.tag in ("ImportGroup", "Import") and holds(group):
      for node in group.children if group.tag == "ImportGroup" else [group]:
        if node.tag == "Import" and holds(node):
          result.imports[node.attrib.get("Project", "")] = node.line

  for group in _project.children:
    if group.tag == "ItemDefinitionGroup" and holds(group):
      for kind in (node for node in group.children if holds(node)):
        for meta in (node for node in kind.children if holds(node)):
          key = (kind.tag.casefold(), meta.tag.casefold())
          previous = result.definitions.get(key)
          value = inherit(meta.value(), meta.tag, previous[1] if previous else None)
          result.definitions[key] = (f"{kind.tag}.{meta.tag}", value, meta.line)

  for group in _project.children:
    if group.tag == "ItemGroup" and holds(group):
      for node in (node for node in group.children if holds(node)):
        defined = {meta: (label.split(".", 1)[1], value, line)
                   for (kind, meta), (label, value, line) in result.definitions.items() if kind == node.tag.casefold()}
        own: dict[str, tuple[str, str, int]] = {}
        stated = [(key, value, node.line) for key, value in node.attrib.items() if key not in ITEM_ATTRIBUTES]
        stated += [(meta.tag, meta.value(), meta.line) for meta in node.children if holds(meta)]
        for meta, value, line in stated:
          previous = own.get(meta.casefold()) or defined.get(meta.casefold())
          own[meta.casefold()] = (meta, inherit(value, meta, previous[1] if previous else None), line)
        for include in filter(None, (part.strip() for part in node.attrib.get("Include", "").split(";"))):
          result.items.append(Item(node.tag, include, node.line, {**defined, **own}, own))
  return result


def comparable(_evaluation: Evaluation) -> dict[str, tuple[str, str, str, int]]:
  """Every setting of one configuration, keyed for comparison: key -> (label, setting name, value, line)."""
  settings: dict[str, tuple[str, str, str, int]] = {}
  for key, (name, value, line) in _evaluation.properties.items():
    settings[f"property {key}"] = (f"property {name}", name, value, line)
  for project, line in _evaluation.imports.items():
    settings[f"import {project.casefold()}"] = (f"Import of {project}", "", "imported", line)
  for (kind, meta), (label, value, line) in _evaluation.definitions.items():
    settings[f"definition {kind}.{meta}"] = (label, label.split(".", 1)[1], value, line)
  for item in _evaluation.items:
    key = f"item {item.kind.casefold()} {item.include.casefold()}"
    settings[key] = (f"{item.kind} {item.include}", "", "present", item.line)
    for meta, (name, value, line) in item.own.items():
      settings[f"{key} {meta}"] = (f"{item.kind} {item.include} {name}", name, value, line)
  return settings


def only_debug_versus_ndebug(_debug: str, _release: str) -> bool:
  """True when the two definition lists differ by _DEBUG in Debug standing where NDEBUG stands in Release."""
  debug = [entry.strip() for entry in _debug.split(";") if entry.strip()]
  release = [entry.strip() for entry in _release.split(";") if entry.strip()]
  return "_DEBUG" in debug and ["NDEBUG" if entry == "_DEBUG" else entry for entry in debug] == release


def shader_metadata(_item: Item, _stem: str, _name: str) -> str | None:
  """An FxCompile item's metadata with %(Filename) expanded, or None when it is not set."""
  found = _item.metadata.get(_name.casefold())
  return re.sub(r"%\(\s*Filename\s*\)", lambda _match: _stem, found[1], flags=re.I) if found else None


def resolve_include(_directory: str, _include: str) -> str:
  """An item's Include as MSBuild resolves it: relative to the project's own directory."""
  return posixpath.normpath(posixpath.join(_directory, _include.strip().replace("\\", "/")))


def header_filter(_root: Path) -> str | None:
  """HeaderFilterRegex from the root .clang-tidy, unquoted as YAML would; None when it is not there."""
  try:
    text = (_root / ".clang-tidy").read_text(encoding="utf-8-sig")
  except OSError:
    return None
  match = re.search(r"^HeaderFilterRegex:[ \t]*(.*?)[ \t]*$", text, re.M)
  if match is None:
    return None
  value = match.group(1)
  if len(value) >= 2 and value[0] == value[-1] == "'":
    return value[1:-1].replace("''", "'")
  if len(value) >= 2 and value[0] == value[-1] == '"':
    try:
      return json.loads(value)
    except ValueError:
      return None
  return value.split(" #", 1)[0].strip()


# ── The checker ──────────────────────────────────────────────────────────────────────────────

@dataclass
class Project:
  path: str
  directory: str
  name: str
  xml: Node | None = None
  filters: Node | None = None
  evaluations: dict[str, Evaluation] = field(default_factory=dict)

  def items(self, _node: Node | None, _kinds: tuple[str, ...] = ()) -> list[tuple[Node, str]]:
    """(element, resolved path) for each item in the project or filters file, ignoring conditions."""
    found: list[tuple[Node, str]] = []
    for group in (_node.children if _node is not None else []):
      if group.tag != "ItemGroup":
        continue
      for node in group.children:
        if _kinds and node.tag.casefold() not in _kinds:
          continue
        for include in filter(None, (part.strip() for part in node.attrib.get("Include", "").split(";"))):
          found.append((node, resolve_include(self.directory, include)))
    return found


class Checker:
  def __init__(self, _root: Path) -> None:
    self.root = _root
    self.files = list_files(_root)
    self.known = {path.casefold() for path in self.files}
    self.sources = [path for path in self.files if not is_build_output(path)]
    self.findings: set[Finding] = set()
    self.projects: list[Project] = []
    self.project_dirs: dict[str, Project] = {}

  def add(self, _path: str, _line: int, _rule: str, _message: str) -> None:
    self.findings.add(Finding(_path, _line, _rule, _message))

  def exists(self, _path: str) -> bool:
    return _path.casefold() in self.known or (self.root / _path).is_file()

  def owner(self, _path: str) -> Project | None:
    """The project whose directory holds the file, the deepest one if they nest."""
    for parent in PurePosixPath(_path).parents:
      if str(parent) in self.project_dirs:
        return self.project_dirs[str(parent)]
    return None

  # ── Loading ──

  def load(self) -> None:
    for path in self.sources:
      if not path.casefold().endswith(".vcxproj"):
        continue
      project = Project(path, str(PurePosixPath(path).parent), PurePosixPath(path).stem)
      self.projects.append(project)
      self.project_dirs.setdefault(project.directory, project)
      project.xml = self.parse(path)
      if project.xml is not None:
        try:
          project.evaluations = {configuration: evaluate(project.xml, configuration)
                                 for configuration in CONFIGURATIONS}
        except ValueError as error:
          self.add(path, 0, "project-xml", str(error))
      filters = path + ".filters"
      if self.exists(filters):
        project.filters = self.parse(filters)
      else:
        self.add(path, 0, "registration", f"has no {PurePosixPath(filters).name} beside it")

  def parse(self, _path: str) -> Node | None:
    try:
      return parse_xml(self.root / _path)
    except xml.parsers.expat.ExpatError as error:
      self.add(_path, error.lineno, "project-xml",
               f"is not well-formed XML: {xml.parsers.expat.ErrorString(error.code)}")
    except OSError as error:
      self.add(_path, 0, "project-xml", f"cannot be read: {error.strerror}")
    return None

  # ── Whole-tree rules ──

  def check_build_output(self) -> None:
    for path in self.files:
      if is_build_output(path):
        self.add(path, 0, "build-output",
                 "is build or IDE output and must not be committed (x64/, .vs/, CompiledShader/, *.user)")

  def check_third_party_files(self) -> None:
    for path in self.sources:
      name = PurePosixPath(path).name.casefold()
      if any(fnmatch.fnmatchcase(name, pattern) for pattern in PACKAGE_MANIFESTS):
        self.add(path, 0, "third-party",
                 "is a package-manager file; the build depends on the Windows SDK and MSVC only")

  def check_layout_and_names(self) -> None:
    for path in self.sources:
      pure = PurePosixPath(path)
      suffix = pure.suffix.casefold()
      owner = self.owner(path)
      inside = pure.relative_to(owner.directory).parts if owner else ()
      if suffix in BANNED_EXTENSIONS:
        self.add(path, 0, "file-names", f"uses '{pure.suffix}'; C++ files are .h or .cpp only (R7)")
      elif suffix in CPP_EXTENSIONS and pure.suffix != suffix:
        self.add(path, 0, "file-names", f"uses '{pure.suffix}'; spell it '{suffix}' (R7)")
      if suffix in CPP_EXTENSIONS and pure.name not in WIZARD_NAMES and not PASCAL_CASE.fullmatch(pure.stem):
        self.add(path, 0, "file-names", "is not PascalCase; a file is named for its primary type (R7)")
      if suffix == ".hlsl" and not SHADER_NAME.fullmatch(pure.stem):
        self.add(path, 0, "file-names", "is not named <Name>VS.hlsl or <Name>PS.hlsl with a PascalCase <Name>")
      if suffix == ".hlsli" and not PASCAL_CASE.fullmatch(pure.stem):
        self.add(path, 0, "file-names", "is not named <Name>.hlsli with a PascalCase <Name>")
      if suffix in CPP_EXTENSIONS + BANNED_EXTENSIONS + HLSL_EXTENSIONS + (".vcxproj", ".sln", ".slnx"):
        spelling = british_spelling(pure.name)
        if spelling:
          self.add(path, 0, "spelling", f"the file name uses '{spelling}'; identifiers and source names use the SDK's "
                   "spelling (R11)")

      if suffix in CPP_EXTENSIONS + BANNED_EXTENSIONS:
        if owner is None:
          self.add(path, 0, "flat-dirs", "is C++ outside every project directory")
        elif len(inside) > 1:
          self.add(path, 0, "flat-dirs",
                   f"is in a subdirectory of {owner.path}; C++ sits directly in the project folder")
      elif suffix in HLSL_EXTENSIONS:
        if owner is None or len(inside) != 2 or inside[0] != "Shader":
          self.add(path, 0, "flat-dirs", "is HLSL outside a project's Shader/ directory")
      elif owner is not None and len(inside) > 1:
        if inside[0] == "Shader":
          self.add(path, 0, "flat-dirs", "is not .hlsl or .hlsli; Shader/ holds HLSL and nothing else")
        else:
          self.add(path, 0, "flat-dirs", f"is in '{inside[0]}/' inside {owner.directory}/; a project's only "
                   "subdirectories are Shader/ and CompiledShader/")

  def check_sources(self) -> None:
    owners: dict[str, tuple[str, int]] = {}
    for path in self.sources:
      suffix = PurePosixPath(path).suffix.casefold()
      if suffix not in CPP_EXTENSIONS + BANNED_EXTENSIONS + HLSL_EXTENSIONS:
        continue
      stripped, tokens = scan_source(read_text(self.root / path))
      for match in SILENCED_WARNING.finditer(stripped):
        self.add(path, line_of(stripped, match.start()), "pragma-warning",
                 f"a warning({match.group(1)}: ...) pragma silences a diagnostic; fix the cause or report it")
      seen: dict[str, int] = {}
      for token, line in tokens:
        if token not in seen and token not in SDK_IDENTIFIERS and british_spelling(token):
          seen[token] = line
      for token, line in seen.items():
        self.add(path, line, "spelling", f"identifier '{token}' uses '{british_spelling(token)}'; identifiers use the "
                 "SDK's spelling (R11)")
      if suffix in HLSL_EXTENSIONS:
        continue
      for name, line in type_definitions(tokens):
        problem = affix_problem(name)
        if problem:
          self.add(path, line, "type-affix", f"type '{name}' {problem}; a type name carries no prefix or affix (R2)")
      for match in DEFINES_WINDOWS_MACRO.finditer(stripped):
        if suffix != ".h":
          self.add(path, line_of(stripped, match.start()), "windows-macros",
                   f"defines {match.group(1)}; only the one header that owns the Windows macros may")
        elif path not in owners:
          owners[path] = (match.group(1), line_of(stripped, match.start()))
    if len(owners) > 1:
      for path, (macro, line) in owners.items():
        others = ", ".join(header for header in owners if header != path)
        self.add(path, line, "windows-macros", f"defines {macro}, and {others} defines Windows macros too; one header "
                 "owns the family")

  # ── Solution ──

  def check_solution(self) -> None:
    solutions = [path for path in self.sources if "/" not in path and path.casefold().endswith((".sln", ".slnx"))]
    if len(solutions) > 1:
      for path in solutions:
        self.add(path, 0, "solution", f"is one of {len(solutions)} solutions at the root; there is exactly one")
      return
    if not solutions:
      if self.projects:
        self.add(".", 0, "solution", f"there are {len(self.projects)} project(s) and no .slnx or .sln at the root")
      return
    path = solutions[0]
    listed = self.read_slnx(path) if path.casefold().endswith(".slnx") else self.read_sln(path)
    names = set()
    for project, line in listed:
      names.add(project.casefold())
      if not self.exists(project):
        self.add(path, line, "solution", f"lists {project}, which does not exist")
    for project in self.projects:
      if project.path.casefold() not in names:
        self.add(project.path, 0, "solution", f"is not listed in {path}")

  def read_slnx(self, _path: str) -> list[tuple[str, int]]:
    node = self.parse(_path)
    if node is None:
      return []
    listed: list[tuple[str, int]] = []
    for element in node.walk():
      if element.tag == "Project" and element.attrib.get("Path"):
        listed.append((posixpath.normpath(element.attrib["Path"].replace("\\", "/")), element.line))
      if element.tag == "Platform":
        platforms = [element.attrib.get("Name", ""), element.attrib.get("Project", "")]
        platforms.append(element.attrib.get("Solution", "").rpartition("|")[2].replace("*", ""))
        for platform in filter(None, platforms):
          if platform.casefold() != "x64":
            self.add(_path, element.line, "x64-only", f"maps the platform '{platform}'; x64 is the only platform")
    return listed

  def read_sln(self, _path: str) -> list[tuple[str, int]]:
    listed: list[tuple[str, int]] = []
    section = ""
    for number, line in enumerate(read_text(self.root / _path).splitlines(), start=1):
      project = re.match(r'\s*Project\("\{([^}]*)\}"\)\s*=\s*"[^"]*",\s*"([^"]*)"', line)
      if project and project.group(1).upper() != "2150E333-8FDC-42A3-9474-1A3956D46DE8":  # not a solution folder
        listed.append((posixpath.normpath(project.group(2).replace("\\", "/")), number))
      section_start = re.match(r"\s*GlobalSection\((\w+)\)", line)
      if section_start:
        section = section_start.group(1)
      elif line.strip() == "EndGlobalSection":
        section = ""
      elif section in ("SolutionConfigurationPlatforms", "ProjectConfigurationPlatforms") and "=" in line:
        left, _equals, right = line.partition("=")
        left = re.sub(r"\.(ActiveCfg|Build\.0|Deploy\.0)\s*$", "", re.sub(r"^\s*\{[^}]*\}\.", "", left.strip()))
        for side in (left.strip(), right.strip()):
          platform = side.rpartition("|")[2] if "|" in side else ""
          if platform and platform.casefold() != "x64":
            self.add(_path, number, "x64-only", f"maps the platform '{platform}'; x64 is the only platform")
    return listed

  # ── Per project ──

  def check_project(self, _project: Project) -> None:
    if _project.xml is None:
      return
    self.check_configurations(_project)
    self.check_raw_settings(_project)
    if _project.evaluations:
      self.check_stated_settings(_project)
      self.check_alignment(_project)
      self.check_shaders(_project)
    self.check_registration(_project)
    if _project.filters is not None:
      self.check_filters(_project)

  def check_configurations(self, _project: Project) -> None:
    declared = {node.attrib.get("Include", ""): node.line for node in _project.xml.walk()
                if node.tag == "ProjectConfiguration"}
    for configuration, line in sorted(declared.items()):
      if configuration.casefold() not in (wanted.casefold() for wanted in CONFIGURATIONS):
        self.add(_project.path, line, "x64-only", f"defines the configuration '{configuration}'; a project defines "
                 "Debug|x64 and Release|x64 and nothing else")
    for wanted in CONFIGURATIONS:
      if wanted.casefold() not in (configuration.casefold() for configuration in declared):
        self.add(_project.path, 0, "x64-only", f"does not define the configuration {wanted}")

  def check_raw_settings(self, _project: Project) -> None:
    """The rules that read what the file says, whatever configuration it says it for."""
    for node in _project.xml.walk():
      tag = node.tag.casefold()
      if tag == "additionalincludedirectories":
        self.check_include_dirs(_project, node)
      elif tag == "preprocessordefinitions":
        for entry in node.value().split(";"):
          macro = entry.split("=", 1)[0].strip()
          if macro in WINDOWS_MACROS:
            self.add(_project.path, node.line, "windows-macros", f"defines {macro}; the one header that owns the "
                     "Windows macros sets it, and a project file sets none of them")
      elif tag == "packagereference":
        self.add(_project.path, node.line, "third-party",
                 f"has a PackageReference to {node.attrib.get('Include', '?')}")
      elif tag == "import" and PACKAGES_PATH.search(node.attrib.get("Project", "")):
        self.add(_project.path, node.line, "third-party",
                 f"imports {node.attrib['Project']} from a NuGet packages folder")

  def check_include_dirs(self, _project: Project, _node: Node) -> None:
    for entry in filter(None, (part.strip() for part in _node.value().split(";"))):
      normalized = entry.replace("/", "\\").rstrip("\\")
      if re.fullmatch(r"%\(\s*AdditionalIncludeDirectories\s*\)", normalized, re.I):
        continue
      if _project.name.endswith("Tests") and normalized.casefold() == UNIT_TEST_INCLUDE.casefold():
        continue
      target = re.fullmatch(r"\$\(SolutionDir\)[\\]*(.+)", normalized, re.I)
      if target:
        directory = posixpath.normpath(target.group(1).replace("\\", "/"))
        if directory.casefold() == _project.directory.casefold():
          self.add(_project.path, _node.line, "include-dirs", f"lists its own directory ({entry}); a quoted include "
                   "already searches it")
          continue
        if any(directory.casefold() == other.casefold() for other in self.project_dirs):
          continue
      self.add(_project.path, _node.line, "include-dirs", f"lists '{entry}'; only $(SolutionDir)<OtherProject> and "
               "%(AdditionalIncludeDirectories) are allowed")

  def check_stated_settings(self, _project: Project) -> None:
    problems: dict[str, list[str]] = {}
    for configuration, evaluation in _project.evaluations.items():
      stated = [(name, required, evaluation.properties.get(name.casefold())) for name, required in
                REQUIRED_PROPERTIES.items()]
      stated += [(name, required, evaluation.definitions.get(("clcompile", name.casefold()))) for name, required in
                 REQUIRED_COMPILE_SETTINGS.items()]
      for name, required, found in stated:
        if found is None:
          problems.setdefault(f"{name} is not stated; it must be {required}", []).append(configuration)
        elif found[1].casefold() != required.casefold():
          problems.setdefault(f"{name} is '{found[1]}'; it must be {required}", []).append(configuration)
      for item in evaluation.items:
        if item.kind.casefold() != "clcompile":
          continue
        for name, required in REQUIRED_COMPILE_SETTINGS.items():
          own = item.own.get(name.casefold())
          if own and own[1].casefold() != required.casefold():
            self.add(_project.path, own[2], "stated-settings",
                     f"{item.include} overrides {name} as '{own[1]}'; it must be {required}")
    for problem, configurations in problems.items():
      self.add(_project.path, 0, "stated-settings", f"{problem} (for {' and '.join(configurations)})")

  def check_alignment(self, _project: Project) -> None:
    debug, release = (comparable(_project.evaluations[configuration]) for configuration in CONFIGURATIONS)
    for key in sorted(debug.keys() | release.keys()):
      left, right = debug.get(key), release.get(key)
      if left and right and " ".join(left[2].split()) == " ".join(right[2].split()):
        continue
      label, name, _value, line = left or right
      if name.casefold() in OPTIMIZATION_SETTINGS:
        continue
      if (left and right and name.casefold() == "preprocessordefinitions"
          and only_debug_versus_ndebug(left[2], right[2])):
        continue
      shown = [f"'{side[2]}'" if side else "not set" for side in (left, right)]
      self.add(_project.path, line, "debug-release", f"{label} differs: Debug|x64 {shown[0]}, Release|x64 {shown[1]}")

  def check_shaders(self, _project: Project) -> None:
    for evaluation in _project.evaluations.values():
      for item in evaluation.items:
        if item.kind.casefold() != "fxcompile":
          continue
        stem = PurePosixPath(item.include.replace("\\", "/")).stem
        variable = shader_metadata(item, stem, "VariableName")
        if variable != f"g_{stem}":
          self.add(_project.path, item.line, "registration",
                   f"{item.include}: VariableName is {variable!r}; it must be g_{stem}")
        output = shader_metadata(item, stem, "HeaderFileOutput")
        normalized = re.sub(r"/+", "/", (output or "").replace("\\", "/")).casefold()
        wanted = f"CompiledShader/{stem}.h"
        if not any(normalized == (prefix + wanted).casefold() for prefix in HEADER_OUTPUT_PREFIXES):
          self.add(_project.path, item.line, "registration", f"{item.include}: HeaderFileOutput is {output!r}; it must "
                   f"be CompiledShader\\{stem}.h in the project directory")
        stage = SHADER_NAME.fullmatch(stem)
        shader_type = shader_metadata(item, stem, "ShaderType")
        if stage and (shader_type or "").casefold() != SHADER_TYPES[stage.group(1)].casefold():
          self.add(_project.path, item.line, "registration", f"{item.include}: ShaderType is {shader_type!r}; a "
                   f"{stage.group(1)} shader is {SHADER_TYPES[stage.group(1)]}")

  def check_registration(self, _project: Project) -> None:
    registered = {path.casefold(): node for node, path in _project.items(_project.xml, REGISTERED_ITEMS)}
    filtered = {path.casefold(): node for node, path in _project.items(_project.filters, REGISTERED_ITEMS)}
    filters_name = PurePosixPath(_project.path).name + ".filters"
    for path in self.sources:
      if self.owner(path) is not _project:
        continue
      pure = PurePosixPath(path)
      inside = pure.relative_to(_project.directory).parts
      suffix = pure.suffix.casefold()
      if suffix in CPP_EXTENSIONS and len(inside) == 1:
        wanted = "ClCompile" if suffix == ".cpp" else "ClInclude"
        node = registered.get(path.casefold())
        if node is not None and node.tag != wanted:
          self.add(_project.path, node.line, "registration", f"{inside[0]} is a {node.tag} item; it must be {wanted}")
        if node is None:
          self.add(path, 0, "registration", f"is not in {_project.path}")
          # A registered file missing from the .filters is reported once, against the project, below.
          if _project.filters is not None and path.casefold() not in filtered:
            self.add(path, 0, "registration", f"is not in {filters_name}")
      elif suffix == ".hlsl":
        node = registered.get(path.casefold())
        if node is None or node.tag.casefold() != "fxcompile":
          self.add(path, 0, "registration", f"is not an FxCompile item in {_project.path}, so nothing compiles it")
      elif suffix == ".hlsli":
        node = registered.get(path.casefold())
        if node is None or node.tag.casefold() != "none":
          self.add(path, 0, "registration", f"is not a None item in {_project.path}; a shader include is compiled "
                   "only through the shaders that include it")
    for path, node in sorted(registered.items()):
      if _project.filters is not None and path not in filtered:
        self.add(_project.path, node.line, "registration", f"{node.attrib['Include']} is not in {filters_name}")
    for source, label in ((_project.xml, _project.path), (_project.filters, f"{_project.path}.filters")):
      for node, path in _project.items(source):
        if node.tag.casefold() in NON_FILE_ITEMS or re.search(r"[$%@*?]", node.attrib.get("Include", "")):
          continue
        if not self.exists(path):
          self.add(label, node.line, "registration", f"{node.tag} {node.attrib['Include']} does not exist")

  def check_filters(self, _project: Project) -> None:
    label = f"{_project.path}.filters"
    by_stem: dict[str, dict[str, tuple[str, int]]] = {}
    for group in _project.filters.children:
      for node in group.children if group.tag == "ItemGroup" else []:
        if node.tag == "Filter":
          name = node.attrib.get("Include", "")
          if any(part.strip().casefold() in DEFAULT_FILTERS for part in re.split(r"[\\/]", name)):
            self.add(label, node.line, "filters", f"has the filter '{name}'; filters say what the code does, not what "
                     "kind of file it is")
        elif node.tag.casefold() in REGISTERED_ITEMS:
          include = node.attrib.get("Include", "")
          chosen = next((child.value() for child in node.children if child.tag == "Filter"), "")
          if not chosen:
            self.add(label, node.line, "filters", f"{include} has no filter")
          path = PurePosixPath(resolve_include(_project.directory, include))
          if path.suffix.casefold() in CPP_EXTENSIONS:
            by_stem.setdefault(str(path.with_suffix("")).casefold(), {})[path.suffix.casefold()] = (chosen, node.line)
    for pair in by_stem.values():
      if len(pair) == 2 and pair[".h"][0] != pair[".cpp"][0]:
        self.add(label, pair[".cpp"][1], "filters", f"a .h is in '{pair['.h'][0]}' and its .cpp in "
                 f"'{pair['.cpp'][0]}'; they share one filter")

  def check_cross_project(self) -> None:
    claimed: dict[str, list[str]] = {}
    for project in self.projects:
      for _node, path in project.items(project.xml, REGISTERED_ITEMS):
        claimed.setdefault(path.casefold(), [])
        if project.path not in claimed[path.casefold()]:
          claimed[path.casefold()].append(project.path)
    for path, projects in claimed.items():
      if len(projects) > 1:
        shown = next((source for source in self.sources if source.casefold() == path), path)
        self.add(shown, 0, "registration", f"is registered in {len(projects)} projects: {', '.join(sorted(projects))}")

  def check_tidy_reach(self) -> None:
    if not self.projects:
      return
    pattern = header_filter(self.root)
    if pattern is None:
      self.add(".clang-tidy", 0, "tidy-reach", "has no readable HeaderFilterRegex, so no project's headers are linted")
      return
    try:
      regex = re.compile(pattern)
    except re.error as error:
      self.add(".clang-tidy", 0, "tidy-reach", f"HeaderFilterRegex does not compile: {error}")
      return
    for project in self.projects:
      prefix = "" if project.directory == "." else project.directory + "/"
      probes = (f"{prefix}Probe.h", f"{prefix}Probe.h".replace("/", "\\"))
      if not all(regex.search(probe) for probe in probes):
        self.add(project.path, 0, "tidy-reach", f".clang-tidy's HeaderFilterRegex does not match {probes[0]}, so no "
                 "header in this project is linted; add the project to it")

  # ── Driver ──

  def run(self) -> None:
    self.load()
    self.check_build_output()
    self.check_third_party_files()
    self.check_layout_and_names()
    self.check_sources()
    self.check_solution()
    for project in self.projects:
      self.check_project(project)
    self.check_cross_project()
    self.check_tidy_reach()


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--root", type=Path, default=ROOT, help="repository root (default: the parent of Build/)")
  arguments = parser.parse_args()
  root = arguments.root.resolve()
  if not root.is_dir():
    print(f"error: {root} is not a directory", file=sys.stderr)
    return 2
  if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(errors="replace")  # a cp1252 console must not crash on a path it prints

  checker = Checker(root)
  checker.run()
  if not checker.projects:
    print("No .vcxproj in the tree yet: the project rules have nothing to check; the whole-tree rules ran.")
  findings = sorted(checker.findings)
  for finding in findings:
    print(finding)
  if findings:
    fired = sorted({finding.rule for finding in findings})
    print(f"\nCheckProjectFiles: {len(findings)} finding(s). Rules (AGENTS.md): "
          + ", ".join(f"{rule} {RULES[rule]}" for rule in fired))
    return 1
  sources = sum(1 for path in checker.sources if PurePosixPath(path).suffix.casefold() in CPP_EXTENSIONS)
  print(f"CheckProjectFiles: clean -- {len(checker.projects)} project(s), {sources} C++ file(s), {len(checker.files)} "
        "file(s) considered.")
  return 0


if __name__ == "__main__":
  sys.exit(main())
