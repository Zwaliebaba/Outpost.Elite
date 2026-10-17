# ADR-067 — D10, Disc access: the catalogue to pick from, a name typed on the screen, the disk work still Start's

**Status:** accepted 2026-10-17, with the change that implements it: tasks T6 (the disc's file operations and the input device's switch) and T16 (D10) of Phase 6's piece 5 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)). It follows the owner's rulings:
- D82, the screens as drawn;
- D83, the mouse while docked;
- D96, Esc;
- D102, D10 adapted to `FileStore`;
- D117 to D120, the load, the save, the opening and Esc, and the tests.

## Context

The original's Disc/Control menu is a text page that Esc opens docked:
- L, S and D ask for a file name (`PromptCommanderFileName`, through `ReadTextLine`). C catalogues the disc. E asks whether to exit to DOS. K and M choose the keyboard or the mouse. V shows the version.
- A load, a save, a delete or the catalogue leaves the game loop for Start (`LeaveGameLoopForDisk`).
  - Start does the disk work through `FileStore` (D78), then runs the game loop again.
  - The menu then shows `LOADED:`, `SAVED:`, `DELETED:` or `CATALOGUED`, or `ERROR: Retry (Y/N)?`.
- A save over a name already on the disc overwrites it, unasked.

D10, `Design/Canvas/docked/10-disc-menu.png`, replaces it with a modal over the docked frame. Its six items sit on the left, and beside them the catalogue: each file with the day it was saved and the cash in it, the count, `ESC · BACK` and `ENTER · LOAD SELECTED`. D102 adapts it to `FileStore`: `5 Quit game`, `COMMANDER FILES` with no drive, and the count with no free space.

## Decision

1. **D10's own logic is values (T6)** (`GameLogic/DiscScreen.h`, `DiscView.h`).
   - **The keys.** `DiscKey` reads them:
     - 1 to 6 choose, the arrows move, Enter acts and Esc goes back.
     - In the name: its letters and digits, upper case, Backspace, Enter and Esc.
     - At a question: Y, N and Esc. Enter does not answer a question, so that two Enters cannot overwrite or delete.
   - **The clicks.** `DiscClickInput` reads the shell's: an item, a row, the lit button, which is Y at a question, and `ESC · BACK`, which is N.
   - **The steps.** `StepDisc` takes the screen, an input and what D10 knows (`DiscFacts`). It returns the screen, and what the routine is to do:
     - load, save, delete, retry or quit through Start;
     - read the disc again;
     - set the device;
     - go back to the status screen.
   - **The catalogue.** `CommanderFilesOf` lists each `.CDR` file with no attribute, by name, at most 40 as the original's did. Each file has the day its stamp holds and the cash of the commander in it, read through the `.cdr` serializer without changing the game. A file too short to hold a commander has no cash.
2. **The owner's rulings.**
   - **The load (D117).** Load commander picks from the catalogue: the arrows or a click select, and Enter or the button loads.
   - **The save (D118).** Save commander's name starts as the commander's and takes eight letters or digits. A name that starts with a digit is refused, as the original refused it. A name already on the disc asks Y/N before it is overwritten.
   - **The opening and Esc (D119).** D10 opens on Load commander with the catalogue. Esc in the name or at a question cancels it. Otherwise Esc goes back to the status screen, as F6 would.
3. **The routine is `ShowDiscScreen`**, which Esc opens docked.
   - Each tick it takes the clicks the shell sent (`Host::Click(DiscClick)`) and one key, steps, and publishes the view (`Host::ShowDisc`).
   - **The disk work stays Start's.** A load, a save and a delete write `commanderFileName` as the original's prompt did, and leave through `LeaveGameLoopForDisk`. Start does the work and runs the game loop again, and a loaded commander starts it as the original's did.
   - **Back from the disk work,** D10 opens on what it came to: loaded, saved or deleted, the file selected, or the question whether to retry.
     - A loaded name becomes the commander's, as the original's eight bytes of it.
     - A save comes back on Load commander, the saved file selected.
   - **Quit game** is the original's exit to DOS, a quit (D80).
   - **The catalogue is read by D10 itself**, on opening and on 4: reading changes nothing, so it no longer leaves the game loop as the original's C did.
   - **Input device** offers the keyboard and the mouse. The mouse is refused, with a strip, when none is attached.
   - Any key that closes D10 clears `resumeAtDiskMenu`, as the original's F-keys did.
4. **The original's menu goes**, with its text page, its texts (`SaveLoadTables.h`), `PrintCommanderCatalogue` and `ListCommanderFiles`. `ReadTextLine` goes too, as D10's name was its last reader, with its cursor and its redraw. `SaveLoadState` keeps the catalogue's members and `TextState` the typed line's, and nothing writes them. `PerformDiskRequest` keeps the load, the save and the delete.
5. **The shell draws it (T16)** (`Outpost/DockedDisc`), with ESC DISC lit.
   - **The menu:** each item with its number's box, the chosen one lit, and the footer `1–6 or click · Esc returns to the game` as drawn.
   - **The panel:**
     - the catalogue, `COMMANDER FILES`, `SAVED` and `CREDITS`, with each file's day as `14-10-87` and its cash with its thousands apart, and the count;
     - or the name, with its caret and `.CDR`;
     - or the two devices, the one that steers marked `IN USE`;
     - or the quit's line.
   - **The strip** says what a step came to, or asks its question.
   - **The two buttons:**
     - the lit one says what Enter does, and is off on an empty catalogue;
     - at a question they are `N · NO` and `Y · OVERWRITE`, `Y · DELETE`, `Y · QUIT` or `Y · RETRY`.
   - **The catalogue scrolls** to keep the selected file in sight, eight rows at most.
   - **The bar.** A click on the lit ESC DISC does nothing, where Esc would close D10.
   - **The words the drawing leaves open** are this ADR's: the strips' and the questions', the name's hint, and `IN USE`.
6. **A modal dims the screen between the bars** (`DockedFrame::Modal`), as D09's and D10's pixels show, not the whole screen as T1 drew it. Rows 0 to 71 and 984 down are as they were; rows 72 to 983 are dimmed in both drawings.

## How it was measured

| Measure | Value |
|---|---|
| Modal | 360, 150, 1200 by 740; the divider at 800.5 |
| Menu | title at 393, 194; its rule at 223.5, to 768; items 375 by 62 from 242, 72 apart; the number's box 34 square at 412, 14 down; the label at 464; the footer at 842.5 |
| Panel | title at 841, 205; table 841 to 1519 from 243, its header 40 tall and its rows 48; columns at 860, 1241 and right at 1500; the count 36.5 below the table |
| Buttons | `ESC · BACK` 1077, 797, 151 by 57; the lit one 1242, 797, 277 by 57 |
| Backdrop | rows 72 to 983, compared with D01 pixel by pixel on three columns, in D09 and D10 |

The positions, sizes and colours were read from the drawing's pixels. D10 was drawn by the shell's code, compiled with g++ against the portable canvas and rasterised with the Inter typeface, then compared by eye with the drawing, in each of its states.

**The tests.**
- `DiscScreenTests` (7 tests) covers:
  - the keys and the clicks;
  - the catalogue's selection, the load, an empty disc and Esc;
  - the name, the bad name and the overwrite's question;
  - the delete's and the quit's questions and the device;
  - the catalogue's stamps and cash from real files;
  - D10 docked through Start: a save, a load, a disc error and its retry declined, a click, Esc back to the status screen and F1's launch;
  - the quit.
- `DockedDiscTests` draws `D10-Disc-Access`, `D10-Save-Overwrite`, `D10-Empty-Catalogue` and `D10-Disc-Error` on WARP for the owner to make golden (D94). It also checks the control a click finds, the scrolling, and the words. Those two checks were run here, through the same code, outside the test framework: 0 failures.

**What retires (D120, D108's way).**
- `DiscMenuAgreesOnEveryPath` and its known answers tested the original's menu: its texts, its keys, its prompt's bad names, and its catalogue.
- `ReadTextLineAgreesWhileEditing` typed at the menu's prompt, which is gone with `ReadTextLine`.
- `disc-menu.replay` is rewritten for D10: the catalogue, a delete asked and answered, a bad name, the mouse refused, the keyboard, a load and the status screen. It is recorded anew.

**What is rewritten.**
- Sixteen replays, and the twins that load a prepared commander, now load it with Esc, 1 and Enter, where they typed its name: `AiTests`, `DockedTests`.
- `save-and-load`:
  - saves with 2, the name erased and typed;
  - loads with 1 and Enter, the one file selected.
- `leave-for-dos`, `TwinStartAndExit` and `ReplayTests`' end step quit with 5 and Y.
- `TwinQuit` saves from D10 and quits.
- `TwinMouseFlight` takes the mouse with 6, Down and Enter.

**The re-recording.** Every replay and twin was dumped member by member on main and after the change, and compared at each digest by name. What differs is the original menu's working state:
- its text attribute and its text layout;
- the keys left in the key buffer and its indices;
- the typed line's countdown, its characters left and its cursor;
- at `TwinQuit`'s point before the save, the first byte of `commanderFileName`, which the prompt had cleared.

No member of the clock, the random state or the flight moved. Every progress outcome is as recorded but `disc-menu.replay`'s, which is new. No timing needed compensating, as no clock or random member moved at any digest. The digests were re-recorded from the run's own values, and the pictures and outcomes with the guard. The suite passes, 175 tests under g++, on two runs in check mode.

`CheckFormat` and `CheckProjectFiles` are clean. g++ (`-Wall -Wextra -Wpedantic -Wconversion -Wshadow`) and clang-tidy 18 are clean on the changed files.

| Not run here | the MSVC build and clang-tidy 22, which CI runs; `Application`'s clicks and drawing of D10, which only Windows compiles; D10 on WARP, whose images CI draws; the game played with D10 |
|---|---|

## What this forecloses

- A name is no longer typed to load or delete: a file not in the catalogue cannot be loaded.
- A save over a file asks first.
- The catalogue no longer leaves the game loop.
- The version, the original's V, is not shown.
- A test or a replay reaches the disc through D10's keys: Esc, the item's digit, the arrows and Enter.
