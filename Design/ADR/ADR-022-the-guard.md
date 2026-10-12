# ADR-022 — The guard: pictures, outcomes and subsystem answers (D33)

**Status:** accepted 2026-10-11, with the change that implements it: picture and outcome lines in every replay and beside every known answer, `Elite::OutcomeValue`, and known answers for the galaxies, the markets and the maths. It is the first piece of D33's route, D29's guard brought forward, with D33's amendment that the pictures and the timing outcomes are reported rather than gated ([Reverse-Engineering-Plan.md §5](../Reverse-Engineering-Plan.md#phase-5--the-native-platform-and-the-natural-layout)). Item 6's re-base is done by [ADR-025](ADR-025-addresses-become-what-they-mean.md) for the converted words. **Amended 2026-10-11 by [ADR-028](ADR-028-the-match-to-elites-exe-retired.md) (D57):** the twins' pictures and outcomes are deleted with the twins, and no test reads the corpus's picture and outcome lines. The subsystems' known answers stay. **Amended 2026-10-12 by [ADR-029](ADR-029-the-guard-restored.md) (D59):** the twins' pictures and outcomes and `CorpusTests` are restored, and the guard is as this ADR records it.

## Context

**What guarded the port until now.** The state digest (ADR-008 item 5): a SHA-256 of the data segment, video memory and the CGA's registers, at the corpus's 134 points and the twins' 356 known answers (ADR-016). It holds the whole state to the original's byte for byte. That is what let Phase 3 and Phase 4 change every routine without changing the game.

**Why it cannot carry Phase 5.** The natural layout moves every byte of the data segment, and D32 lets a `.cdr`, a `.replay` and the digests change format. A digest of the 1987 layout cannot survive that. So D33 has the guard that replaces it recorded first, from today's build, in runs that still meet every 1987 digest: what it records is the original's.

**What the owner ruled about strictness.** D33 first gated the pictures, then amended the same day:
- the pictures are reported, not gated;
- so are the flight values that move with the exact timing of a frame;
- the player's progress and the subsystem answers gate.

The game need not stay pixel-identical. It must still play to the same results.

## Decision

**1. A picture digest beside every state digest.**
- **What one is.** `Elite::PictureDigest`: the SHA-256 of the CGA's 16 KiB of video memory and then its 20 register bytes (mode control, colour select and the 18 CRTC registers). These are the last two parts of the state digest, the parts the player sees.
- **In a replay,** a `picture` line follows each `digest` line and belongs to it:
  ```
  digest title-turned c91007204ddd45f9687a5e3a9f6948b5c10793dc345ae5ab8e0b8e24d4ba08fe
  picture title-turned 7d2d7646618ff6d0842ee559f4e2c6611d265c3508b2f92d28f62fb4dd7f9232
  ```
- **Beside the known answers,** in `GameLogicTests/KnownPictures.txt`: `TWIN ORDINAL PICTURE`, keyed as `KnownAnswers.txt` is. They are a separate file rather than a fourth column, so the recorder never rewrites the 1987 answers, and the state digests can later go without touching it.
- **Reported, never gated.** A picture that differs is logged with its point and both hashes, and changes no result and no exit code. Every run ends with a count: `ReferenceRunner` prints how many pictures of its replay it checked and how many differ, `CorpusTests` logs `corpus pictures: 134 checked, 0 differ; …`, and each twin logs the same for its own. `Build/RunTests.py` prints what each shard's tests logged, and under GitHub Actions turns each difference into a warning.
- **A missing picture fails,** naming the point. The ruling is about a picture that differs, not one nobody recorded.

**2. Outcomes, the named facts a player could notice.**
- **One function knows where each lives.** `Elite::OutcomeValue(state, name)` (`GameLogic/Outcome.h`) maps a name to its value in the `GameState`. `Elite::OUTCOMES` lists the 53 names, each with its kind, in the order the lines carry them. The natural layout will change only that function.
- **In a replay,** an `outcome` line follows the `picture` line, as `name=value` pairs, about 750 characters:
  ```
  outcome title-turned galaxy=0 system=7 witchSpace=0 docked=0 inFlight=0 dead=0 creditsTenths=1000 … supernovaHeat=0
  ```
- **Beside the known answers,** in `GameLogicTests/KnownOutcomes.txt`: `TWIN ORDINAL NAME=VALUE …`.
- **The player's progress gates: 40 names** (`OutcomeKind::Progress`). None of them moves from frame to frame; each changes only when something happens to the commander.
  - where they are: the galaxy, the system, in witch space, docked, in flight, dead;
  - what they hold: credits in tenths, the 17 commodities, refugees, missiles;
  - what is fitted: the 10 equipment flags, the 4 laser counts, the laser mounts and types;
  - their standing: legal status, kills and the title (COMMANDER or ARCHANGEL);
  - the missions: which, its stage, its jumps, whether a jump was made since the briefing, and the mask and invasion flags.
- **Timing is reported: 13 names** (`OutcomeKind::Timing`). They move with the exact timing of flight: energy, the two shields, speed, the ship slots in use, fuel, the cabin and laser temperatures, the altitude and the missions' four frame counters. Fuel is here because it drains frame by frame in the fuel leak and fills while scooping.
- **Left out:** the rating, a fixed lookup of the kills; and the quantities on sale, which the pictures show.
- **A missing outcome fails, whatever its kind,** and so does a name the recorded line lacks.

**3. Known answers per subsystem, on the game's value routines.** Each is a test class in `GameLogicTests`, with its answers in a checked-in text file whose header gives its format and its count. A line that differs fails, naming the file, the line and both texts, and so does a count other than the stated one. They call the game's own routines on a `ReferenceRig`; nothing is re-implemented.

| Class | File | Answers | What they cover |
|---|---|---|---|
| `GalaxyAnswerTests` | `GalaxyAnswers.txt` | 2,048 | every system of the eight galaxies: name, place, economy, government, tech level, population, productivity, radius, and the system the chart's cursor selects there |
| `MarketAnswerTests` | `MarketAnswers.txt` | 515 | 256 price lists (8 economies × 8 governments × tech levels 0, 1, 9 and 15), and the quantities on sale for 259 random states |
| `MathsAnswerTests` | `MathsAnswers.txt` | 3,348 | the byte, word and signed divides, through the trap; `DivideUnsigned` at its 7 return offsets; `NextRandom`; `SinCosOf` at all 2,048 angles and 2 past the mask; the rotations; the arc tangents; `VectorLength`, `VectorWithinBox`, `AngleWithinTolerance` and `ShiftRight24` |

Three routines had no callable piece of their own, and were moved out of their callers unchanged:
- `Elite::DescribeSystem` is `SelectSystemAtCursor` after its search;
- `Elite::StartMarketRandom` and `Elite::RollQuantityOnSale` are the buy screen's roll.

**4. Recording is gated, and happens once, never in CI.**
- **How.** One `GameLogicTests` process with `OUTPOST_ELITE_RECORD_GUARD=1`. It refuses to run when `CI` is set.
- **The gate.** The corpus writes its replays only after all 134 state digests are met. A twin writes its lines only from its destructor, and only if no assertion fired, so only if every state digest it gave met its known answer.
- **Shown to hold.** With `title.replay`'s `title-turned` digest and `TwinTitle`'s answer 2 broken by hand, a recording wrote no replay and no line for `TwinTitle`. The other 35 twins wrote their 352 lines.
- **Reproducible.** A second recording reproduced every guard file byte for byte.
- **The rest of a replay is untouched.** `Elite::RecordCheckpoints` replaces only picture and outcome lines and keeps every other line, line endings included: 268 lines added over the corpus, none removed or changed.
- **The subsystem answers** were written in the same recording runs. Their routines take no part in a run, so no state digest gates them; the runs that wrote them met every digest.
- **The shell's own session replays** now carry pictures and outcomes too. `Outpost/Application.cpp` changes in two lines, from `GameStateDigest` to `Elite::TakeCheckpoint`.

**5. Each check bites.** Each was changed by hand, run, and restored:
- **a picture** (in `title.replay` and for `TwinTitle` 2): the tests pass and log `the picture at title.replay line 9 (digest title-turned) differs`, with `1 differ` in the count; `ReferenceRunner` exits 0 and reports it;
- **a timing outcome** (energy): the tests pass and log `the timing outcome differs: energy is 1023, and was recorded as 1024`; `ReferenceRunner` exits 0;
- **a progress outcome** (credits in a replay, kills in a twin): both fail, naming the point and both values; `ReferenceRunner` exits 1;
- **a subsystem answer** (Lave's radius, one price, one sine): each fails, naming its file and line;
- **a missing picture and a missing outcome line:** each fails, naming the point.

**6. The state digests still run, and still gate.** All 134 and all 356 are met. D25 has ended, so a later piece may move them without a new ruling: where the layout changes they are re-based or dropped (D32), recorded in that piece's ADR. Until a piece moves them they hold.

**7. Measured 2026-10-11.**

| Check | Result |
|---|---|
| `GameLogicTests` | 64 of 64 pass under g++ and clang++ (56 before: 2 `ReplayTests`, 3 `OutcomeTests` and the 3 subsystem classes are new); 4 further runs, 2 with each compiler, all pass |
| `MachineTests` | 133 of 133 pass under g++ and clang++ |
| Warnings | none under g++ `-Wshadow` or clang++ `-Wimplicit-int-conversion -Wshadow` |
| The corpus | all 26 replays meet all 134 state digests under `ReferenceRunner`, built with each compiler; 134 pictures checked, 0 differ; 134 outcome lines checked, 0 differ |
| The twins | 356 state digests met; 356 pictures and 356 outcome lines checked, 0 differ |
| Run time (g++, Release, medians of 11 rounds, CPU time) | the corpus under `ReferenceRunner` 1.978 s → 2.018 s; `CorpusTests` 1.876 s → 1.911 s |
| Run time (g++, Debug) | the whole of `GameLogicTests` 26.37 s → 27.81 s, 0.83 s of it the subsystem classes. `Build/RunTests.py`'s groups stay balanced, about 14.4 s for the corpus against 13.4 s for the rest, and were not changed |
| Checkers | `GenerateOverlay --check`, `CheckFormat`, `CheckProjectFiles` clean; clang-tidy clean on every changed `.cpp` but the shell's, which needs the Windows SDK and is tidied in CI |

**8. What the answers found.**
- **Lave is not the BBC Micro's.** This build gives `0 7 LAVE 20 173 2 3 5 28 13552 6276 7`. Its economy (Rich Agricultural in this build's names) and government (a Dictatorship) are as published. Its tech level, population, productivity and radius are this build's own, so the plan's §6.4 ground truth holds only for those two. `GalaxyAnswers.txt`'s header gives both sets of values.
- **Ten systems cannot be selected on the galactic chart.** Each shares its place, at the chart's halved Y, with an earlier system of its galaxy, which the cursor's search finds first: galaxy 1 system 104, 3/221, 4/128, 4/156, 4/161, 4/247, 6/110, 6/177, 7/247 and 7/251.
- **The tech level changes no price.** `ComputeMarketPrices` caps it at 9 and multiplies it by each commodity's tech-level factor, which is 0 for all 17. The prices come from the economy and the government alone: 64 distinct lists.
- **The quantities on sale come from the random state alone;** the economy plays no part.
- **The divide trap at returns 236Ch and 2395h gives 007Fh,** the memory-operand quirk of plan §7.1, confirmed against the code image.

**9. Not verified here.**
- **The MSVC build, vstest, and the shell.** Whether the native test adapter puts `Logger::WriteMessage`'s lines under `Output/StdOut` in the TRX file is untested; `Build/RunTests.py`'s reader was tested on a TRX file written by hand, and also reads text messages. CI shows it.
- **`Tools/ReferenceScreens.py`** now skips picture and outcome lines. Its step check passes on all 26 replays; it was not run against DOSBox-X.
- **One unexplained failure.** A single Release run of `GameLogicTests`, while the guard was timed, failed and its output was not kept. It did not recur in 8 runs repeated at once, nor in any run since, under either compiler. The likely cause is that the tests' scratch directories have fixed names under the system's temporary directory, which other processes on the same machine were using at the time. Within one run each name is used by one test, and CI's shards run disjoint tests, so CI cannot meet it.

## What this forecloses

- **A guard recorded after the first change.** Pictures and outcomes are recorded only from runs that meet every 1987 state digest. Once a change on purpose lands, nothing can be recorded as the original's again.
- **Re-recording a guard to make a red build green.** A progress outcome or a subsystem answer changes only by a ruling recorded with its cause, as a digest does (ADR-008 item 7, ADR-016 item 4).
- **Reaching the state by address from the guard.** Outcomes are read by name through one function, so a layout change touches that function and nothing else.
