// GameLogic/Market.h
#pragma once

#include "NativeEntry.h"

#include <span>

namespace Elite
{

// The reference's market routines, ported (plan §5 Phase 3, ADR-010): the market, cargo and prices. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> MarketEntries() noexcept;

/// ShowMarketPricesScreen (CS:5E2C): the F8 screen, every commodity's buy and sell price, then the wait for a key. Waits.
/// Out: AH=the key that ended it.
void ShowMarketPricesScreen(Guest& _guest);

/// SubtractCredits (CS:65BA), the body PayForEquipmentItem runs into and SpendCredits jumps to: BX:AX tenths of
/// credits off creditsTenths, and the balance reformatted. Out: CF=1, and the credits unchanged, when they are not
/// enough.
void SubtractCredits(Guest& _guest);

/// SpendCredits (CS:65EC): SubtractCredits.
void SpendCredits(Guest& _guest);

/// AddCredits (CS:65EE): BX:AX tenths of credits onto creditsTenths, and the balance reformatted.
void AddCredits(Guest& _guest);

/// ComputeResalePrice (CS:6995): resalePriceInput less one less a 32nd of it halved below 100. Out: AX;
/// ZF set if AX is 0.
void ComputeResalePrice(Guest& _guest);

/// ComputeMarketPrices (CS:69CE): each commodity's buy and sell price into screenPrices, for the current
/// system.
void ComputeMarketPrices(Guest& _guest);

/// NextMarketRandom (CS:6A85): NextRandom's step on marketRandomState. Out: AX.
void NextMarketRandom(Guest& _guest);

/// ParseQuantity (CS:6A99): the decimal number at DI, spaces around it allowed. Out: AX; CF=1 if it is not a
/// number or is above 250.
void ParseQuantity(Guest& _guest);

/// PrintCargoQuantity (CS:6AD9): AX, or "-" for 0, in the quantity column of menuSelectedRow's row.
void PrintCargoQuantity(Guest& _guest);

/// RunCargoTradeMenu (CS:6B1E): the cargo menu: the cursor, and B or S with a typed quantity on the buy or sell
/// screen (tradeScreenIsBuy). Waits. Out: AH=Esc or the F-key that ended it.
void RunCargoTradeMenu(Guest& _guest);

/// AddContrabandPenalty (CS:6DC1): menuSelectedRow's legal penalty onto legalStatus, unless the sum is 0.
void AddContrabandPenalty(Guest& _guest);

} // namespace Elite
