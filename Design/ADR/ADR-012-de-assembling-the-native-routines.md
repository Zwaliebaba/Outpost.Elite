# ADR-012 — De-assembling the native routines: the GameState, typed views, entries and poisoning

**Status:** accepted 2026-10-10, with the change that implements it: the arithmetic (`Maths`), the first subsystem of Phase 4's second step (D17, ADR-011 item 7). Amended the same day with levels 0 and 1 of the call graph (items 10 and 11).

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
- **The first is `ObjectSlot`** (`GameLogic/ObjectSlot.h`), one of the 64-byte records at `shipSlots` and `debrisSlots`. It holds a `GameState` and the slot's offset.
- **Its fields are two enumerations, `SlotByte` and `SlotWord`,** read and written with `Get` and `Set`. A byte field cannot be read as a word, nor a word as a byte.
- **One name per use.** Five files named the slot's fields with constants of their own, and they disagreed. Flight called +3Dh the scale shift where Scene called it the depth and gave that name to +0Ah; Flight's view position at +20h is Scene's compass. An offset that holds different things for different kinds of object now has an enumerator for each, and the files' constants go as their routines are converted.
- **A view grows with the routines that use it.** Fields no routine yet names are added when one does.

**3. Every write the original makes stays, in the original's order.** Memory is compared after every call and digested, so a write is not a leftover to drop. That includes the scratch words (`rotateScratch`) and the divide trap's saves of BX and DS in the code segment.

**4. An entry keeps the register contract** for the hook and for every caller not yet converted.
- **What it does.** `…Entry(Guest&)` reads the routine's inputs from the contract's registers, calls the routine, and writes its results back.
- **Who calls it.** The hook, and native code still on registers; a converted caller calls the routine.
- **When it goes.** When neither the hook nor any register caller needs it.

**5. Poisoning makes undefined leftovers visible.**
- **The mechanism.** Every entry hands `Guest::Clobber` its contract. With `NativeCode::SetPoisoning(true)`, `Clobber` marks what the contract leaves to the routine:
  - **each register the contract clobbers** gets a marked value, `A5A1h` plus the register's index: AX `A5A1h`, BX `A5A2h`, CX `A5A3h`, DX `A5A4h`, BP `A5A7h` and so on;
  - **each status flag the contract does not name** is set on one call and cleared on the next. A flag has no unlikely value, but a caller that reads it, reached twice, sees both.

  A caller that reads a marked register or flag is unlikely to agree with the original by chance, and a comparison or a digest shows that it does not.
- **Why flags too.** ADR-010 compares only the flags a contract names, on the ground that no caller reads the others. The contracts in Symbols.tsv were written by hand, so poisoning is the first machine check of that ground.
- **Where it is on.** Everywhere a test installs native routines: every `ComparisonRig` comparison, every twin (`TwinRig`), and both of `CorpusTests`' runs.
- **Where it is off.** In the game. `Clobber` then does nothing.
- **Where a mark travels.** A compared call through a hook ends with the run carrying on from the original's registers and flags (ADR-010), so a mark goes no further than the comparison of the call itself. Marks travel through native code that calls an entry directly, and through every uncompared run. Those runs are where a reader shows: in the caller's comparison, or in a digest.

**6. The rule for contracts.**
- **An entry clobbers exactly what its contract declares,** and nothing else.
- **A contract may be widened only with poisoning as the evidence.** Every comparison and every digest must still agree with the widened registers poisoned, and the callers in code must be read to agree.
- **Once poisoning finds a reader, the entry reproduces the leftover** the original leaves, and the contract narrows so that the comparison checks it.

**7. What the arithmetic's pilot found.** The first run with registers poisoned failed six tests. The native corpus kept all 40 digests throughout. Poisoning flags as well, added afterwards, found no caller that reads a flag Maths' contracts leave unnamed.

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
- **A routine is ready when its callees are converted, entries included.** A caller of an entry can call the routine behind it instead. So readiness counts an entry as converted, and the leaves' callers become the next level's work.
- **Routines that need a device wait for D17's third step.** That means port I/O, a call to DOS, the BIOS or the mouse driver, a wait, or the stack. A `GameState` has no ports, and the devices become native there.

**10. Level 0, measured 2026-10-10.** The 144 leaves that need no device were converted by four workers in parallel, one per group of subsystems.

| | Count |
|---|---|
| Routines converted | 144: Flight and Video 34; Galaxy, Text, Market and Docked 38; Ships, Scene, Combat and Ai 34; the eight others 38 |
| Leaves left for D17's third step, because they need a device | 32 |
| Contracts narrowed, because poisoning found a caller reading a leftover | 13 |
| Contracts widened | 0 |
| Entries, each handing `Clobber` its contract | 110, of which Maths has 20 |
| Lines touching a register in `GameLogic/*.cpp` | 5,525 before, 5,167 after |
| Routines ready for the next level | 81, and 52 more that need a device. The 83 first written here counted two register adapters, which are not routines. |
| Register functions left | 467, on 19 levels |

The 13 contracts poisoning narrowed:

| Routine | The leftover its callers read |
|---|---|
| `DrawScreenChar`, `DrawViewChar` | AX, the last glyph row drawn, and `DrawViewChar`'s BP, the paper |
| `FormatDecimal5` | AX, the units |
| `PrintTextModeString` | AX, the text attribute |
| `TerminateSelectedSystemName` | BX, the name's length |
| `XorDashboardPixel` | AX, BX and ES = B800h |
| `DrawMissileLockIndicator` | CX = 0 after it draws. `DrawMissileIcons` hands CH on to the next bar, and a poisoned CX corrupted the dashboard's digest. |
| `InvalidateDashboard` | AX, CX and DI |
| `ReflectVertexAboutCenter`, `OffsetVertexByCenter` | the draw centre's z |
| `RunVertexProgram` | CX = 0. `RenderBlueprintBody` loads only CL and stores all of CX as the vertex count, and a poisoned CX stopped the run. |
| `TriangleWindingSign` | everything but CX and DI: the product in DX:AX, BX, and SI, BP and ES untouched |
| `ClampTurnStep` | CX, the turn rate |

**Write order.** Nine routines now make the writes the original makes where the Phase 3 port had collapsed them; ADR-010's comparison of final memory could not tell the difference. Each now matches the listing:
- `SpeedUp`, `SlowDown`, `HandleAntiEcmKey` and `UseMaskingDevice`: the subtraction, then the floor written over it;
- `IntegrateRate`: the rate zeroed when a key reverses it;
- `Plot` and `DrawSmallViewChar`: the AND, then the OR;
- `TwistSystemSeeds` and `NextMarketRandom`: the seeds in the order of the original's exchanges.

`DrawSmallDisc` still collapsed its AND and OR; level 1 split them (item 11).

**11. Level 1, measured 2026-10-10.** The 81 routines ready after level 0 were converted by three workers, one per group of subsystems. Each calls only converted routines, and it calls their value routines, not their entries, so poisoning now sits only where register code meets converted code.

| | Count |
|---|---|
| Routines converted | 81, none skipped: Flight and Video 27; Text, Galaxy, Market, Docked, Equipment, Docking, Input, Sound and StartUp 30; Ships, Scene, Ai and Combat 24 |
| Contracts narrowed, because poisoning found a caller reading a leftover | 15 |
| Contracts widened | 0 |
| Entries added | 57 |
| Lines touching a register in `GameLogic/*.cpp` | 5,167 before, 4,712 after |
| Routines ready for the next level | 36, and 72 more that need a device |
| Register functions left | 389, on 19 levels |

Measured with item 9's call graph, entries counted as converted and the register adapters (`…OnRegisters`) left out; a line touching a register is one that names `regs.` or `Regs()`.

The 15 contracts poisoning narrowed:

| Routine | The leftover its callers read |
|---|---|
| `FillSpan` | DI, the span's last byte, which `DrawTitlePlanet` and `DrawSunOrPlanet` go on from |
| `XorCompassDot` | AX and BX: the last pixel and its mask |
| `XorScannerBlip` | BX, CX, DX and ES: the mask, the step, the last pixel and B800h |
| `MoveObjectsByVelocity` | AX, `playerVelocityZ`, and SI, past the last slot |
| `ResetStardust` | AX, the last random number, and DI |
| `DrawSignedIndicator` | CX = 0. With CX poisoned, the next bar ran A5h times and wrote about 50,000 bytes. |
| `DrawMissileIcons` | CX: 0 when it draws, otherwise CL = `missileCount` |
| `PrintCountedTextLines` | AX, the attribute with AL = 0, and CX = 0 |
| `CopySelectedNameLower` | AX: AL = 0, and AH as it came in |
| `GenerateSystemName` | AX, the fourth seed pair, which `InsertRandomName` reads through `CopySelectedNameLower` |
| `MaskOutsideTunnel` | SI, from which `PlayStationTunnel` draws its rectangles |
| `ComputeMarketPrices` | DS |
| `IsDebrisType` | AX: AL, the type, which `UpdateDriftingObjectAi` hands back |
| `ComputeVelocity` | BX, the z velocity word |
| `TurnTowardAngles` | CX, the turn rate, and BP, the pitch error's magnitude |

**What else changed shape.**
- **Helpers removed.** Ships' `SetCompareFlags`, `CompareType` and `TypeMatch`: the type tests' entries set only ZF, the one flag their contracts name. Flight's `SetSinCosPair` and `RotateByPair`: their 28 callers call Maths' value forms.
- **Register adapters added.** `RotateToViewDirectionOnRegisters`, `SpawnOddsMetOnRegisters` and `TakeFreeShipSlotOnRegisters` serve register code that calls a routine now converted. Each goes when its last caller converts.
- **Register images.** `DrawDisc` rebuilds AX, BX, CX, DX and DI from two small structs, `SmallDiscPlace` and `SmallDiscEnd`, because the contracts of `DrawDistantStation` and `DrawSunOrPlanet` compare every register at their end. The structs go when those two convert.
- **Views.** `ObjectSlot` names four more bytes (`XMiddle`, `YMiddle`, `ZLow`, `ZMiddle`) and one word (`Color`, the sun's and the planet's).
- **Write order.** `DrawSmallDisc` now writes its AND and then its OR (CS:19D4 and CS:19E0), as the listing does. Every other routine kept the original's writes, in order and at their width, checked against the listing.

**The suites.** `GameLogicTests` passes 173 of 173 with poisoning on, under g++ and clang++. The corpus keeps all 40 digests in all three of its runs.

## What this forecloses

- **A de-assembled routine that takes a `Guest` or reads a register.** It takes a `GameState`, views and values; only its entry knows the contract.
- **Dropping or reordering a write the original makes,** before D17's third step puts plain structs behind the `GameState`.
- **Widening a contract on reading alone,** without the poisoned comparisons and digests agreeing.
- **Poisoning in the game,** or a test that runs native code with it off.
