# ADR-061 — T2, the docked host: one state machine on the key bar's keys, now

**Status:** accepted 2026-10-15, with the change that implements it, task T2 of Phase 6's piece 5 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under the owner's ruling D96. It amends ADR-058's item 4 and its key bar: D92's interim, F2, F3 and F8 opening D01 while EQUIP sent F4, GALAXY F5, LOCAL F6 and STATUS F9, ends.

## Context

D82's frame names the docked keys: `ESC DISC`, `F1 LAUNCH`, `F2 MARKET`, `F3 EQUIP`, `F4 GALAXY`, `F5 LOCAL`, `F6 STATUS`. The original's docked keys are others:

| Key | The original, docked | The key bar |
|---|---|---|
| F2, F3 | Sell Cargo, Buy Cargo | MARKET, EQUIP |
| F4 | Equip Ship | GALAXY |
| F5, F6 | the galactic chart, the short-range chart | LOCAL, STATUS |
| F7 | Data on System | — (into the charts) |
| F8 | Market Prices | — (D01) |
| F9, F10 | Status, Inventory | — (D08) |

D92 kept the original's keys while D02 to D10 are the original's screens: only F2 moved, as D01 landed, and the key bar sent the original's keys. The docked dispatcher was a table of keys to screens, and each screen decided for itself which keys close it: Esc and any F-key but its own.

The owner ruled (D96, 2026-10-15) that the docked host takes the key bar's keys now, before the screens it opens are new, and that a key whose screen has been replaced does nothing docked. F7 and F10 keep their screens until the charts and D08 take them in.

## Decision

1. **The docked host's keys are a function, not a table in the dispatcher** (`GameLogic/DockedKeys.h`).
   - `DockedTargetOf` maps a key to what it does docked:
     - F1 launches;
     - Esc shows the disc menu;
     - F2 to F6 show the market, the equipment, the galactic chart, the short-range chart and the status screen;
     - F7 and F10 show Data on System and the inventory;
     - every other key, F8 and F9 among them, does nothing.
   - `DockedKeyOf` is the inverse, for the screens' own keys and the shell's key bar.
2. **A screen is told where it is shown** (`ScreenPlace`), which says what closes it (`ClosesScreen`).
   - Docked, a screen closes on the keys the docked host acts on; in flight, on Esc and every F-key, as the original's screens did.
   - Each keeps its own key, which leaves it open:
     - the status screen's is F6 docked and F9 in flight;
     - the galactic chart's is F4 docked and F5 in flight;
     - the short-range chart's is F5 docked and F6 in flight;
     - the equipment's is F3;
     - D01's is F2.
   - The disc menu leaves only on a key the docked host acts on.
   - So F8 and F9 do nothing on any docked screen: they neither close it nor show another, and the picture stays as it was.
   - The screens' flight keys are unchanged, so flight is the original's to the key.
3. **The dispatcher is the docked host's state machine** (`DispatchDockedKeys` over `DockedScreen`). From the status screen, or the disc menu back from a disk request, each screen returns the key that closed it, and `DockedTargetOf` picks the next, until F1 leaves. The status screen still resets the keyboard as it opens.
4. **The host says which docked screen is open** (`Host::ShowDockedScreen`, `HideDockedScreen`, `ShownDockedScreen`): set by the docked host as each screen opens and cleared when it ends, at the launch or the disc's leaving. It is plain values (R9) for the shell's key bar and the screens still to land, and it is in no digest.
5. **The shell's key bar sends the key it names** (`DockedKeyOf`): EQUIP F3, GALAXY F4, LOCAL F5, STATUS F6, DISC Esc, LAUNCH F1.

## What moved, and how it was measured

Every press of a docked key whose meaning moved was rewritten to the key that now does the same:

| Old key | New key |
|---|---|
| F3 and F8 (D01) | F2 |
| F4 | F3 |
| F5 | F4 |
| F6 | F5 |
| F9 | F6 |

F7 and F10 stay. To find which presses were docked, the tree before the change was run with a trace:
- every F-key step of every replay and twin was logged with whether the docked dispatcher was running when it was pressed, and when the wait after it ended;
- 161 presses were logged: 123 docked and 38 in flight, every one the same at both moments;
- 59 of the docked presses needed a new key, in 10 replays, 14 twins and two rig tests.

The rewritten tests were then dumped member by member (`OUTPOST_ELITE_DUMP_STATE`) and compared with the dumps of the tree before:
- every picture and every outcome held;
- 81 of 402 digest points moved, in 19 replays and twins, and at every one the only member that differs is `input.keyBuffer`, the scan codes of the keys pressed;
- the digests were re-based from the dumps after (`OUTPOST_ELITE_REBASE_DIGESTS`), and `KnownAnswers.txt` and the replays' digest lines were rewritten.

The suite passes, 155 tests under g++, on two runs in check mode. The count includes `DockedKeysTests`:
- each key's target and its inverse;
- what closes a screen docked and in flight;
- the docked host as the game runs it: each key's screen, F8 and F9 leaving the status screen, the equipment and the disc menu as they were, and F1 launching with no docked screen shown.

`MarketTests` now checks that F2, F8 and F9 keep D01 open and that F6 closes it. `CheckFormat` and `CheckProjectFiles` are clean, and clang-tidy 18 is clean on the changed files.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; the game played with the new keys, which the owner does |
|---|---|

## What this forecloses

A docked screen still to land takes its key from `DockedTargetOf`, and its own key is `DockedKeyOf`'s. A screen shared with flight is shown with its `ScreenPlace` and closes by `ClosesScreen`. When D04 to D07 take in Data on System, and D08 the inventory, F7 and F10 leave `DockedTargetOf` and do nothing docked, as F8 and F9 do now; the replays and twins that press them are rewritten then.
