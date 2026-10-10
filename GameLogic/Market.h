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

} // namespace Elite
