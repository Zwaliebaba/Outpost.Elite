# ADR-010 — Native routines, and how each is proved

**Status:** accepted 2026-10-10, with the change that implements it. That change contains:

- the hooks and the comparison in `Machine` (`NativeCode`, `Pc::Hook`, `Pc::CallNear`);
- the data overlay and `Guest` in `GameLogic`;
- the corpus run three ways;
- the first 23 entries ported: the maths leaves.

It is the machinery plan §5's Phase 3 and §6.3 describe, and ADR-003 item 3 requires. **Amended 2026-10-10:** native code waits on a thread of its own (item 8), and each subsystem has its own file (item 9). **Amended again with the first 262 entries,** every subsystem's work routines:

- a compared call leaves the original's whole outcome (item 4);
- constructed tests' coverage is measured, and CI checks coverage (item 5);
- a routine that waits only sometimes is compared when its original does not wait (item 8);
- native code that waits where it cannot stops the run (item 8);
- a routine that always waits is accepted on the digests, with the coverage of interpreted runs (item 5).

**Amended a third time with the last 125 entries, Phase 3's exit:** the corpus runs no original instruction (item 7). This change also:

- ports the routines that wait, through `Guest::JumpBack`, and unwinds native code that a call returns past (item 8);
- lets a host's call run beside a routine that waits, and native code count the cycles of a poll loop it stands in for (item 8);
- gives `TwinRig` its options, an uncompared twin among them, and makes a stale reason fail the coverage check (item 5);
- names what the ported routines still do differently from the original (item 10).

## Context

**Phase 3 replaces the original's routines with C++, bottom-up through the call graph** (plan §5). The interpreter keeps running whatever is not yet ported.

**The test that admits a replacement** (ADR-003 item 3, plan §6.3) compares it with the original:

- on the calls a replay makes;
- on every memory write, port write and register its callers read;
- with basic-block coverage of the original, or a written reason for each block it leaves out.

**The digests must not move** (plan §5's exit). Paced time (ADR-008) already lands interrupts where the program waits, whatever its work costs. So a native routine changes no digest if it does exactly what the original did.

**Plan §3 asked for the data segment to be named from the first routine**, as a packed struct overlay, "so native code reads `m_ds.shipEnergy` rather than `Byte(0x3A1F)`".

## Decision

**1. A native routine stands in for the original at an entry.**

- **Hooking.** `Pc::Hook(segment, offset, name, routine, contract)` puts a C++ function at an address.
- **Where the CPU stops.** `Cpu` stops at a hooked entry, at the start of a step, after taking any interrupt that was due. It executes nothing there, so an interrupt handler's entry can be hooked too.
- **Running it.** `Pc` then runs the routine. It takes no time, which is what paced time expects of work (ADR-008).
- **What `Machine` knows.** Only that native code is at an address. The game's routines, names and contracts live in `GameLogic` (`NativeRoutines.cpp`), and R9 holds.

**2. Bodies and entries are separate.**

- **The body.** A ported routine's body is a function of `Elite::Guest&`. It works on the registers of the contract Symbols.tsv gives the routine, and touches no return address.
- **Calling native from native.** Native code calls a native body as a plain C++ call.
- **Calling the original.** Native code calls code still interpreted through `Guest::Call` (`Pc::CallNear`):
  - a near call with a return offset nothing executes (FFFFh);
  - the program runs until it comes back there.
- **The hooked entry** is the body, followed by the return the original makes (`Pc::ReturnNear`).
- **Shared bodies.** Where several entries share one body, they share its C++ function: the nine `SetSinCosN` and nine `RotateBySinCosN` entries.

**3. The data overlay is generated, and it is not a struct laid over the bytes.**

- **The generator.** `Tools/GenerateOverlay.py` writes `GameLogic/DataOverlay.h` from Symbols.tsv. Every DS row of kind data, table or text becomes a field, typed from its size and a table's element type: `DataField<std::uint8_t>`, `DataField<std::uint16_t>`, `DataBlock<N>`, `DataTable<entry bytes, entries>`, or `DataAt` when the table does not say.
- **How native code uses it.** It reads `guest.Get(DS.randomState0)` through `Guest`.
- **Kept current in CI.** CI fails if the header in the tree is not what the tool would write (`--check`).
- **Why not a struct over the bytes.** Plan §3's packed struct is set aside because every write must pass through `Machine::Memory`:
  - paced time decides that a loop is waiting when no byte has changed (ADR-008);
  - item 4's journal records every byte a run changes;
  - a struct laid over the bytes would bypass both.
- **What carries over.** The names, and the readable tree they were meant to give, are the same. Phase 4 re-lays the overlay as typed state, as the plan says.

**4. Every call can be compared with the original, inline, as it is made.**

When `NativeCode::SetVerifying(true)` is on, a call of a native routine that is not already inside a comparison goes through these steps:

1. **The original runs first, all of it, with no hook in force.** Every native routine it reaches is therefore compared through it. That includes routines only native code calls, such as `QuadrantArcTangent` inside `ArcTangent2`. Every byte it changes is journalled (`WriteJournal`, with what the byte held before), and every port access it makes is logged. Its run ends when it returns to its caller, or as soon as SP is above the return frame: a routine that drops its own return address returns past its caller (`TickEscapePod`, and `RunPauseScreen`'s abort).
2. **Memory is undone and the registers are put back.**
3. **The native routine runs, with the bus in replay** (`PortRouter::StartReplay`). Each access must be the next one the original made: a read returns the value the original read, and a write is compared but not made again.
4. **The two are compared:**
   - **Registers:** every register but those the contract says the routine clobbers. SP, SS, CS and IP are always compared.
   - **Flags:** those the contract names as results.
   - **Memory:** every byte either run changed, except the dead stack below SS:SP once the routine has returned.
   - **Ports:** the sequence of accesses.
5. **Whatever it finds, the run carries on from the original's outcome.** That outcome is:
   - its memory, the dead stack included;
   - its registers;
   - the counts of changed bytes and port writes, and the last loop turn, which are what paced time saw of it (ADR-008).

   The native run starts from what paced time saw before the call, as the original's did. A compared run is then the interpreted run, observed. A loop around a ported routine is seen to wait exactly when it would be without the comparison, and one mismatch does not hide the next. A mismatch is recorded (`NativeCode::Mismatches`).

A call whose original cannot be undone is not compared: its outcome stands, and it counts as unverifiable. The original cannot be undone when it waited (the clock moved), took a hardware interrupt, or called the BIOS, DOS or mouse services.

**Why inline rather than captured states.** Compared inline, every call in the corpus is a test case. It needs no state files and no snapshot of the devices. Measured 2026-10-10, g++ -O2, the five replays:

| Run | Time |
|---|---|
| Interpreted | 2.7 s |
| Native | 2.5 s |
| Compared (118,228 calls, all verified, none unverifiable, no mismatch) | 2.8 s |

**5. When a routine is accepted.** A routine is accepted when every compared call agrees, and those calls ran every reachable instruction of the routine and of everything it calls or runs into. Each instruction left out needs one of:

- **A constructed input.** A `GameLogicTests` case that calls the entry directly from a state the reference reached itself, such as `MathsTests`, which drives `RatioArcTangent` through the divide trap.
- **A written reason** in `Design/NativeCoverage.tsv`.

**Native reports record what each compared call executed.** Two things write them:

- `ReferenceRunner --compare --native-report` for a run;
- `GameLogicTests` for each corpus replay and each constructed test, under the system's temporary directory.

`Tools/RoutineCoverage.py` checks all of them against MapReference's static walk, and CI runs it after the tests. A constructed input is therefore measured rather than listed, and `Design/NativeCoverage.tsv` holds only reasons.

**A routine that always waits is accepted on the digests.** It is never compared on its own (item 8). What shows that it does what the original did is a native run that reproduces an interpreted run's digests from the same inputs. Its coverage is read accordingly:

- **Its instructions** are those of the routine and of what it alone calls or runs into, stopping at any other hooked entry, which answers for itself.
- **Each must have run in such an interpreted run.** An offsets file records the instructions an interpreted run started:
  - GameLogicTests writes one for each corpus replay, from the interpreted run whose digests the native runs must reproduce;
  - `ReferenceRunner --coverage` writes one for a run.
- **`TwinRig` reaches the states no replay does.** It boots the reference twice, interpreted and native with every call of a work routine compared. It gives both the same memory and the same keys, and requires every digest, and the state both end in, to agree. It writes the interpreted twin's offsets and the native twin's report. `TwinOptions` sets the rest:
  - **`compared = false`** runs the native twin uncompared. A compared call keeps the original's outcome when the original waited or called the BIOS or DOS (item 4), so only an uncompared twin, or a native corpus run, executes the native code of those paths. `SaveLoadTests` runs the disc menu both ways for that reason, and its screenshots uncompared.
  - **`startMoment`** sets DOS's clock at power-on, and **`fromPowerOn`** starts both twins there rather than at the title screen: `StartUpTests` starts at 55 seconds past the minute, where the start-up wait's target second wraps.
- **For every other routine,** the instructions it answers for stop at a routine that always waits.

**Measured 2026-10-10 for 262 entries:** 231 covered and 31 with reasons, 1,879 instructions explained.

**Measured 2026-10-10 for all 387 entries,** `Tools/RoutineCoverage.py` over everything g++'s `GameLogicTests` run left:

| Entries | Count |
|---|---|
| Covered by compared calls | 312 |
| Covered by the digests | 32 |
| Explained, compared | 39 |
| Explained, on the digests | 4 |

Between them the 43 explained entries leave 435 instructions to `Design/NativeCoverage.tsv`. The reasons are of four kinds:

- **Unreachable,** on any machine or on this one. No input takes the path, the D5 byte closes it (ADR-001), or this machine never answers as the path needs: a DOS before 2.0, a BIOS tick count past a day.
- **Only in a call that waits.** These are the flight function-key screens, the pause and the Ctrl+Esc freeze. Such a call cannot be compared (item 8). `flight-screens.replay`'s native run, and the uncompared twins in `FlightTests`, run their native code, and the digests check it.
- **Only in calls that make a BIOS or DOS call.** Item 4 cannot undo those calls. The native corpus runs and the uncompared twins run this code natively, and the digests, and the files the twins write, check it. This covers all of `PerformDiskRequest`, `WriteScreenshotFile`, `RestoreTimerInterrupt`, `SetGraphicsMode` and `SetTextMode`.
- **Not reached.** No replay or test arranges the state, and the reason names it: a host I/O error after DOS opened or created the file, a commander file with attributes, the Amstrad's timer.

A reason for an instruction the runs now cover is stale, and fails the check: it would explain away a regression. The 262-entry file had 1,884 rows, and most had gone stale unnoticed.

**6. What gates in CI.** `GameLogicTests.CorpusTests` runs the corpus three ways:

| Run | Must hold |
|---|---|
| Interpreted | Every digest. |
| Native | Every digest, unchanged, and no stop. A stop includes `StopReason::Overran` (item 8). Since ADR-011 it runs on a `Dispatcher`, which interprets nothing, so it also shows that no original instruction runs. |
| Compared | No mismatch, and no stop. |

The coverage check (item 5) is a CI step of its own, after the tests.

**7. How far Phase 3 has got** is the number of the original's instructions the corpus still executes, from `ReferenceRunner --native --coverage`. The phase's exit is zero. Measured 2026-10-10 as the union over the replays:

| Corpus | Interpreted | Native |
|---|---|---|
| Five replays, with the 23 maths entries | 8,717 | 8,500 |
| Six replays, with 262 entries | 8,849 | 1,943 |
| Six replays, with all 387 entries | 8,849 | **0** |

The 262-entry six-replay run's times, g++ -O2:

| Run | Time |
|---|---|
| Interpreted | 4.0 s |
| Native | 1.4 s |
| Compared | 10.4 s |

The compared run made 1,272,389 calls of ported entries:

- 1,051,544 were compared on their own, and all were verified.
- 24 were unverifiable.
- The rest ran inside a caller's comparison.

With all 387 entries, each replay run on its own by `ReferenceRunner` (g++ Release), the times are summed over the six processes, start-up and screenshots included. That is not how the table above was taken, so the two are not compared:

| Run | Time |
|---|---|
| Interpreted | 3.6 s |
| Native | 0.6 s |
| Compared | 4.1 s |

The compared runs made 1,274,741 calls of ported entries, with no mismatch:

| Entries | Calls | Compared on their own and verified | Unverifiable |
|---|---|---|---|
| 345 that never wait | 1,228,751 | 1,047,240 | 21 |
| 6 that sometimes wait | 8,245 | 4,304 | 3 |
| 36 that always wait | 37,745 | never compared (item 8) | — |

**8. Native code waits on a thread of its own.**

- **Which routines.** Each entry says whether it can wait (`NativeWait`, in `NativeEntry` and `Pc::Hook`):
  - `Never`;
  - `Sometimes`, on some of its paths;
  - `Always`, as a rule.

  A routine that can wait runs on the native thread, and so does everything it calls. It waits in one of two ways:
  - in original code it calls;
  - through `Pc::Wait`, one idle turn of a waiting loop. That turn is `Idle`, then the end of the run if that is where the clock now is, then any interrupt now due, taken as the CPU takes it at the loop's next instruction;
  - through `Pc::LoopTurn`, called wherever the original jumps back, with the registers the original has there. Paced time looks at that turn as it looks at the original's jump: it idles when the turn changed no register, byte or port since the last. The native loop therefore waits on exactly the turns the original does, and the digests do not move. A loop that never idles stops the run as `Spinning`, as the original's would. `Guest::JumpBack(target)` is the form ported loops use: it sets IP to the original's jump target first, so the turn paced time compares holds the IP the original's would.
- **How a run ends inside one.** When the clock reaches the end of a run there, in `Wait` or in original code it calls, the native thread hands the machine back and `RunUntil` returns. The next `RunUntil` carries on where it stopped.
- **Why it stays deterministic.** The host thread and the native thread take turns through two semaphores, so only one runs at a time and a run is still a function of its inputs (ADR-008). `MachineTests` is clean under ThreadSanitizer.
- **Teardown.** A machine destroyed while a routine waits unwinds the native thread by an exception.
- **Returning past a caller.** A routine that drops its own return address returns past the code that called it (`RunPauseScreen`'s abort, `TickEscapePod`, the disc menu leaving `GameLoop`). Native code does it as the original does, popping the words and returning. `Pc::CallNear` then sees SP come back above its frame while a native routine runs on that thread, and throws `ReturnedPast`. `RunNative` catches it and ends that routine without a return of its own. The call that ran the routine makes the same check, so each native routine the original returned past ends in turn. A call a host makes from outside any native routine ends as calls do.
- **A host's call beside a routine that waits.** A test, or the shell, may call into the program while a routine waits suspended on the native thread. That call runs on the host thread, beside the waiting routine, which stays where it is (`Pc::EnterBesideNative`). A routine that waits runs there as a plain call, and stops the run as `Overran` if it does wait. Handing the call to the native thread instead livelocked `FlightTests`.
- **Cycles native code stands in for.** The game reads an IBM stick by counting turns of a poll loop while the game port's one-shots, timed by the instructions executed, run down. Native code takes no time, so a native read would count forever. `Pc::CountInstructionCycles` adds the cycles the 8088 takes over the instructions a native loop stands in for, so the one-shots drop at the poll they drop at in the original. `ReadJoystickAxes` counts them, and waits as the original does when Y's one-shot outlasts X's.
- **A routine that never waits is a plain call,** with no thread switch.
  - **If one waits after all,** it cannot stop at the end of the run. What would end its wait, such as a key, reaches the machine only between runs.
  - **So the run stops there.** `RunUntil` returns `StopReason::Overran`, and `NativeCode::Overran` names the routine.
  - **Running on instead, as the first version did, never ends the call.** `flight-screens.replay` hung at the first function key pressed in flight, and the shell would have frozen there.
- **Which calls are compared:**
  - **`Always`: never.** Its original could not be undone, and the original of `Start` or `GameLoop` never returns. What it calls through hooks is compared instead, each call on its own. So native code that waits calls the work routines through `Guest::Call`, and work routines call each other directly.
  - **`Sometimes`: on the calls where its original does not wait.** The comparison runs on the native thread, so an original that does wait stops at the end of a run as native code does. That call is then unverifiable, and its outcome stands.
- **The `Sometimes` routines.** The first two were `ProcessFlightKeys` and `HandleFlightFunctionKeys`. They work once a frame, and wait for a key only for the F5–F10 screens, the pause and Ctrl+Esc. The other four are the stick's read and what calls it: `ReadJoystickAxes`, `ReadJoystickSteering`, `ReadSteering` and `UpdatePlayerMotion`.
- **The `Always` routines** are the 36 that wait as a rule. They include `Start`, `GameLoop`, `RunFlight`, `RunTitleAndDocked`, the docked screens and menus, the charts, the tunnels, the text prompts and `TickHyperspaceCountdown` (item 10).
- **How they were found.** A pass walked the closure of every ported entry for loops that poll a port, or memory an interrupt handler writes, and each loop it found was read:
  - two of them wait, in those two routines;
  - `CopyProtection`'s retrace poll is closed by the D5 byte;
  - the rest walk the object slots, or count the joystick's bounded poll.

  `flight-screens.replay` runs those waits. With both marked `Never`, it stops at 13.1 s with `StopReason::Overran`, naming `ProcessFlightKeys`; before the stop existed, it hung.
- **The alternatives weighed:**
  - C++20 coroutines would have made every routine above a wait a coroutine.
  - Platform fibers do not exist where the Linux tools run.
  - A state machine for each routine is the Phase 4 scheduler, built too early.

**9. Each subsystem has its own file.**

- **Lists and registration.** The 19 subsystems of Symbols.tsv each have a source file in `GameLogic` with a list of `NativeEntry`, and `InstallNativeRoutines` walks every list. A port therefore touches only its own subsystem's file.
- **Returns.** An entry says how the original returns: `RET n`, `RETF n`, or `IRET` for an interrupt handler's entry, which is hooked like any other because the CPU takes a due interrupt before it stops at a hook.
- **What native code can do.** It makes interrupts through `Pc::CallInterrupt`, as it makes calls through `Pc::CallNear`. It reaches the code segment, any segment, CGA memory, the stack and the ports through `Guest`.

**10. What the ported routines still do differently.** None of these moves a digest of the corpus or of a test. Each is a limit of the machinery, kept rather than built around, and Phase 4 replaces the machinery.

- **An interrupt that comes due inside native code is taken late.** The CPU takes a due interrupt at the start of a step, and native code makes none. So an interrupt that comes due inside a native routine, or that it unmasks, is taken at the next step: in original code the routine calls, at its next loop turn, or after it returns. The original takes it at the instruction where it came due. The only effect found is a BIOS tick count read one lower in `RestoreTimerInterrupt`'s int 1Ah, in registers `GetKey` restores.
- **A run of bad file names nests native frames.** After a bad name, `PromptCommanderFileName` drops its return address and jumps into the disc menu's key loop. The native port calls the key loop instead, one C++ frame deeper on the native thread for each bad name typed in a row, until a key leaves the menu and unwinds them all. Hundreds in a row could overflow the native thread's stack on Windows, where the original's stack does not grow.
- **An IBM stick whose X axis outlasts its Y.** The original's read returns before X's one-shot drops, and the next read then depends on the cycles of everything run in between, which native code does not count. `InputTests` steers with X at or below Y.
- **`Guest::Call` returns to FFFFh.** Native code calls the original with that return address on the stack, where the original's caller had pushed its own. The words differ only in the dead stack, which no digest reads.
- **`TickHyperspaceCountdown` is hooked as `Always`,** though it waits only on the frame the countdown reaches 0. As `Sometimes`, a compared run would hand that frame's whole jump to the original with no hook in force, and none of the work routines the jump calls would be compared.

## What this forecloses

- **Native code that bypasses the machine.** Native code changes the program's memory and ports only through `Machine::Memory` and the `PortRouter`. Anything else would be invisible to paced time and to the comparison.
- **A port admitted by its digests alone.** The digests are the last line of defence; the comparison and its coverage are the test.
- **Hooks in clocked runs compared with DOSBox-X** (ADR-003). Native code takes no cycles, so such a comparison would mean nothing.
- **Editing `GameLogic/DataOverlay.h` by hand.**
