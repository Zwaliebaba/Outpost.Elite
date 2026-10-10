# ADR-008 — Time, pacing and the replay digests

**Status:** accepted 2026-10-10, with the change that implements it: paced time in `Machine::Pc`, the `GameLogic` library with the replay format and the game-state digest, and the seed corpus in `Replays/`. It records D6, the owner's speed ruling, and D16, the owner's ruling of 2026-10-10 that replays are framed by the game's own pace and that this ADR is written now rather than in Phase 4 ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)). Amended 2026-10-10 by [ADR-013](ADR-013-the-charts-at-the-ibm-pcs-speed.md) (D18): the galactic and short-range charts take the time they took on the IBM PC. Amended again the same day: the `file` step, prepared commanders and the corpus Phase 4 completes (items 4, 6 and 8). Amended a third time the same day with the owner's ruling on the corpus's gaps (D20, item 8), and a fourth with the `end` step and the replays that close them (items 4 and 8).

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
- **A chart frame takes one vertical retrace.** ADR-013 (D18) amends this: a chart frame first pays what its redraw cost the IBM PC, so it takes the retraces it took there.
- **The docked screens' cursor delays take their 100 timer ticks.**
- **There are no 1987 slowdowns,** because the work between waits takes no time, as D6 ruled. The charts are the one exception (ADR-013). The port's scheduler in Phase 4 runs the same way, so Phase 4 no longer moves the digests for this reason.

**4. A replay** is a script of steps (`GameLogic/Replay.h`): `wait`, `key`, `down`, `up`, `shot`, `digest`, `file` and `end`.

- **It always runs in paced time.** It starts from these fixed conditions:
  - the reference loaded with the D5 byte (`Elite::LoadReference`);
  - the PSP at 0813h;
  - DOS's clock at 00:00 on 1 January 1980;
  - DOS's files in an empty directory.
- **Time in a replay is whole milliseconds from its start, converted to cycles once** (`Elite::ReplayCycle`). A recorder that samples time at any rate and a player that sums the recorded waits therefore land on the same cycle (`ReplayTests.WaitsAddUpExactly`).
- **Inputs are keys,** made and broken at moments of paced time. `key` makes and breaks at once, which is what a menu reads through the key buffer. `down` and `up` hold a key across frames, which is what flight reads through the key-down table.
- **What it reproduces.** A replay reproduces the run it records bit for bit, on every build. Two runs, and builds by g++ 13 and clang++ 18, give the same digests (measured 2026-10-10).
- **The same steps drive DOSBox-X.** `Tools/ReferenceScreens.py` reads them, in wall time, as an approximation of the run.
- **`file NAME SOURCE` puts a commander where the game can load it.** At that moment a copy of SOURCE goes into DOS's directory as NAME, through the file store, as the game's own save leaves a file: created, written, its attributes cleared, and stamped 00:00 on 1 January 1980. It takes no time and replaces a file already there.
  - SOURCE is a bare file name beside the replay, in `Replays/` for the corpus, so a replay reaches nothing else. NAME is a DOS 8.3 name.
  - A step that cannot be done stops the replay with an error naming its line.
  - The recorder writes no `file` steps, so a session that loaded a commander from the player's folder does not replay from its file alone.
  - `ReferenceScreens.py` copies the file into DOSBox-X's mounted drive, with the sidecar that records a clear archive bit; without it the game's loader refuses the file.
  - A run starts with DOS's directory empty, and `ReferenceRunner` refuses a run whose directory is not.

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

**8. The corpus that the interpreter's deletion waits on** (D7, plan §6.2). Thirteen replays were added on 2026-10-10, so that the corpus reaches the subsystems the plan lists before the interpreter goes.

**Prepared commanders.** Most of the list needs credits, equipment or a place the starting commander lacks, and the game takes a commander only from a file. So a replay loads one through the disc menu after a `file` step.
- **The `.cdr` format** is exactly 208 bytes: the data segment's `commanderBlock`, DS:756B–763A, byte for byte. `SaveCommanderFile` (CS:0358) writes `commanderFileBytes` bytes from it and `LoadCommanderFile` (CS:031A) reads them back over it. The block holds, in the symbol table's names: the "Commander file" header and ^Z, the status frame and rank, the cash as text; `randomState0-2`, the galaxy and the chart cursors; the current and the selected system's 25-byte records; fuel, equipment, the laser mounts and their types, the 32-bit cash, legal status and kills; the name, the hold and the mission state. There is no checksum, version or length check. The loader refuses only a file with a DOS attribute set, and a loaded commander takes the file's name, so every replay copies its commander in as `JAMESON.CDR`.
- **Why a prepared commander is faithful.** Each starts from `Replays/jameson.cdr`, which the reference saved itself: the starting commander after selecting Leesti, by the steps `Tools/PrepareCommanders.py` documents, its SHA-256 pinned. The script then changes only fields the symbol table names, and only to values the game's own code produces: equipment as the equipment screen fits and removes it, cash with its text as `FormatCredits` writes it, kills, an arrival as `CompleteHyperspaceJump` and `UpdateMissionSchedule` leave the bytes (checked against a real jump: identical but for the random state), the second mission as the briefing sets it up, and the random state stepped by `NextRandom`'s own recurrence. So each file is a state play reaches, loaded as the game loads its own saves. `PrepareCommanders.py --check` verifies the committed files.

**The added replays**, every digest recorded from the interpreted original:

| Replay | What it reaches |
|---|---|
| `trading` | buying and selling cargo at Lave, alien items refused, what it cannot afford refused, contraband sold and the commander made an offender |
| `buy-every-equipment` | all 14 items bought at Leesti; a missile and the pulse laser sold; each laser fitted to each mount |
| `disc-menu` | catalogue, delete, a bad name, a missing file and the retry declined, mouse, both joysticks, version, keyboard, and a load |
| `docking-computer` | D pressed 6.0 s after the launch; the computer docks |
| `galactic-hyperspace` | G then H; galaxy 2 and its chart |
| `escape-pod` | C on the first frame in space; the capsule reaches the station |
| `death` | the station rammed: GAME OVER, the title and a new game |
| `witch-space` | the misjump the reference forces with Alt+W; Thargoids, Thargons, a missile the ECM removes; killed in the fight |
| `attack-the-station` | a missile locked and launched, the station's ECM, laser fire that angers it, the police it launches, the energy bomb, the side views |
| `mask-mission` | the second mission's Mask ship and escorts, which ram the first launch; a second commander copied in mid-replay, whose fight meets the Constrictor and the Cougar |
| `combat-orerve` | a fight at Orerve: the Transporter, the Boulder, a Moray and a Krait |
| `combat-reorte` | a fight at Reorte: the Boa, the Cobra Mk I, the Gecko and the Fer-de-Lance, with a Shuttle and a Barrel |
| `combat-orerve-drifters` | a fight at Orerve: the Plate, an Escape Pod and a Sidewinder, with the Anaconda, the Worm and a Cobra Mk III |

With the first six, the corpus meets every ship type the game spawns. Under D18 no single fight met all six types `combat-orerve` was first recorded for: a search of 4,000 random states at each of Orerve and Reorte found at most five, because the Anaconda and the Boulder are rare and about half the runs end in the player's death. So the three combat replays share the types out. They also meet the Moray, the Barrel and the Cobra Mk III, which `hyperspace-and-fight` stopped meeting under D18 and no other replay meets. The fights were played by a scratch steering program, as `hyperspace-and-fight`'s was, and recorded as key steps; the combat commanders' random states were found by searching draw counts for the runs that meet the most types not met elsewhere.

**What the corpus reaches, measured 2026-10-10.** Interpreted, the replays execute 11,364 of the original's instruction starts, against 8,849 before. The corpus is 19 replays, 507.4 s of game time and 98 digests, summed from `ReferenceRunner`'s runs. `CorpusTests` takes about 18 s interpreted, 2 s native and 18 s compared (g++ Release, a shared machine), against about 12 s in all for the first six replays.

**What it does not reach, and why.**
- Missions 1 and 3 and their briefings; mission 2 only as prepared state.
- Scooping, the mining laser splitting a rock, `ShowShipIdentity`, the masking device and the anti-ECM: they need precise flying or a mission's reward.
- A Shuttle the station launches; an invasion's Thargoids.
- Flight steered by mouse or joystick: `InputTests` constructs them.
- Screenshots, which `SaveLoadTests` constructs, and leaving for DOS, which ends a run as `Terminated`, which the corpus does not accept.
- `InitEscapePod` and `InitKraitHunter`, which nothing calls.

The routines these leave unreached are compared by constructed tests (ADR-010 item 5), but the corpus is the only guard after D7.

**How the gaps close before D7: the owner's ruling of 2026-10-10 (D20).**
- **By replay**, what play reaches:
  - missions 1 and 3 with their briefings, the masking device and the anti-ECM, from prepared commanders;
  - scooping and the mining laser splitting a rock, flown by the steering program;
  - leaving for DOS, with a replay now allowed to end in `Terminated`.
- **By known answers**, recorded from the interpreter before it goes: flight by mouse or joystick, screenshots, a Shuttle the station launches and an invasion's Thargoids.
- **`InitEscapePod` and `InitKraitHunter`** are deleted at D7 (D22): nothing calls them, so nothing guards them.

**The D20 replays, measured 2026-10-10.** Seven replays close the gaps play reaches, each from a prepared commander, with every digest recorded from the interpreted original. Each agrees with the native code on every call.

| Replay | Game time | What it reaches |
|---|---|---|
| `supernova-mission` | 58.9 s | mission 1 from its trigger to the debriefing's 1,000 credits: the fuel leak, the briefing, the refugees, a galactic jump, and a docking by hand |
| `invasion-mission` | 52.7 s | mission 3 from its trigger to the debriefing: the briefing, the station destroyed by an aft military laser, which ends the invasion, the title Archangel and the anti-ECM |
| `masking-device-and-anti-ecm` | 12.2 s | the anti-ECM letting a missile reach the station, and the masking device keeping a police Viper from firing |
| `mining-laser` | 14.3 s | `ShowShipIdentity` on an asteroid, the mining laser splitting it, and its splinters scooped |
| `scooping-a-canister` | 14.9 s | a cargo canister scooped |
| `scooping-an-escape-pod` | 25.1 s | an escape pod scooped as slaves, and a boulder the scoops refuse |
| `leave-for-dos` | 5.6 s | quitting to DOS, with a digest after the program has ended |

- **The `end` step.** `end S` runs the machine as `wait S` does. It is the only step in which the program may end, and the program must have ended by its end. Only `digest` and `shot` may follow it. A wait in which the program ends fails, and so does an `end` after which it still runs. The step declares the quit, so the test can tell a replay that quits from one that stopped by mistake. A digest after the end reads memory and the CGA as the end of the program leaves them.
- **The commanders,** under the rule above:
  - `supernova-mission.cdr` and `invasion-mission.cdr` sit one jump before missions 1 and 3. `UpdateMissionSchedule` gives each at its count of jumps (CS:4980–4992).
  - `archangel.cdr` has mission 2's two rewards and the title, as the debriefing and `AwardArchangelTitle` write them.
  - The three scooping commanders have fuel scoops, and a mining laser in place of the fore pulse laser.
  - Each random state was found by searching draw counts for a run that meets its target near the launch.
- **What the corpus is now.** 26 replays, 690.9 s of game time and 134 digests. Interpreted, they execute 11,861 of the original's instruction starts, against 11,421 for the 19 before. Nine routines the corpus never called now run, among them `ShowShipIdentity`, `TryScoopObject`, `UseMaskingDevice`, `ShowMissionBriefing` and `ShowMissionDebriefing`. `CorpusTests` takes about 20 s interpreted, 3 s native and 21 s compared (g++ Release, a shared machine).
- **Left to known answers, with the list above.** These paths are within play, but none came within a short run. They take the D20 ruling's second branch:
  - the invasion's Thargoids, which spawn only after a jump that follows the briefing, in that system's safe zone;
  - the paths of `TryScoopObject` the replays miss: the masking device's barrel, a full hold, splinters that carry minerals, and Thargons;
  - `ShowShipIdentity`'s labels other than Debris;
  - mission 1 declined, and its 1,400-credit reward.
- **The known answers, 2026-10-10.** Every twin's digests are recorded from the original, and eight new twins reach the paths left to known answers (ADR-016). One label, `ShowShipIdentity`'s "None", is unreachable in the original: its only ship, the Cobra an escape capsule leaves behind, can be identified only while the capsule flies, and then no key is read.
- **Found in the original on the way.**
  - An escape capsule's arrival does not set `playerDocked`. So the status screen briefs after one but never debriefs, and both mission replays dock for real before their debriefings.
  - The fuel leak's counters are in the commander block.
  - A launch sets every station up alike, so `launch-and-dock`'s keys dock at any of them.

**Found in the original on the way.** `PlaceAtSpawnPoint` turns the spawn point by `rotationSinCos` pairs 6 and 7, which nothing sets before the first `ComputeVelocity`; until then a ship spawns on the player. So the Mask's escorts ram the commander on the first launch after boot, and the first Thargoid in witch space dies ramming the ship. Escorts copy their leader's type (`PlaceEscortNear`), which is why the Constrictor and the Cougar appear only as replacement ships.

## What this forecloses

- Clocked time in replays, in the corpus or in the game. It stays for comparisons with DOSBox-X.
- A model of the 1987 slowdowns, as D6 ruled. ADR-013's two pacing points are constants, not a model, and need a ruling each.
- Digests over registers, the code segment or the stack.
- Re-recording a digest to get CI green. A digest moves only by a ruling recorded here.
