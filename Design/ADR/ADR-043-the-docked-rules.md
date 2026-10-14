# ADR-043 — The docked screens, first step: the rules are functions with no screen code

**Status:** accepted 2026-10-14, with the change that implements it, the first step of piece 5 of Phase 6 ([Reverse-Engineering-Plan.md §5, Phase 6](../Reverse-Engineering-Plan.md#phase-6--the-new-game)), under the owner's rulings D53 and D74.

## Context

Piece 5 rewrites the docked screens as the canvas draws them (D44), and D53 guards them with rules that are functions with no screen code, tested with known answers, and golden images of the screens. The plan has the docked gaps D54 leaves to the owner drawn first: the disc menu, the laser mount, the missions, the longer equipment list, the errors and every screen's states. They are not drawn yet. The owner ruled:

- **D74 (2026-10-14).** The rules come first, as functions the 1987 screens call, so every replay stays exact while they stand; the screens wait for the drawings.
- **D75 (2026-10-14).** The status screen lists the cargo beside the equipment, which the canvas folds into the market's In Hold column; the owner draws it with the other gaps.

The rules were inside the 1987 routines, `Market.cpp`'s and `Equipment.cpp`'s, read from and written to the game's state as they decided, between the messages and the cursor's moves.

## Decision

1. **The market's rules** (`Market.h`) take values and give values: what a commodity costs and sells back for (`PricesOf`, `ResaleTenths`, `MarketTechLevel`), credits paid and earned (`CreditsAfterPaying`, `CreditsAfterEarning`), the hold's size and what it weighs in tonnes (`CargoBayTonnes`, `WeighedInTonnes`), the legal penalty of a sale (`LegalStatusAfterSelling`), and a trade in cargo: why the market refuses before a quantity is asked for (`RefuseToBuy`, `RefuseToSell`), and the lot after a purchase or a sale, or why not (`CargoBought`, `CargoSold`, `CargoLot`, `CargoRefusal`).
2. **The shipyard's rules** (`Equipment.h`) likewise: the equipment list's items, how many are on sale at a tech level and at what price (`EquipmentItemAt`, `EquipmentRowsOnSale`, `EquipmentPriceTenths`), what filling the tank costs (`FuelPriceTenths`), which rows sell lasers (`LaserTypeOfRow`), why an item cannot be fitted or taken back (`RefuseToFit`, `RefuseToRemove`, `Fitting`, `EquipmentRefusal`), what a purchase counts (`CountAfterFitting`), and the laser mounts (`LaserMounts`, `MountRefusal` and six functions).
3. **They live in `Market.cpp` and `Equipment.cpp`**, beside the tables they read, which D38 gives to one `.cpp` each (`Tools/GenerateTables.py` checks it). **The 1987 routines call them** and keep every write of the state they made, the cursors `ComputeMarketPrices` walks and the laser type `SelectLaserType` counts to among them, so the corpus still checks the rules.
4. **The rules keep the reference's arithmetic**, its quirks with it: the tonnes added in 8 bits, so 250 more tonnes fit on 10; a legal status of FFh that a sale's penalty would wrap to 0 left at FFh; the fuel's divide saturating at 7Fh where it overflows; the cargo bay extension refused with 21 tonnes aboard before the shipyard asks whether one is fitted; a hold refused only when exactly full. The new screens may change a rule on purpose, by a ruling, as D26 allows.
5. **Known answers test them.** `MarketAnswerTests.TheRulesPriceEveryMarketAsRecorded` gives `MarketAnswers.txt`'s 256 markets from `PricesOf`. `EquipmentAnswers.txt` records 1,040 answers, every shipyard at tech levels 0 to 15 and the fuel's cost at 16 prices, which `EquipmentAnswerTests` checks. `MarketRulesTests` and `EquipmentRulesTests` give the rest, quirks included.

A file of rules of its own was not taken: it would read the tables two `.cpp`s each own.

### Measured 2026-10-14

| Check | Result |
|---|---|
| The old routines and the new, side by side | A scratch harness, built into this tree and into `main` at `c04caec`, put the same inputs through both and hashed every result and the whole of `GameData` after each: `ComputeResalePrice` at all 65,536 prices; `ComputeMarketPrices` at 16,384 markets, every economy, government and tech level 0 to 255; `SubtractCredits` and `AddCredits` on 200,000 pairs, edges first; `AddContrabandPenalty` for 17 commodities at 256 statuses; `SelectLaserType` on 256 rows; `PayForEquipmentItem` for fuel at every level, 772 prices and six balances (1,185,792 payments), and 2,000 payments on each other row. Then 1,500 random docked commanders, each through the buy, sell or equip menu by keys, a row reached, B or S, a typed quantity or a mount chosen: 899 sessions changed the credits, 490 the hold, 84 the mounts and 162 the fuel or the missiles. Every hash identical. |
| The harness sees a wrong rule | Eleven rules broken one at a time, each in a copy of this tree: ten changed a hash. The eleventh, the mining laser's check for fuel scoops counting 0 where it counts not 1, differs only for a count of 2 or more, which play never makes; dropping the check was caught. |
| The shipyard | `ShowEquipShipScreen` in `main`, run by keys at every economy, government and tech level 0 to 15, lists the same rows at the same prices as `EquipmentRowsOnSale` and `EquipmentPriceTenths`: 1,024 identical lines, the ones `EquipmentAnswers.txt` records. |
| The idle trajectory | The probe of ADR-038 on this tree and on `main`: 166 tests, 43,159,730 idles, each test's count and hash identical. |
| The suite | `GameLogicTests` passes under g++, 184 tests, the corpus's digests, pictures, outcomes and answers among them, with no re-base |
| Unused code and lint | clang 18's warnings over `GameLogic`: none. clang-tidy 18 on the changed files: clean. mingw-w64 g++ compiles `Market.cpp`, `Equipment.cpp` and `Docked.cpp` with `-Wall -Wextra`: clean. |
| Not run here | the MSVC build, clang-tidy 22 and the game: CI builds and runs the tests and the lint, and the owner plays it |

## What this forecloses

- **A rule in screen code.** A docked screen asks the rules what a price is, why a trade is refused and what it changes; it draws the answer and stores it.
- **The new screens computing anything the rules compute**, until a ruling changes a rule, which is then changed here and its known answers re-recorded with the ruling.
