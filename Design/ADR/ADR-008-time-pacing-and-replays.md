# ADR-008 — Time, pacing and the replay digests

**Status:** accepted 2026-10-10, with the change that implements it: paced time in `Machine::Pc`, the `GameLogic` library with the replay format and the game-state digest, and the seed corpus in `Replays/`. It records D6, the owner's speed ruling, and D16, the owner's ruling of 2026-10-10 that replays are framed by the game's own pace and that this ADR is written now rather than in Phase 4 ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)).

## Context

**D6 ruled what "the same speed" means:** the game's own frame time, 50 ms by default and selectable with F1–F10, with no model of the 1987 slowdowns (Reference-Map.md, "How the game paces itself").

**The plan expected replay digests to move in Phase 4** (plan §5), because native code would change when the timer and keyboard interrupts land. In clocked time every instruction takes its 8088 cycles, and an interrupt lands between whichever two instructions the clock says. The same is true of Phase 3, though: a native routine takes no cycles, so the first routine replaced would move every later interrupt to a different instruction.

**The digests would then drift from the first routine on.** A digest that drifts every time a routine is replaced gates nothing. Asked, the owner ruled that replays are framed by the game's pace, with digests at those points, and that this ADR is written now.

**What decides the clock model.** Phase 1 settled what the interrupts share with the main program (Reference-Map.md, "What the interrupt handlers share"):

- the timer handler's counters;
- the keyboard's tables and buffer;
- the sound engine.

**How the game waits.** It always waits by polling, in one of three ways:

- `PresentSpaceView`'s spin until `msSinceFrame` reaches `minimumFrameMs`;
- three loops on the CGA's vertical retrace;
- loops on `GetKey`, and on `WaitForTimerTick`.

So if interrupts land only while the program waits, they land at the same points however long its work takes, in an interpreted routine or a native one.

## Decision

**1. Paced time.** `Machine::Pc` has two time modes (`TimeMode`).

- **Clocked**, the default, is the real machine's: every instruction takes its 8088 cycles. ADR-003's comparisons with DOSBox-X run in it.
- **Paced** is what replays, the corpus, Phase 3 and the game run in.

In paced time:

- **Instructions take no time.**
- **The clock moves only while the program waits.** It then moves straight to the next thing a device will do, or to the end of the run if that comes first. A device's next action is any of these:
  - the PIT's next IRQ0;
  - the keyboard's next delivery;
  - the next point where the CGA's status can change.
- **Waiting is recognised, not declared.** A loop that waits changes nothing on its turns: when it takes its backward jump, the registers are what they were the last time that jump was taken, no byte of memory has changed value, and no port has been written. Paced time watches every taken backward jump, and a turn like that moves the clock. HLT moves it too.
  - **What counts as work.** A loop that counts, copies or draws changes something each turn, so it is work and takes no time.
  - **Why it is not a list.** A hand-kept list of the game's wait loops would have to be complete, and it would have to be kept complete.
- **Native waits.** Native code that replaces a waiting loop calls `Pc::Idle` in its place (Phase 3).
- **Spinning stops the run.** If the program runs 50 million steps without waiting once, the run stops with `StopReason::Spinning` and the address. A wait the rule misses therefore stops the run rather than freezing the clock silently.
- **The game port keeps instruction timing.** Its one-shots are measured against the cycles of the instructions executed, in either mode, because `ReadJoystickAxes` reads a stick by counting polling loops with interrupts off.

**2. In paced time the CGA reports each vertical retrace to only the first status read that sees it.** The game waits for a retrace by reading 3DAh until bit 3 is set, which is a level and not an edge.

- **On the real machine** the drawing between two such waits always outlasts the retrace's 16 lines, so every wait meets the next retrace.
- **In paced time** the drawing takes no time. The retrace would still be on at the next wait, which would then not wait at all.
- **Measured 2026-10-10:** without this rule the galactic chart redrew itself 50 million steps in a row with the clock standing still.

Only three loops read 3DAh (0x04C1, 0x05D0, 0x4602), all of this form; the first is the protection's, which D5 makes unreachable.

**3. This is D6, made exact.**

- **A flight frame takes exactly `minimumFrameMs`.** The spin in `PresentSpaceView` waits that long, and nothing else takes time.
- **A chart frame takes one vertical retrace.**
- **The docked screens' cursor delays take their 100 timer ticks.**
- **There are no 1987 slowdowns,** because the work between waits takes no time, as D6 ruled. The port's scheduler in Phase 4 runs the same way, so Phase 4 no longer moves the digests for this reason.

**4. A replay** is a script of steps (`GameLogic/Replay.h`): `wait`, `key`, `down`, `up`, `shot` and `digest`.

- **It always runs in paced time.** It starts from these fixed conditions:
  - the reference loaded with the D5 byte (`Elite::LoadReference`);
  - the PSP at 0813h;
  - DOS's clock at 00:00 on 1 January 1980;
  - DOS's files in an empty directory.
- **Time in a replay is whole milliseconds from its start, converted to cycles once** (`Elite::ReplayCycle`). A recorder that samples time at any rate and a player that sums the recorded waits therefore land on the same cycle (`ReplayTests.WaitsAddUpExactly`).
- **Inputs are keys,** made and broken at moments of paced time. `key` makes and breaks at once, which is what a menu reads through the key buffer. `down` and `up` hold a key across frames, which is what flight reads through the key-down table.
- **What it reproduces.** A replay reproduces the run it records bit for bit, on every build. Two runs, and builds by g++ 13 and clang++ 18, give the same digests (measured 2026-10-10).
- **The same steps drive DOSBox-X.** `Tools/ReferenceScreens.py` reads them, in wall time, as an approximation of the run.

**5. The game-state digest** (`Elite::GameStateDigest`) is a SHA-256 over three things:

- the data segment, except the stack (DS:AD60–B15F);
- the CGA's 16 KiB of video memory;
- the CGA's mode, colour and CRTC registers.

It leaves four things out:

- **The registers**, because a native routine has no instruction pointer to agree on.
- **The code segment**, because the triangle filler rewrites its own instructions (Reference-Map.md, "The solid renderer") and a native filler will not.
- **The stack**, which holds whatever the last calls and interrupts left below SP.
- **DOS's and the BIOS's own memory**, which the game does not read.

**It includes more than the owner's ruling required.** The ruling allowed leaving out the timer handler's counters and the sound channels. Under paced time those are as reproducible as everything else, so the digest keeps them, which makes the gate stricter. If a later change cannot hold them, leaving them out is the fallback already ruled. It is to be recorded here when used, and is not a way to make a red build green.

**6. The corpus is `Replays/*.replay`,** played by `GameLogicTests.CorpusTests` on every CI run. Each replay names the digests it must reproduce. The seed corpus has five replays, 175.5 s of game time and 31 digests:

| Replay | What it does |
|---|---|
| `title.replay` | the boot, the title's turning solid ship, and the ship types stepped with F9 and F10 |
| `docked-screens.replay` | every docked screen at Lave; buys a missile; moves both charts' cursors |
| `launch-and-dock.replay` | launches, turns back along the slot's axis, matches the station's spin, and docks by hand |
| `hyperspace-and-fight.replay` | selects Riedquat, jumps there, and fights the pirates that come; one is destroyed |
| `save-and-load.replay` | saves the commander through DOS, spends money, loads the save and gets it back |

**Added since.** `flight-screens.replay` (ADR-010 item 8, 29.4 s and 9 digests) launches and, in flight, shows every screen the function keys show, pauses and resumes, and freezes with Ctrl+Esc and thaws: the waits inside a routine that otherwise works once a frame.

Some of these were found by probing, but every one is checked by state, not by eye:

- **Docking** sets `playerDocked`.
- **The fight** raises `killCount` to 1.
- **The save and load** move the commander's credits from 100.0 to 66.4 and back.

The fight was played by a scratch program that steered by the targets' positions as it went. What it pressed is recorded like any other input.

**Speed, measured 2026-10-10:**

- The corpus plays in 2.9 s with a g++ -O2 build.
- `GameLogicTests`, which includes it, runs in 14 s with a g++ -O0 build.
- The corpus executes 8,717 of the 13,831 instructions the static map reaches.

**7. Digests change only by a ruling recorded here, with its cause,** in Phase 3 and Phase 4 alike. Re-recording a replay's digests is `ReferenceRunner --replay FILE --update`. That is how a new replay is finished, and never how a failing one is made to pass.

## What this forecloses

- Clocked time in replays, in the corpus or in the game. It stays for comparisons with DOSBox-X.
- A model of the 1987 slowdowns, as D6 ruled.
- Digests over registers, the code segment or the stack.
- Re-recording a digest to get CI green. A digest moves only by a ruling recorded here.
