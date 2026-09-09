# ADR-001 — Scope and Fidelity: Port the C64 Game As It Is, First

**Status:** Accepted · 2026-09-02 (§5 amended the same day by owner ruling — see below); §5 amended twice more 2026-09-03 (make it private, then reversed: it stays public, knowingly)
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
   same AI decisions on the same RNG state, same lines on a 320×200 canvas — which is the
   verification view — same sounds as a sequence of SID register writes. What the executable
   presents is `Elite::Picture`, a 640×400 rendering of the same frame
   ([Design/Resolution.md](../Resolution.md)), drawn beside the canvas and never in its place. Anything that can be checked against the assembled original
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
4. **Modernisation comes after, and each item is its own decision.** *(Amended 2026-09-06, owner
   ruling: the gate below is met, the modernisation of the PROGRAM is planned in
   [Design/Modernize.md](../Modernize.md), and its last phase detaches the port from the original —
   the oracle becomes recorded fixtures and `MasterFile/` and `Upstream/` leave the tree (Modernize.md
   §1 R-a to R-d, Phase M6). Every clause of this ADR stands until that phase amends §5 for real.)* Smoothing,
   input remapping, gamepad, save UI, timing options — none is designed in this corpus. The
   resolution is designed in [Design/Resolution.md](../Resolution.md) and is not an option: the
   fidelity suites keep their meaning because they read the canvas, which the picture never
   replaces. The
   gate for starting phase 6 is: every oracle suite, every golden and the replay suite green on
   the faithful build. **One original feature is removed ahead of that phase by owner ruling,
   2026-09-08: the pause screen, recorded in ADR-005 §4 and Design/InputTimer.md §5.9.** It is a
   removal and not an option, so "green with the option off" cannot apply to it; the suites are
   green without it and the ledger row says what went.
5. **Licence posture — owner ruling, 2026-09-02.**

   **THE HISTORY WAS REWRITTEN 2026-09-09 (owner ruling), WHICH IS WHAT THE CLAUSE BELOW SAID
   WOULD NEVER BE DONE BY THIS PLAN.** `git filter-repo` stripped `Design/Reference/`,
   `MasterFile/`, the `Upstream` gitlink and `.gitmodules` from every commit, and all three
   branches were force-pushed. A fresh clone now carries **no object under any of those paths and
   no blob bearing "copyright D. Braben and I. Bell 1985"**, and it still builds and passes all 131
   tests and 13 checks. Two commits were pruned as empty because they contained nothing else.

   **IT IS NOT FINISHED AND THIS SECTION MUST NOT PRETEND IT IS.** GitHub retains twenty-four
   `refs/pull/*` refs that a force-push cannot touch, and every one of them still points at
   pre-rewrite commits: the blobs stay fetchable by SHA until GitHub Support purges them. Until
   that request is made and completed, the exposure is reduced for anyone who CLONES and unchanged
   for anyone who knows a commit hash. Nor can a rewrite un-publish: the repository was public
   throughout, and forks, mirrors and archives are outside anyone's reach here.

   **AMENDED 2026-09-08 AT M6-f, WHICH IS THE AMENDMENT THIS SECTION WAS ALWAYS WAITING FOR.**
   Everything below describes the posture while the port was written against the original, and it
   is kept because it is the reasoning the owner ruled on. What is true at the tip now:

   - **`Upstream/` and `MasterFile/` are gone.** The submodule entry, `.gitmodules`, the twelve
     masters and upstream's `README.md` beside them are deleted, with the tools that read them
     (`labels.py`, `c64_source.py`, `extract_tables.py`) and the assembler step on both CI legs.
     A fresh clone builds and runs the whole suite with a compiler and nothing else.
   - **What remains of the original in this repository is its DATA**: the generated tables in
     `GameLogic/` — the font, the token tables, the sine and arctangent tables, the ship
     blueprints, the dashboard image — which the game cannot run without and which §1 R-c of
     Modernize.md accepts as the residual exposure. They are checked in and cannot be regenerated
     here any more, which is the point: the tables ARE the inheritance.
   - **THE HISTORY IS NOT REWRITTEN AND THIS DOES NOT CLAIM IT IS.** Every commit before M6-f
     still carries `MasterFile/`, and a reader of the history still finds all 5,616 lines of it.
     Whether that history is rewritten is a separate owner decision, out of the modernisation
     plan's scope and recorded as its Risk R21. Deleting at the tip changes what a clone gets,
     not what a fetch can reach.
   - **Slice 0e is still not closed**, and the position is still "published knowingly, pending
     permission" rather than "published because it is fine".

   The ruling as taken, and the reasoning under it. The upstream repository states it is
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
     all. **`MasterFile/` was the exception, and it was committed:** 13
     files, 5,616 lines,
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

   **Owner ruling, 2026-09-03: the repository stays public.** This reverses the ruling taken
   earlier the same day, which was to make it private; that ruling was recorded here and is
   recorded as reversed rather than removed.

   What the two rulings share is the finding that prompted them, and it stands: the clause this
   section used to carry -- "nothing is pushed to a public remote until 0e closes" -- had never
   been true. `Zwaliebaba/Outpost.Elite` is public (checked against the GitHub API, not assumed),
   CI assembled the game on every push, and `MasterFile/` was tracked from the commit that added it
   until the history was rewritten on 2026-09-09 and that commit ceased to exist. The
   clause is gone, because a rule nobody follows is worse than no rule.

   **This does not make the exposure smaller. It makes it accepted rather than mitigated**, and
   the difference is worth writing down:

   - The 13 tracked files were public and carried `copyright
     D. Braben and I. Bell 1985` in their own headers, with the commentary `copyright Mark Moxon`.
     That is 5,616 lines of source this project does not own, in a
     repository anyone can read. **Thirteen is the right number HERE and twelve is the right
     number in §1.1 of the plan**: the exposure is every tracked file in the folder, upstream's
     own `README.md` included, while the annotated SOURCE is the twelve `.asm` files. The two
     were conflated until `tools/check_counts.py` made each of them a marked, checked claim.
   - What the structural mitigation bought is unchanged and is why this is 13 files rather than
     3,000: `Upstream/` is a **submodule**, so the library is a gitlink and not content; no
     assembled binary is tracked; and no original assembler is copied into `GameLogic/`, `Tests/`
     or `Design/`. Those rules stay, and they are what keeps the decision cheap to revisit.
   - **Slice 0e is not closed by this.** What closes it is a written answer from the rights
     holders. Until then the position is "published knowingly, pending permission" rather than
     "published because it is fine", and Risk R1 says so.

   The fallback if permission is refused is unchanged: private, undistributed, and `Upstream/`
   excised from the published history rather than merely deleted at the tip. Going public first
   makes that fallback more expensive, not less, because a history that has been read cannot be
   unread -- and that cost is part of what this ruling accepts.

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

**What belongs in this table, decided 2026-09-05 because it had been filled by feel.** A row here
is a behaviour **a player can observe** that the original gets wrong and the port reproduces. The
port has found a great deal else — dead code, branches that cannot be taken on this build, upstream
comments describing another version's constants, flags computed and discarded — and none of that is
a row: it is recorded at the call site in `GameLogic/` and in the plan's §6 findings, where it
belongs. Mixing the two would make this table long and stop it meaning anything.

| Behaviour | Where | Status |
|---|---|---|
| **A distant ship can be drawn where the previous one was.** `SHPPT` does not test `PROJ`'s carry; it tests the accumulator ORed with `K3+1`, and on one of `PROJ`'s two overflow exits the accumulator is zero and `K3+1` still holds the LAST projection's high byte. So a ship whose x overflowed is plotted at the previous ship's position. | `ShipDraw.h` / `ShipDraw.cpp`, `DrawShipAsPoint` — and it is why `_screen` is a parameter that outlives the call rather than a local | Ported, with the reason on the declaration; plan §6.36 |
| **A failed load poisons every later exit from the disk menu.** `LOD`'s two error paths leave by `JMP SVE` rather than by an RTS, so the menu is re-entered with `loading`'s own `JSR LOD` return address still on the stack. Leaving the menu afterwards returns *into* `loading`, renames the last-saved commander to whatever was typed, and restarts the game; and the frame is pushed again on every failed load. | `SaveGame.cpp` | Ported; plan §6.21 has the full trace |
| `NRU% = 0` although the RUGAL table has entries; Data on System can hang for a few systems (e.g. after docking at Biarge in galaxy 1 during the Constrictor mission) | `EliteConfig.h`, `SystemData.cpp` | Ported; fix is a phase-6 option |
| **A commander who cannot afford a Trumble is given one anyway.** `TBRIEF` calls `LCASH` to take 5,000 credits and does not test the carry: `INC TRIBBLE` follows unconditionally, and `LCASH` puts the money back when it cannot afford the spend. `DOENTRY` mostly prevents the state by refusing to offer below a cash threshold, but that test reads ONE BYTE of a four-byte value, so the band recurs every 6,553.6 credits and a poor commander inside one is offered a free Trumble. | `Missions.h` / `Missions.cpp`, `OfferTrumble` | Ported, with the reason on the declaration; `TheTrumbleOfferMatchesTBRIEF` sweeps both halves and asserts sixteen paid and sixteen not |
