# ADR-027 — The short-range chart capped to its buffers, a change on purpose (D41)

**Status:** accepted 2026-10-11, with the change that implements it: `AddShortRangeSystem`, `PlaceChartLabels` and `DrawChartItems` in `Galaxy.cpp`, and the test `CrowdedShortRangeChartsStayInTheirBuffers`. It records the owner's ruling D41 ([Reverse-Engineering-Plan.md §8](../Reverse-Engineering-Plan.md#8-decisions-for-the-owner)). It is the game's first change on purpose (D26).

## Context

The short-range chart keeps its items in `chartItems`, 32 of them, and its labels' names in `pendingChartLabels`. Each system on the chart takes an item for its disc and one for its label. The original does not check either buffer:
- **More than 16 systems.** The 33rd item is written over `chartLabelCursor`, an address in 1987. The labels are then read from wherever that word points, through the data segment.
- **Crowded labels.** On the most crowded charts, the label names run past `pendingChartLabels` into the members after it.
- **No nudges left.** A label that runs out of nudges is drawn below the chart, past the drawing buffer, into DS:2000 and on.

These paths were measured by building and drawing the chart of every system of the nine galaxies as the current one (2,304 charts). They were measured on `77a5a54`, where `GameData` still had the 1987 layout, and played on past the chart where the damage lasts.
- **135 charts are affected:** 92 have more than 16 systems, and 43 have a label below the chart. In the first galaxy these include Riinus, Aanbiat, Veis and Onusorle. No replay or known answer reaches any of them.
- **What the player sees.** The crowded charts show garbage labels, fragments of the game's texts, and bands of garbage. The others lose one label near the bottom.
- **What lasts.** Damage that outlives the chart, each confirmed by playing on:
  - On 2 charts, `speakerPortImage` holds the keyboard clear, so keys are lost.
  - On one chart, `sineTable` is overwritten, and with the same keys the player dies about 9 s after launch.
  - The laser sights are garbled on 4 charts, and the farewell message on 21.
  - On one chart, the find error and the commander file pattern are overwritten, so the catalogue then always fails.
  - A screenshot loses its file name.
- **Change 2 had already moved this, unnoticed.** Change 2 (ADR-025) made `chartLabelCursor` an index. So from it on, after the overflow the port read its labels at `pendingChartLabels` plus the overwritten word, where 1987 reads at the word itself. All 92 crowded charts drew different garbage from `cf8567b`, the commit before it.
- **Change 3a then made them stop.** It distributed the data segment (ADR-026), so all 135 charts stopped the game with `std::out_of_range`.
- **Reproducing 1987 cannot outlive change 3c.** It was prototyped in about 100 lines that read and write the overflow by 1987 address. Those lines need the `GameState`'s answer by address, which change 3c removes. Without about nine constants put back into the state, they still could not reproduce the death, the sights or the texts.

## Decision

The chart stays within its buffers. The original ran past them, and the port does not:
- **`AddShortRangeSystem`** keeps no label whose name would run past `pendingChartLabels`.
- **`PlaceChartLabels`** stops at the 32nd item. A chart of more than 16 systems labels the first 32 items' worth, which is fewer than all its systems.
- **`DrawChartItems`** does not draw a label whose last row is below the chart.

## Measured 2026-10-11

| Check | Result |
|---|---|
| The chart probe, all 2,304 charts | 0 stop. The 2,169 others are identical to `77a5a54` after building and after drawing. The 135 differ only in the chart's own members, the drawing buffer, and the members the overflow wrote. |
| Pictures | The 5 rendered charts with a label below are byte-identical to `77a5a54`'s. On the crowded charts the garbage is gone, and the first label is right. |
| Playing on | 120 steps over the affected charts match `77a5a54` with this change, and none of the lasting damage occurs. |
| `CrowdedShortRangeChartsStayInTheirBuffers` | Passes. Without the change it fails, as main does. |
| The corpus | 26 replays and 134 state digests, 356 known answers, pictures and outcomes all hold. No replay reaches these charts. |

## What this forecloses

- **The 1987 overflow.** The game no longer draws the garbage labels or carries the damage they did.
- **A chart that names every system in range.** A chart of more than 16 systems still names fewer than all, in the order the systems come: ZACEAR names 9 of its 23. Naming all of them needs larger buffers, a change of the state's layout, and the owner chose not to make it here. The redrawn short-range chart (`Design/Canvas/docked/04-short-range-chart.png`) will lay out its own labels.
