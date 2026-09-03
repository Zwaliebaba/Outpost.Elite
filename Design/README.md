# Design/ — Outpost: Elite

**Status:** opened 2026-09-02; all five owner decisions taken the same day, and **slices 0a and
0c are built** — the upstream source is referenced and proved, the 6502 oracle runs, and the
first routine is ported and matching it. **A fresh clone needs `git submodule update --init`
before any of that is true** (Elite-Conversion-Plan.md §6.9).

The task this corpus plans: take the annotated 6502 source of **Commodore 64 Elite** that sits
under [`MasterFile/`](../MasterFile/) and produce a modern C++ port of the game inside the
`Outpost` solution, on the same engineering conventions as the sibling repositories
(Outpost.Frontier, Outpost.Warzone).

## The one finding to read first

`MasterFile/` holds the **13 master files** of Mark Moxon's annotated C64 Elite source
(5,615 lines). Those masters are almost entirely `INCLUDE` lines: they pull in **710 distinct
library files** plus the font binary, and the routine bodies, ship blueprints and token tables
all live in those includes rather than in the masters. They were not in this repository.

**Slice 0a fixed that**: the upstream tree sits at `Upstream/elite-source-code-library`, pinned
at commit `aa3f7ee`, and all 712 include paths resolve. It is a **submodule**, not a copy —
a fresh clone needs `git submodule update --init` before anything here can be built or tested,
and `tools/inventory.py --check-includes` is the standing proof either way. See
[Elite-Conversion-Plan.md §1](Elite-Conversion-Plan.md#1-what-we-actually-have).

## Reading order

| | Read | For |
|---|---|---|
| 1 | this file | what exists and where the decisions are |
| 2 | [Elite-Conversion-Plan.md](Elite-Conversion-Plan.md) | the inventory of what we have, the target architecture, the phased build order with acceptance criteria, and the verification strategy. **The only document that sequences.** |
| 3 | [Source-Inventory.md](Source-Inventory.md) | every group of original routines, which C++ file it becomes, and whether it is ported, replaced or dropped. The coverage ledger the port is measured against. |
| 4 | the ADRs below | the decisions the plan rests on. **The ADR wins on *what*, the plan on *when*.** |
| 5 | [Risk-Register.md](Risk-Register.md) | what is most likely to go wrong, and where each risk is validated early |

## Decisions at a glance

| ADR | Question | Decision (one line) |
|---|---|---|
| [001](ADR/ADR-001-scope-and-fidelity.md) | Scope and fidelity | **Port the C64 game as it is, bit-faithful in logic, before changing anything.** GMA85 variant as configured in `elite-build-options.asm`; the assembled original running in an emulator is the reference. Modernisation is a later phase with its own decisions. |
| [002](ADR/ADR-002-numeric-model.md) | Numeric model | **8-bit integer semantics preserved exactly** — same widths, same wraparound, same lookup tables, same RNG — in a fixed 320×200 logical canvas. No floats in game logic. |
| [003](ADR/ADR-003-verification.md) | Verification | **A 6502 oracle in the test project** runs the assembled original's routines and the C++ port on the same inputs; **golden canvases** for screens; **replay hashes** for whole-game determinism. |
| [004](ADR/ADR-004-projects-and-layout.md) | Projects and layout | **Our own codebase — nothing lifted from a sibling repository.** `GameLogic` (namespace `Elite`) holds the port, platform-free and deterministic; presentation lives in `Outpost.exe`; tests under `Tests/`. Flat folders, unique PascalCase names, generated data tables checked in, `MasterFile/` and `Upstream/` are reference only. |
| [005](ADR/ADR-005-presentation.md) | Presentation | **Packaged Win32, no XAML: MSIX stays, WinUI 3 goes.** Raw window, flip-model D3D12 swap chain blitting the indexed canvas at integer scale, XAudio2 with a small SID-style synthesiser. |

## Two things this corpus is deliberately not

- **Not a modernisation design.** Higher resolution, smoother motion, gamepad, new UI: all
  real, all later, all gated on the faithful port passing its oracle and golden tests
  (ADR-001 §4). Building toward a nicer game before the original one runs is the failure mode
  the plan is shaped to avoid.
- **Not a licence.** The upstream source carries no licence (ADR-001 §5, Risk R1), and the
  owner intends to publish eventually, which makes this the project's largest exposure rather
  than a footnote. Slice **0e** seeks the rights holders' permission, and nothing is pushed to
  a public remote until it closes. `Upstream/` is a submodule, so none of its content is in
  this history at all — but `MasterFile/`'s 13 files are, and they carry the same copyright.
  ADR-001 §5 records that as an open owner decision.

## Conventions

The house rules are the ones this repository already carries in `.clang-format`,
`.clang-tidy` and `.editorconfig` (copied from Outpost.Frontier, and they reference an
`AGENTS.md` that does not exist here yet — slice 0c writes it). In short: PascalCase types,
functions and files; `_param`, `m_member`, `g_global`; `UPPER_CASE` constants; Allman braces,
two-space indent, 140 columns; flat project folders; `/std:c++latest`, x64, v145.

Every ported function carries the original label in a comment on its declaration
(`// 6502: LL9`), and `Source-Inventory.md` is the table that says where each label went, so
that "where did TACTICS part 4 end up" is a grep rather than an archaeology.
