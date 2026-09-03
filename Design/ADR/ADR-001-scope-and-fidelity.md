# ADR-001 — Scope and Fidelity: Port the C64 Game As It Is, First

**Status:** Accepted · 2026-09-02 (§5 amended the same day by owner ruling — see below); §5 amended again 2026-09-03 (make the repository private)
**Depends on:** — (root decision)
**Feeds:** every other ADR; the plan's phases 1–5 exist to satisfy it, phase 6 is what it defers

## Context

`MasterFile/` is the annotated source of **Commodore 64 Elite**, configured by
`elite-build-options.asm` as `_VERSION=8` (C64), `_VARIANT=1` (the GMA85 NTSC release), with
`_MATCH_ORIGINAL_BINARIES=TRUE`. The code is the BBC Master lineage (`common` + `enhanced` +
`advanced` + `master` libraries) with a C64 platform layer: VIC-II bitmap and sprites, SID
sound and music, CIA keyboard, Kernal disk I/O, and the Trumble mission.

"Convert to modern C++" admits two very different projects: a faithful port whose behaviour is
the original's, or a re-imagining that uses the original as a reference for feel. They cannot be
done at once, because the second has no definition of "correct" until the first exists.

## Decision

1. **The target is the C64 game, behaviour-exact in its logic.** Same universe, same prices,
   same AI decisions on the same RNG state, same lines on a 320×200 canvas, same sounds as a
   sequence of SID register writes. Anything that can be checked against the assembled original
   is checked (ADR-003).
2. **The variant is the one the masters are configured for** (GMA85 NTSC), held as
   `constexpr` in `EliteConfig.h` so the PAL variant and the maxed commander are switches, not
   forks. The NTSC/PAL distinction matters only to timing (§5.3 of the plan) and to a few bytes
   of workspace noise the port does not carry.
3. **Known original bugs are ported, then listed.** `NRU% = 0` (the Data on System crash for a
   handful of systems), and whatever else the port finds, ship as the original behaviour behind
   a named constant. The list lives in this ADR's §6 and grows as they are found. Fixing any of
   them is a phase-6 option with the fix *off* by default, so the fidelity suites keep meaning
   what they say.
4. **Modernisation comes after, and each item is its own decision.** Resolution, smoothing,
   input remapping, gamepad, save UI, timing options — none is designed in this corpus. The
   gate for starting phase 6 is: every oracle suite, every golden and the replay suite green on
   the faithful build.
5. **Licence posture — owner ruling, 2026-09-02.** The upstream repository states it is
   provided with no licence, for reading and forking only, on an educational and non-profit
   basis; the code is copyright D. Braben and I. Bell, the commentary Mark Moxon. The owner has
   ruled two things that this ADR originally assumed the other way:

   - **The upstream tree is referenced at `Upstream/elite-source-code-library`**, pinned at
     commit `aa3f7ee` (2026-09-01), and not a sibling checkout. **Corrected 2026-09-03:** this
     ADR said "vendored ... not a submodule", and the tree has never been either. What the
     repository actually holds at that path is a **gitlink** — mode `160000`, the commit hash
     and nothing else — which is a submodule entry in all but the registration. That
     registration was missing, so `git submodule` could not act on it and a fresh clone got an
     empty directory with no URL to recover from; `tools/inventory.py --check-includes`
     reported **0 of 712** include paths resolving, and nothing from slice 1a onward could be
     built. `.gitmodules` now names the path and the upstream URL, and one
     `git submodule update --init` restores the tree.

     The correction is a description catching up with the tree, not a change of posture, and it
     lands on the safer side of the ruling: because the content is referenced rather than
     copied, **none of the ~3,000 unlicensed files has ever entered this repository's history.**
     What was described as reversible in one command turns out never to have needed reversing.
     The cost is that the pin depends on `markmoxon/elite-source-code-library` continuing to
     exist and continuing to contain `aa3f7ee`; if that becomes a risk the answer is a mirror
     under the owner's account, not a copy into this history.
   - **The intent is to publish eventually.** This repository is therefore not permanently
     private, and the plan gains a slice — **0e, Permission** — that seeks the rights holders'
     agreement. Nothing is pushed to a public remote until 0e closes.

   Those two rulings pull against each other, and the corpus says so rather than pretending
   otherwise: vendoring puts ~3,000 unlicensed files in the tree, and publishing that tree is
   the single largest legal exposure in the project (Risk R1). The mitigation is structural,
   so that the decision stays reversible in one command:

   - Everything not ours lives under **`Upstream/`, with one exception this section used to
     omit**. No original assembler is copied into `GameLogic/`, `Tests/` or `Design/`, and
     `Upstream/` is a reference rather than a copy, so nothing under it is in the history at
     all. **`MasterFile/` is the exception, and it is committed:** 13 files, 5,615 lines,
     headed `copyright D. Braben and I. Bell 1985` with commentary `copyright Mark Moxon`.
     "Removing one directory removes all of it" was therefore not true — removing `Upstream/`
     removes nothing, and `MasterFile/` is what a published history would carry. That is an
     owner decision (see the note under 0e in the plan), not something the corpus settles.
   - The **reference binaries are never committed** (`.gitignore` covers
     `Upstream/**/3-assembled-output/`, `5-compiled-game-disks/` and `4-reference-binaries/`).
     They are what an oracle run needs on the machine, not in the history. **The third of those
     matters most and was found on inspection, not assumed:** the upstream tree ships
     `versions/*/4-reference-binaries/` — 589 files, including `ELTA.bin`…`ELTK.bin` for the
     C64 — and those are the **original released game itself**, not commentary on it.
     Publishing the annotated source is one exposure; publishing the shipped commercial
     binaries beside it is a larger and quite different one. Ignoring them applies the rule
     this section already states rather than inventing a new one, and it costs nothing: they
     stay on disk, which is where the oracle reads them.
   - **Generated data tables are checked in** because the port cannot build without them. They
     are a derivative of the original data and 0e must cover them explicitly.
   - The port's own code — the C++, the 6502 interpreter, the tools, this corpus — is ours.

   If 0e does not produce permission, the fallback is the posture this ADR first proposed:
   private, undistributed, `Upstream/` excised from the published history rather than merely
   deleted at the tip.

   **Owner ruling, 2026-09-03: make the repository private now, rather than at the fallback.**

   The clause above — "nothing is pushed to a public remote until 0e closes" — had never been
   true. `Zwaliebaba/Outpost.Elite` is public (checked against the GitHub API, not assumed), CI
   assembles the game on every push, and `MasterFile/` has been tracked since `92a3c7f`. So this
   is not a new posture; it is the stated one, applied.

   Three things it does and does not do, in order of how easily each is misread:

   - **It stops the exposure continuing. It does not undo it.** The commits are public and have
     been. Anyone who cloned has a copy. Nothing short of a history rewrite plus a request to
     GitHub touches that, and neither was ruled.
   - **It costs the project nothing.** The oracle build is untouched, no history is rewritten, no
     network dependency enters CI, and every existing clone stays valid. Public visibility is not
     something a personal port needs before 0e answers.
   - **It does not close 0e.** The written answer from the rights holders is still what closes
     it, and this ruling only removes the pressure of an open question being answered by default.

   The alternatives put to the owner and not chosen were: strip `MasterFile/` from the history
   (which would make the oracle depend on fetching a pinned upstream commit at build time — a new
   failure mode in CI, and a rewritten history that invalidates every clone), document that
   permission exists (which needs a real grant from a real rights holder, not a reading of a
   licence file), and accept the risk in writing (defensible for a hobby port, but it should be a
   decision with a name on it rather than the unexamined default it had become).

   **The action is the owner's.** Repository visibility is not something any tool in this project
   can change; it is one setting under the repository's own settings page. Until it is taken,
   slice 0e stays open and Risk R1 stays marked realised.

## What "faithful" does not mean

- It does not mean emulating the 6502, the VIC-II or the SID. The port is a rewrite that
  produces the same results; the emulator is the *test oracle*, not the product (ADR-003).
- It does not mean the same memory layout at runtime. Zero page and workspaces become structs
  with the original names (plan §4.3); that is for traceability, not for address fidelity.
- It does not mean the same frame rate mechanism. The original ran its loop as fast as it
  could; the port runs a fixed rate measured from the original (ADR-005 §3, Risk R3).
- It does not mean the loader, copy protection, disk fast-loader or the PDS transfer tool.
  Those masters are dropped in full (Source-Inventory §5).

## Consequences

- Every slice's acceptance is a fidelity check, which is what makes the plan's order
  defensible: the docked game before flight is not a preference, it is the order in which
  the oracle can reach things.
- The port will carry the original's data model (37-byte ship slots, sign-magnitude 24-bit
  coordinates, the token encodings) longer than a fresh design would. That is the price of
  being testable against the original, and the plan's §4.2 says when it may be tidied.
- The DOS-port in `~/source/repos/Elite` is not a source for this work. It is a different
  program (Andy Onions' 1987 x86 rewrite). Its universe tables are used only as a second
  cross-check in slice 2a.

## §6 — Original behaviours ported deliberately (append as found)

| Behaviour | Where | Status |
|---|---|---|
| `NRU% = 0` although the RUGAL table has entries; Data on System can hang for a few systems (e.g. after docking at Biarge in galaxy 1 during the Constrictor mission) | `EliteConfig.h`, `SystemData.cpp` | Ported; fix is a phase-6 option |
