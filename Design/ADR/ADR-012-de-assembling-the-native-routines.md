# ADR-012 — De-assembling the native routines: the GameState, typed views, entries and poisoning

**Status:** accepted 2026-10-10, with the change that implements it: the arithmetic (`Maths`), the first subsystem of Phase 4's second step (D17, ADR-011 item 7).

## Context

**The native routines are the original's instructions, translated** (ADR-010). They read and write `Registers` and the data segment at computed addresses. ADR-011 measured 4,991 register uses and 512 computed addresses over `GameLogic/*.cpp`. D17's second step replaces them, subsystem by subsystem, with ordinary functions on typed state, and keeps the comparison with the original in the tests.

**Two things stand in the way of doing that one subsystem at a time.**
- **Callers.** A routine is called by the hook and by native code in every other subsystem, all of it still on registers. Converting a routine cannot mean converting its callers at the same moment.
- **Leftovers.** A routine leaves more in the registers than its results: the high word of its last multiply, a search's index, a magnitude it took. Where a contract (Symbols.tsv, ADR-010) says a register is preserved, the comparison checks that the native routine leaves exactly what the original does there. A routine that computes values and returns them does not compute those leftovers, and nothing says which of them some caller reads.

**Leaving a leftover undefined does not show.** A routine that simply does not write a register leaves whatever its caller had there. That is often what the original left as well, so the comparison and the digests agree by accident, and the dependency stays hidden until some other change moves it.

## Decision

**1. A de-assembled routine takes values and gives values back, over a `GameState`** (`GameLogic/GameState.h`).
- **The `GameState`** is the game's memory without the registers: the data segment by name (`DataOverlay.h`) or by offset, the code segment the original keeps data and patches in, and the CGA's video memory.
- **It reads and writes through `Machine::Memory`,** so paced time's change count and the comparison's journals see every byte as they did. Plain structs replace it in D17's third step.
- **Results** that are more than one value come back as small aggregates (`SinCos`, `Pair`, `Vector`, `Angles` and so on). Signed words are `std::int16_t`; angles, 2048 to a turn, are `std::uint16_t`.

**2. Typed views name the records the routines used to compute addresses into.**
- **The first is `ObjectSlot`** (`GameLogic/ObjectSlot.h`), one of the 36 records of 64 bytes at `shipSlots`. It holds a `GameState` and the slot's offset, and names the fields Reference-Map.md lays out.
- **A view grows with the routines that use it.** Each subsystem adds the fields it reads and writes, and no more.

**3. Every write the original makes stays, in the original's order.** Memory is compared after every call and digested, so a write is not a leftover to drop. That includes the scratch words (`rotateScratch`) and the divide trap's saves of BX and DS in the code segment.

**4. An entry keeps the register contract** for the hook and for every caller not yet converted.
- **What it does.** `…Entry(Guest&)` reads the routine's inputs from the contract's registers, calls the routine, and writes its results back.
- **Who calls it.** The hook, and native code still on registers; a converted caller calls the routine.
- **When it goes.** When neither the hook nor any register caller needs it.

**5. Poisoning makes undefined leftovers visible.**
- **The mechanism.** An entry hands `Guest::Clobber` the registers its contract leaves to the routine. With `NativeCode::SetPoisoning(true)`, `Clobber` fills each with a marked value: `A5A1h` plus the register's index, AX `A5A1h`, BX `A5A2h`, CX `A5A3h`, DX `A5A4h`, BP `A5A7h` and so on. A caller that reads one is unlikely to agree with the original by chance, and a comparison or a digest shows that it does not.
- **Where it is on.** Everywhere a test installs native routines: every `ComparisonRig` comparison, every twin (`TwinRig`), and both of `CorpusTests`' runs.
- **Where it is off.** In the game. `Clobber` then does nothing.

**6. The rule for contracts.**
- **An entry clobbers exactly what its contract declares,** and nothing else.
- **A contract may be widened only with poisoning as the evidence.** Every comparison and every digest must still agree with the widened registers poisoned, and the callers in code must be read to agree.
- **Once poisoning finds a reader, the entry reproduces the leftover** the original leaves, and the contract narrows so that the comparison checks it.

**7. What the arithmetic's pilot found.** The first run with poisoning on failed six tests. The native corpus kept all 40 digests throughout.

| Routine | What poisoning showed | Resolution |
|---|---|---|
| `VectorWithinBox`, `ObjectWithinBox` | `UpdateObjectsAndSpawn`'s collision path (51BE, 51D5) carries on with the magnitudes the test leaves in AX, BX and CX. Poisoned, `playerHitPending` (DS:4262) came out 02 where the original has 01. | The entry leaves \|x\| in AX, \|y\| in BX once x is inside, and \|z\| in CX once y is too. The contract narrows from AX, BX, CX and CF to CF alone. |
| `ComputeAnglesToObject`, `ConvertVectorToAngles` | The original keeps the first angle in BP. It escaped `UpdateStationAi`, whose contract compares BP: FFE7 in the original, `A5A7h` poisoned. | The entry leaves the first angle in BP. The contract narrows from CX, DX and BP to CX and DX. |
| `TransformToView`, `TransformToViewWithBlip`, `BuildDodoVertices` | DX: the last rotation's leftover. Their contracts preserved it, and no caller reads it. | Widened to clobber DX. |
| `RunDockingComputer` | BX, CX and DX: `ArcTangent2`'s search leftovers. No caller reads them. | Widened to clobber BX, CX and DX. |
| `RotatePitchYawRoll`, `RotateRollYawPitch`, `RotateBySinCos7210` | DX, the last `RotateBySinCos`'s leftover. No caller reads it. | Widened to clobber DX. |

**8. Measured 2026-10-10,** g++ and clang++ Release.
- **`Maths`.** Its 20 routines, and the helper `SinCosOf`, hold no register use. The 19 left in `Maths.cpp` outside the entries are the four divide helpers and the int 0 handler, which model DIV for code that still holds its operands in registers. 121 call sites in other subsystems now call the entries, a rename only.
- **The suites.** `GameLogicTests` 173 of 173 pass with poisoning on, and `MachineTests` 155 of 155, one of them new, for poisoning itself.
- **The corpus.** The six replays reproduce all 40 digests on the `Dispatcher` with poisoning on.
- **Coverage.** The coverage check is unchanged (ADR-010 item 5).

**9. The order is the routines' call graph, not the subsystems'.**
- **The subsystems call one another in cycles.** For example, Ships calls Flight's `EraseScannerBlip`, and Flight calls Ships' spawners; Combat and Ai call each other. A subsystem therefore cannot wait until everything it calls is converted.
- **The routines nearly form a DAG.** Measured over `GameLogic/*.cpp` from direct calls and `Guest::Call` with a named address:
  - 629 functions take a `Guest`, on 20 levels;
  - 192 of them are leaves;
  - there are two cycles, each within one subsystem: five routines of Galaxy's text expansion, and two of SaveLoad's disc prompt.
- **Indirect calls are not in that count.** Seven sites call through a table or a parameter: the behaviour handlers, the screens, the blueprint handlers and the timer's samples. Their dispatchers sit high in the graph and get native tables when they are converted.
- **So the order is level by level.** A routine is de-assembled once everything it calls is. Within a level, the subsystems are independent, and a cycle is converted as one unit.

## What this forecloses

- **A de-assembled routine that takes a `Guest` or reads a register.** It takes a `GameState`, views and values; only its entry knows the contract.
- **Dropping or reordering a write the original makes,** before D17's third step puts plain structs behind the `GameState`.
- **Widening a contract on reading alone,** without the poisoned comparisons and digests agreeing.
- **Poisoning in the game,** or a test that runs native code with it off.
