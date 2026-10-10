# ADR-013 — The charts at the IBM PC's speed

**Status:** accepted 2026-10-10, with the change that implements it: pacing points in `Machine::Pc` and `GameLogic/Pacing.h`, the native chart loops paying the same, and the chart replays' digests re-recorded. It records D18, the owner's ruling of 2026-10-10 that the galactic and short-range charts run at the speed they ran on a 4.77 MHz IBM PC ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)). It amends D6 and ADR-008 item 3 for those two loops, and nowhere else.

## Context

**The owner found the charts' cursor too quick to aim with.**

**Why it is quick.** Each chart is one loop: it redraws the chart, waits for a vertical retrace in `PresentChartFrame` (CS:0587, its wait at CS:05D0), moves the cursor by what `ReadSteering` gives, and reads a key. The retrace is the loop's only wait. Unlike flight, which waits for `minimumFrameMs` (D6), the binary sets the charts no pace: on the IBM PC the redraw itself was the pace. Paced time lets instructions take no time (ADR-008 item 1), so a chart frame took one retrace (ADR-008 item 3), 60 a second, and the cursor moved 60 times a second.

**Measured 2026-10-10,** on the interpreter clocked as a 4.77 MHz 8088 (`TimeMode::Clocked`, whose cycle counts ADR-005 checks), from leaving one retrace wait to arriving at the next. The CGA's frame is 79,648 cycles.

| Chart | Work between two waits | In CGA frames | A frame on the IBM PC |
|---|---|---|---|
| Galactic | 521,504 to 522,342 cycles over 24 frames, mean 521,816 | 6.55 | the seventh retrace: 116.6 to 116.9 ms, 8.6 a second |
| Short-range | 400,233 to 402,216 cycles over 32 frames, mean 401,339 | 5.04 | the fifth or the sixth: 84.0 to 99.6 ms, mean 91.8 |

So paced time ran the galactic chart seven times as fast as the IBM PC, and the short-range chart five to six times. Held for one second, Right moved the galactic chart's cursor 102 units on the 8088 and the short-range chart's 148. In paced time both reached the chart's right edge, 235 and 175 units away, before the second was out. (The scratch program that measured this is not in the repository; it counts the loop's arrivals at `PresentChartFrame` and reads `chartCursorX`.)

**What the owner was asked.** Three choices: the IBM PC's speed; every chart frame held to the flight's 50 ms (D6's frame time, one rule for every screen, but the galactic chart still 2.3 times as fast as on the IBM PC); or 60 a second as before. The owner ruled the IBM PC's speed.

## Decision

**1. Paced time charges a fixed cost at a named point** for work the 8088 took time over where the reference sets no pace. There are two points, each a chart loop's call of `PresentChartFrame`, and each cost is the mean measured above (`GameLogic/Pacing.h`):

| Point | Where | Cycles |
|---|---|---|
| `GALACTIC_CHART_PACING` | CS:0DB1 | 521,816 |
| `SHORT_RANGE_CHART_PACING` | CS:0FD7 | 401,339 |

**A cost is a constant, not a model.** It does not vary with what the chart shows. The galactic chart's frames varied by 0.16% on the 8088, far less than a retrace, so every one met the seventh. The short-range chart's sit 4% of a frame past the fifth retrace, so on the IBM PC the retrace's 16 lines decided between the fifth and the sixth; the constant's frames, each starting where the last wait ended, meet the fifth or the sixth as the IBM PC's did (item 8).

**2. In an interpreted run, the cost is paid at the address** (`Pc::SetPacingCost`, installed by `Elite::LoadReference` through `InstallPacing`).
- **When paced time reaches the address, the cost is paid before the instruction there runs.** The clock moves as in a wait, a device event at a time.
- **Interrupts that fall due are taken there.** The handler's IRET returns to the same instruction, which goes on paying until the cost is paid. Then the instruction runs, and the next arrival pays again.
- **Clocked time charges nothing,** because there the instructions pay their own cycles. ADR-003's comparisons with DOSBox-X are unchanged.

**3. Native code pays the same at the same point** (`Guest::Spend`, through `Pc::Spend`). `ShowGalacticChart` and `ShowShortRangeChart` call it just before their call of `PresentChartFrame`, where the original's loop reaches CS:0DB1 and CS:0FD7. `Spend` waits as the interpreted run does: the clock moves a device event at a time, interrupts are taken as they fall due, and the run stops there if that is its end. So an interpreted and a native run take the same interrupts at the same point, and the corpus's three runs (interpreted, native, and compared call by call) agree.

**4. The costs outlive the interpreter (D7).** They are two numbers in `Pacing.h`, not a cycle model. When D7 deletes the interpreter, `SetPacingCost` goes with it, and the native loops' `Spend` is what remains. What D6 declined, a cost model kept alive after Phase 4, is not what this is.

**5. Digests moved, by this ruling** (ADR-008 item 7).
- **`docked-screens.replay`:** the seven digests from `galactic-chart` on. Its galactic chart now runs two or three frames, not eighteen, in the 0.3 s its cursor key is held, and the cursor's position is kept in the data segment.
- **`hyperspace-and-fight.replay`:** all four. Fewer chart frames before the launch leave the random state different, so the pirates come differently.
- **`flight-screens.replay`:** none. It shows both charts without moving the cursor, and a chart frame changes nothing else the digest sees.

**6. Two replays' inputs changed with them,** and each is checked by state, as ADR-008 item 6 asks.
- **The short-range cursor.** Both replays held Left for 0.1 s to put it on Riedquat. That is now one frame, and the cursor stays on Lave. They hold it 0.55 s: six frames, which select Riedquat (index 46), and the same 0.6 s in all.
- **The fight** in `hyperspace-and-fight.replay` was played again by the same scratch steering program from the new start. It again ends with `killCount` at 1.

**7. The screenshot test holds its keys longer.** A chart reads its keys once a frame, so `SaveLoadTests` holds Alt and PrtSc for 0.15 s, longer than a frame, and waits 0.6 s for the buffered keys to go before Escape.

**8. Measured 2026-10-10, after,** by the same program in paced time. The galactic chart's frames take 116.8 ms. The short-range chart's take 84.1 to 99.5 ms, mean 91.8, against the 8088's 84.0 to 99.6, mean 91.8. A second's hold of Right moves the cursors 102 and 148 units, as on the 8088.

## What this forecloses

- A pacing point without a ruling. D6 holds everywhere else: a flight frame takes `minimumFrameMs`, and there is no model of the 1987 slowdowns. A new point is a ruling, recorded here with its measurement.
- A cost that varies with the chart's contents.
- Charging a cost in clocked time.
