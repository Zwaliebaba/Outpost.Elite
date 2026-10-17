# ADR-066 — String and character literals are ASCII, and the checker says so

**Status:** accepted 2026-10-17, with the change that implements it. The owner asked for the check after playing D02 and D04, which drew `â€”` and `Â·`.

## Context

The tree's sources are UTF-8 without a BOM (`.editorconfig`). MSVC compiles such a file in the system code page unless `/utf-8` is given, and no project gives it. On a Windows-1252 system, `L"—"` is three characters, `â€”`.

The shell's literals held `—`, `·`, `–`, `↑` and `↓`. The screens drew them garbled:
- the shipyard's empty Sell and Fitted cells;
- the charts' target label;
- every top bar's separators.

The tests did not notice, because they compared the same garbled literals on both sides. On main, 43 literals in six files were affected. The game's own code had none.

## Decision

1. **A literal is ASCII** (AGENTS.md §4). A character past ASCII is written as its escape: `L"—"`, `L"·"`. Comments are prose and keep the characters themselves.
2. **`Build/CheckProjectFiles.py` checks it** as the rule `ascii-literals`. It looks at every string and character literal of every `.h` and `.cpp`, raw strings included, through the scanner the R2 and R11 rules already use. Each finding names the escape to write.

`/utf-8` was the other way, and it is not taken:
- It is a compiler setting, which AGENTS.md §3 holds fixed.
- It would also change how every narrow literal is encoded in the binary, not only the shell's.
- A literal that reads the same whatever the compiler's code page needs no setting to be right.

## How it was measured

On main (`eb9e33a`), the rule reports 43 findings, the same 43 literals that a scan of every literal for characters past `0x7F` found:

| File | Characters |
|---|---|
| `Outpost/DockedChart.cpp` | `·` `—` |
| `Outpost/DockedEquip.cpp` | `·` `–` `—` `↑` `↓` |
| `Outpost/DockedStatus.cpp` | `·` |
| `Tests/OutpostTests/DockedEquipTests.cpp` | `·` `—` |
| `Tests/OutpostTests/DockedChartTests.cpp` | `·` |
| `Tests/OutpostTests/DockedMarketTests.cpp` | `—` |

With them escaped, the rule is clean on the whole tree, and comments holding the same characters are not reported. The escaped strings were drawn through the portable canvas under g++, and dumped as the right characters.

## What this forecloses

A designer's string pasted into a literal fails CI until it is escaped. `/utf-8` would need this ADR replaced, not just a project file edited.
