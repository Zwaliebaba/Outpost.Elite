# Design/ — Outpost: Elite

**Status:** opened 2026-09-02. **Phases 0, 1, 2, 3 and 5 are built as of 2026-09-05**, and phase 4
is built but for one slice: the kernel, the whole docked game, flight with its 3D pipeline, the
sound and music, the ship AI and the autopilot, the explosions, the main game loop with hyperspace
and the spawning rules, and the pause screen with its thirteen option toggles are all ported and
compared against the assembled original. The executable launches, flies, fights, docks and dies.
**What is left is slice 4d** — the missions and the Trumbles — plus two pieces of recorded debt:
thirteen mutation survivors in the ship AI's sweep — measured rather than named, by `python
tools/mutate.py --unit tactics` (plan §6.125, §6.132, §6.147), and the `VideoState` work that ADR-005 §1's closed decision
turned out to need (plan §6.133). **A fresh clone needs
`git submodule update --init` and `python tools/labels.py --assemble`** before the oracle tests mean
anything (Elite-Conversion-Plan.md §6.9, Risk R9). The suite is **<!--count:tests-->377 tests** and
CI runs **<!--count:checks-->eleven repository checks** beside it.

The task this corpus plans: take the annotated 6502 source of **Commodore 64 Elite** that sits
under [`MasterFile/`](../MasterFile/) and produce a modern C++ port of the game inside the
`Outpost` solution, on the same engineering conventions as the sibling repositories
(Outpost.Frontier, Outpost.Warzone).

## The one finding to read first

`MasterFile/` holds the **<!--count:masters-->12 master files** of Mark Moxon's annotated C64
Elite source (<!--count:master-lines-->5,577 lines). Those masters are almost entirely `INCLUDE`
lines: they pull in **<!--count:library-includes-->710 distinct library files** plus the font
binary, and the routine bodies, ship blueprints and token tables all live in those includes
rather than in the masters. They were not in this repository.

**The count used to read "13 master files ... 5,615 lines" and that counted the FOLDER**, not the
source: upstream's own `README.md` sits beside the twelve `.asm` files and is 39 lines of
Markdown. `inventory.py` had been printing twelve since the day it was written. Both numbers are
now marked and checked by `tools/check_counts.py`; the thirteen-file figure is still the right
one for the licence exposure, which is every tracked file in the folder, and ADR-001 §5 says so
there.

**Slice 0a fixed that**: the upstream tree sits at `Upstream/elite-source-code-library`, pinned
at commit `aa3f7ee`, and all <!--count:includes-->712 include paths resolve. It is a
**submodule**, not a copy —
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
| [002](ADR/ADR-002-numeric-model.md) | Numeric model | **8-bit integer semantics preserved exactly** — same widths, same wraparound, same lookup tables, same RNG — in the space view's 256×144 logical coordinates, on a canvas that holds the C64's own multicolour bitmap and cell-colour planes and resolves to 320×200 indices at the presenter seam (§4, amended 2026-09-03). No floats in game logic. |
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
  than a footnote. Slice **0e** seeks the rights holders' permission. **The repository is
  already public**, by owner ruling on 2026-09-03 that reversed a same-day ruling to make it
  private (Risk R1, realised and accepted rather than mitigated). `Upstream/` is a submodule, so
  none of its content is in this history — but `MasterFile/`'s 13 files are, and they carry
  the same copyright. What still closes 0e is a written answer from the rights holders.

## Conventions

The house rules are in [`AGENTS.md`](../AGENTS.md) at the repository root, written for this
repository in slice 0c from its own `.clang-format`, `.clang-tidy` and `.editorconfig`. In short: PascalCase types,
functions and files; `_param`, `m_member`, `g_global`; `UPPER_CASE` constants; Allman braces,
two-space indent, 140 columns; flat project folders; `/std:c++latest`, x64, v145.

Every ported function carries the original label in a comment on its declaration
(`// 6502: LL9`), and `Source-Inventory.md` is the table that says where each label went, so
that "where did TACTICS part 4 end up" is a grep rather than an archaeology.
