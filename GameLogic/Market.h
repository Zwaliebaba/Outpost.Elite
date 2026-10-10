// GameLogic/Market.h
#pragma once

#include "NativeEntry.h"
#include "Text.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's market routines, ported (plan §5 Phase 3, ADR-010): the market, cargo and prices. Each body is declared
// here once it is ported, on the registers of its contract in Symbols.tsv. The routines de-assembled so far (ADR-012) take
// values and give values back, and their entries, at the end, keep the register contracts.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> MarketEntries() noexcept;

/// ShowMarketPricesScreen (CS:5E2C): the F8 screen, every commodity's buy and sell price, then the wait for a key. Waits.
/// Out: AH=the key that ended it.
void ShowMarketPricesScreen(Guest& _guest);

/// SpendCredits (CS:65EC): SubtractCredits.
void SpendCredits(Guest& _guest);

/// AddCredits (CS:65EE): BX:AX tenths of credits onto creditsTenths, and the balance reformatted.
void AddCredits(Guest& _guest);

/// RunCargoTradeMenu (CS:6B1E): the cargo menu: the cursor, and B or S with a typed quantity on the buy or sell
/// screen (tradeScreenIsBuy). Waits. Out: AH=Esc or the F-key that ended it.
void RunCargoTradeMenu(Guest& _guest);

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// What ParseQuantity reads.
struct Quantity
{
  std::uint16_t value;   ///< each digit added to ten times the low byte of what came before it
  bool valid;            ///< a number, with nothing but spaces around it, and at most 250
  std::uint16_t end;     ///< one past the last character it read
  std::uint8_t lastRead; ///< that character, less '0' where it was read as a digit
};

/// SubtractCredits (CS:65BA), the body PayForEquipmentItem runs into and SpendCredits jumps to: _tenths of credits off
/// creditsTenths, and the balance reformatted. Returns false, and the credits as they were, when they are not enough.
bool SubtractCredits(GameState& _state, std::uint32_t _tenths);

/// ComputeResalePrice (CS:6995): resalePriceInput less one, less a 32nd of it halved until it is below 100;
/// 0 for 0.
[[nodiscard]] std::uint16_t ComputeResalePrice(const GameState& _state);

/// ComputeMarketPrices (CS:69CE): each commodity's buy and sell price into screenPrices, for the current system.
void ComputeMarketPrices(GameState& _state);

/// NextMarketRandom (CS:6A85): NextRandom's step on marketRandomState, and the number it makes.
std::uint16_t NextMarketRandom(GameState& _state);

/// ParseQuantity (CS:6A99): the decimal number at DS:_text, spaces around it allowed.
[[nodiscard]] Quantity ParseQuantity(const GameState& _state, std::uint16_t _text);

/// PrintCargoQuantity (CS:6AD9): _quantity, or "-" for 0, in the quantity column of menuSelectedRow's row, in its
/// attribute, and textAttribute put back.
PrintedText PrintCargoQuantity(GameState& _state, std::uint16_t _quantity);

/// AddContrabandPenalty (CS:6DC1): menuSelectedRow's legal penalty onto legalStatus, unless the sum, in 8 bits,
/// is 0. Returns that sum.
std::uint8_t AddContrabandPenalty(GameState& _state);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes its
// results back there. The registers the contract leaves to the routine it hands to Guest::Clobber.

void SubtractCreditsEntry(Guest& _guest); ///< In: BX:AX the tenths. Out: CF=1 when they are not enough.
void ComputeResalePriceEntry(Guest& _guest);
void ComputeMarketPricesEntry(Guest& _guest);
void NextMarketRandomEntry(Guest& _guest);
void ParseQuantityEntry(Guest& _guest);
void PrintCargoQuantityEntry(Guest& _guest);
void AddContrabandPenaltyEntry(Guest& _guest);

} // namespace Elite
