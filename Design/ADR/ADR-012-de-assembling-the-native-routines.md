# ADR-012 — De-assembling the native routines: the GameState, typed views, entries and poisoning

**Status:** accepted 2026-10-10, with the change that implements it: the arithmetic (`Maths`), the first subsystem of Phase 4's second step (D17, ADR-011 item 7). Amended the same day with levels 0 to 4 of the call graph (items 10 to 14) and level 5's first slices (item 15).

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
- **The code's contract is the contract.** Symbols.tsv's contract column is Phase 1's reading of the listing, and the narrowings poisoning finds are recorded here, not written back into it.

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

**12. Level 2, measured 2026-10-10.** Three workers converted 65 routines. 36 were ready by item 11's count. 28 more had been counted as needing a device only because they call converted routines by address, through `Guest::Call`; such a call becomes a call of the value routine, so they were ready too. The last was `RunTextControlCode`, which shares a cycle with four of Galaxy's ready routines (item 9) and was converted with them as one unit.

| | Count |
|---|---|
| Routines converted | 65, none skipped: Flight and Video 18; Text, Galaxy, Market, Docked, Equipment, SaveLoad and Sound 29; Ships, Scene, Ai, Docking and Hyperspace 18 |
| Contracts narrowed, because poisoning found a caller reading a leftover | 5 |
| Contracts widened | 0 |
| Entries added | 40 |
| Lines touching a register in `GameLogic/*.cpp` | 4,712 before, 4,445 after |
| Routines ready for the next level | 17, and 43 more that need a device or the stack |
| Register functions left | 338 on 19 levels: 319 routines and 19 register adapters |

Measured as in item 11, with a routine that calls converted routines only by address now counted as ready.

The 5 contracts poisoning narrowed:

| Routine | The leftover its callers read |
|---|---|
| `EraseScannerBlip` | everything: it now preserves all registers. `IsObjectNear` hands its BX, CX and DX on to `EngageJumpDrive`, `CheckShipInRange` and `IsMassLocked`, and `RemoveObject` stored a poisoned byte at DS:84D2. |
| `DrawLine` | ES = DS, which `DrawLaserBeams`, `UpdateStardust` and `DrawClippedLine` go on with |
| `FacePlayer` | BP, the pitch, which `UpdateStationAi` reads through `SpawnRandomTrader` and `FacePlayerWithRandomRoll` |
| `StartPlayerDeathSound` | AX: AL = 3Ch and AH as it came in, which `DrawSunOrPlanet` and `UpdateMissileAi` go on with after `KillPlayer` |
| `StartExplosionSound` | AX: AL = 32h and AH as it came in, which `UpdateMissileAi` reads after `ExplodeObject` |

**Leftovers dropped on evidence.** Where register code no longer reproduces a leftover, a worker poisoned it for a trial run and kept the change only when every test passed: `ApplyPitch`'s AX to DX after `UpdatePlayerMotion` (774 calls); the Within adapters' AX, BX, CX and flags; DX after `TurnToVector`; `RunDockingComputer`'s BX, CX and DX in five of its states; AX and BX after `RotateVerticesToView`; DX after `RandomArrivalOffset`. A sensitivity run that also poisoned registers those paths do read failed the corpus and the docking tests, so the paths are exercised.

**The description text's control codes.** `RunTextControlCode` jumps through `textControlCodes`, and its `Push` and `Pop` only save SI and the way back to CS:7051, so the stack carries no data and the cycle converts. It is now a table of the six handlers' value routines.
- **Codes 7 to 31** make the original run `descriptionPhraseLists` as code, and the value routine runs nothing. No game state reaches them: the 39 phrase lists hold only codes 1 to 6 and 80h to A6h, and the longest description over all 2,048 systems is 144 bytes against a 256-byte buffer, so no expansion overwrites the table. That was measured with a scratch model of the expansion, outside the repository.
- **The inserting handlers' registers** are reproduced for a name made of letters, which every name from `systemNameDigrams` is. Their memory writes are exact for any name.

**`UpdateMessageLine`** leaves AX from the 0 that `ClearMessageLine` leaves where the original's starts from B800h, for an empty message only. The game shows none.

**A routine called only as a value is no longer compared on its own.** `ToggleMenuRowHighlight` was reached through its hook from the equipment and cargo menus; their cursor routines now call its value routine, and the menus that call them wait, so only the digests compared it and the coverage check failed. `TextTests.MenuRowHighlightAgreesOnAndOff` now calls it directly. The same will happen to more routines as their callers convert, and each needs a constructed call or a converted caller that is compared.

**The suites.** `GameLogicTests` passes 174 of 174 with poisoning on, under g++ and clang++, and the coverage check is clean. The corpus keeps all 40 digests in all three of its runs.

**13. Level 3, measured 2026-10-10, and what is ready from level 4 on.** One worker converted the 17 routines ready after level 2, none skipped. With them, Input's and StartUp's device routines moved onto `Hardware` (ADR-014 item 8).

| | Count |
|---|---|
| Routines converted | 17, and the device routines ADR-014 item 8 lists |
| Contracts narrowed, because poisoning found a caller reading a leftover | 6 |
| Contracts widened | 0 |
| Lines touching a register in `GameLogic/*.cpp` | 4,445 before, 4,211 after |
| Register functions left | 302 on 19 levels: 280 routines and 22 register adapters |

The 6 contracts poisoning narrowed:

| Routine | The leftover its callers read |
|---|---|
| `IsObjectNear` | everything but CF: AL, the last high byte it compared, and `EraseScannerBlip`'s registers once it erased a blip, which `IsMassLocked`, `UpdateSafeZone`, `EngageJumpDrive`, `CheckShipInRange` and the AI read |
| `RemoveObject` | all it clobbered but CX: `ExplodeObject`, `TryScoopObject`, `TransformShip` and `UpdateMissileAi` read the rest |
| `KillPlayer` | AX: AL = 3Ch from the death sound, with AH as it came in |
| `FacePlayerWithRandomRoll` | BP, the pitch, and AX, the roll, which `UpdateStationAi` reads |
| `EraseCompassAndBlips` | DS and ES, which the flight screens' drawing goes on with |
| `ShowSystemDescription` | DS |

**Two corrections to the Phase 3 port.**
- **`TryFireLaserAtPlayer`'s second box test** takes x from AX after `MOV AL,[DI+3Ch]`, the depth byte. A first draft used 0, and the corpus caught it: in `attack-the-station`, `UpdateObjectsAndSpawn` call 32 left `playerHitPending` 01 where the original leaves 02.
- **`PayBounty`** collapsed the countdown's SUB and the 1 written over a borrow into one write. It now makes both, as the listing does.

`CreditKill`, still register code, collapses its INC and its `MOV FEh` on `killCount` (CS:8BCD, CS:8BD8) the same way. It is fixed when `CreditKill` converts.

**From level 4 on, a routine is ready when everything it calls is converted.** Its own port accesses and services go through `Hardware` (ADR-014), its loops' turns through `Hardware::LoopTurn` (ADR-015), and a value it pushes and pops itself becomes a local, so none of them holds it back any more.
- **Routines that pass data on the stack to one another,** or that jump back into another routine's loop, convert as one unit with the routine whose loop it is. Market's `OpenCargoMessage` and `ShowCargoMessage`, for example, convert with the cargo menu, not on their own.
- **A unit reports every backward jump the original takes,** a shared tail's included, so that paced time's last turn stays the original's. A tail's jump needs only its address: it is never idle, because the jump before it always lands elsewhere.

By that rule, 55 routines are ready for level 4.

**The suites.** `GameLogicTests` passes 176 of 176 with poisoning on, under g++ and clang++. The coverage check is clean, and the corpus keeps all 98 digests in all three of its runs.

**14. Level 4, measured 2026-10-10:** the first level of routines that wait, drive a device or use the stack. Four workers converted 44 of the 55 routines item 13 counted ready, on ADR-014's devices and ADR-015's loop turns.

| | Count |
|---|---|
| Routines converted | 44: Flight, Video and Scene 16; the docked screens 5; SaveLoad, StartUp, Timer, Input and Maths 10; Combat, Docking, Hyperspace and Ships 13 |
| Left, as fragments of a unit whose owner is not ready | 11 |
| Contracts narrowed, because poisoning found a caller reading a leftover | 6 |
| Contracts widened | 0 |
| Lines touching a register in `GameLogic/*.cpp` | 4,211 before, 3,896 after |
| Register functions left | 265 on 18 levels: 236 routines and 29 register adapters |
| Routines ready for level 5 | 37, of which the 11 fragments still wait for their units |

The 6 contracts poisoning narrowed:

| Routine | The leftover its callers read |
|---|---|
| `FillTriangleSpan` | AX and BP: `DrawVisibleFaces` hands them back to `RenderBlueprintBody`, whose contract compares every register |
| `MoveObject` | everything it clobbered: `UpdateDriftingObjectAi` and `UpdateMissileAi` read AX to DX and ES, and the spawning that follows stores them |
| `TakeDamage` | AX and BX, which `UpdateMissileAi` reads |
| `RemoveAllMissiles` | DI and ES, which `UpdateStationAi` reads |
| `CopyProtection` | ES: `Start` goes on to `InstallDivideAndKeyboardInterrupts`, which takes ES as the interrupt table's segment |
| `RestoreTimerInterrupt` | CX: `Start` hands it to `PerformDiskRequest`, whose find-first uses it as the search's attributes, and DOS writes its low byte into the transfer area, which is digested |

**The units.** A fragment that passes data on the stack, or jumps back into another routine's loop, waits for the routine whose loop it is (item 13):
- the cargo menu, `RunCargoTradeMenu`, with `OpenCargoMessage` and `ShowCargoMessage`;
- the equipment menu, `RunEquipShipMenu`, with `OpenEquipMessage`, `ShowEquipMessage`, `ReportNotEnoughCredits` and `SellFuelOrMissile`;
- the trade screens, `ShowSellCargoScreen` and `ShowBuyCargoScreen`, with `PrintNameAndPrice`, `PrintUnit` and `EndTradeRow`;
- `ReadTextLine` with `RedrawTypedLine`, which jumps into its blink loop;
- the triangle filler, `FillTriangle` and `FillClippedTriangle`, with `PushClippedSpan` and `DrawStackedSpansFromRow`, which pass spans on the stack.

Most of these menus wait on `GetKey`, `ReadSteering` and `ReadTextLine`, which are the input routines left.

**Write order.** Four more routines now make writes the Phase 3 port had collapsed: `CreditKill`'s INC and FEh on `killCount` and its ADD and FFh on `legalStatus`; `UpdateFuelLeak`'s SUB and 0 on the fuel; `HandleFireButton`'s ADD and FFh on the laser's temperature. Combat's register-side `AddSaturating` still collapses the sum and FFh for `HitTarget`, `DetonateEnergyBomb` and `UpdateMissileAi`, and is fixed when they convert.

**Coverage.** `DrawTunnelRectangle` lost its only compared caller to a value call, and `DockingTests.DrawTunnelRectangleAgreesOnEveryRectangle` now compares it on all ten of the tunnel's rectangles.

**Hooks.** `CopyProtection` reports its loops' turns now, so it is hooked as a routine that sometimes waits. The D5 byte keeps the game off that path.

**The suites.** `GameLogicTests` passes 177 of 177 with poisoning on, under g++ and clang++. The coverage check is clean, and the corpus keeps all 98 digests in all three of its runs.

**15. Level 5's first slices, measured 2026-10-10.** From level 5 on, a worker owns files, not a list: it converts bottom-up whatever in its files is ready, then what its own conversions make ready (item 13). Three slices are in.

| | Count |
|---|---|
| Routines converted | 42: the input chain (Maths, Input, Text and `SaveScreenshot`) 17; the docked frame, the escape pod, the mounts and the disc request 14; the renderer's frame waits, screens, presenters and transforms 11 |
| Contracts narrowed, because poisoning found a caller reading a leftover | 5 |
| Contracts widened, because no caller reads what they compared | 2 |
| Lines matching `regs.` or `Regs()` in `GameLogic/*.cpp`, a stricter count than item 14's | 3,759 at level 4's end, 3,503 after |
| Register functions left | 231: 194 routines and 37 register adapters |
| Routines ready | 61 |

The 5 contracts poisoning narrowed:

| Routine | The leftover its callers read |
|---|---|
| `GetKey` | CF, PF, AF, SF and OF besides ZF and IF. The register loops that wait for a key compare every flag from turn to turn (ADR-008 item 1); with these poisoned, six twins and `attack-the-station` stopped as `Spinning`. The entry leaves `AND AH,AH`'s flags. |
| `LaunchEscapePod` | DS: it was said to clobber every register, and with DS poisoned `escape-pod`'s `ejected` digest moved |
| `ShowCockpitScreen` | SI and ES. `RestoreFlightScreen`, whose contract compares every register, ends with its SI; with ES poisoned, `death`'s title digest and six twins moved. |
| `DrawChartFrame` | ES, through which the charts draw their titles |
| `FinishSpaceViewFrame` | DX, 1FF0h as `PresentSpaceView`'s copy leaves it: poisoned, `hyperspace-and-fight`'s `pirates` digest moved. The instruction that reads it has not been found; poisoning is the evidence. |

The 2 contracts widened, each with every test passing poisoned and every caller read:

| Routine | What it no longer compares, and why |
|---|---|
| `TransformShip` | DX. Its one caller, `ClassifyObject` (CS:3D87), returns to `TransformAndDrawObjects`' first pass, which sets DX at CS:3DA2 before it reads it, and nothing on the way reads it. |
| `TransformSunOrPlanet` | DX and BP, which it was said to preserve. The same first pass sets both (CS:3D9E–3DA2) before it reads them. |

**The divides.** `DivideByte`, `DivideWord`, `DivideSignedWord` and `DivideUnsigned` are value routines that return the quotient and the remainder. The divide trap, now `DivideOverflowInterrupt`, keeps its two saves into the code segment, in order. Register code still calls the divides at 28 sites in seven subsystems, through four `…OnRegisters` adapters, which go when their last register caller converts.

**A unit.** `ReadTextLine` converted with `RedrawTypedLine` and `RestartInputBlink`, its turns carrying the length typed. With the input chain converted, the menu units of item 14 wait only on their own routines and on the renderer.

**A parameter the original passes by accident.** `ReadSteering` and `ReadJoystickSteering` take a trigger byte: the AL their caller left, which the stick read writes to port 201h. A converted caller passes what the original holds in AL there.

**Write order.** `FitLaserOnMount` writes `laserMountTypes`' AND and then its OR (CS:6463 and CS:6467), where the Phase 3 port made one write.

**Interrupts.** `SaveScreenshot` takes the interrupts that are due where its callees' hooks took them (ADR-014 item 10).

**Two simplifications, each commented in the code:**
- **A key code of 0.** The original shifts AL left once for each code `GetKey` takes, Shift in bit 0. `WaitForKeyPress`' entry rebuilds AL for the code that ends the wait only. A code of 0 would end no wait, and the original would have shifted AL once more for it. The shell drops a key event with scan code 0 (`Engine/Window.cpp`), so the port never sends one.
- **AL in two signatures.** `WaitForKeyPress` and `ReadTextLine` leave AL out of their turns' signatures, as ADR-015 item 2 allows.

**Coverage.** Three routines lost their only compared callers to value calls inside routines that wait. Constructed tests now compare them:
- `EquipmentTests.RedrawEquipHelpTextAgreesFromEitherAttribute`;
- `TextTests.InputLineRedrawAgreesInBothLayouts`, for `ToggleInputCursor`, `RedrawInputLine` and `PrintStringForLayout`;
- `VideoTests.LaserSightsAgreeForEveryLaser`, for Combat's `DrawLaserSights`.

**The suites.** `GameLogicTests` passes 180 of 180 with poisoning on, under g++ and clang++, without warnings. The coverage check is clean, and no digest moved.

## What this forecloses

- **A de-assembled routine that takes a `Guest` or reads a register.** It takes a `GameState`, views and values; only its entry knows the contract.
- **Dropping or reordering a write the original makes,** before D17's third step puts plain structs behind the `GameState`.
- **Widening a contract on reading alone,** without the poisoned comparisons and digests agreeing.
- **Poisoning in the game,** or a test that runs native code with it off.
