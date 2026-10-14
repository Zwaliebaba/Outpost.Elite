// GameLogic/Market.h
#pragma once

#include "Docked.h"
#include "GameState.h"
#include "Hardware.h"
#include "Text.h"

#include <cstdint>
#include <optional>

namespace Elite
{

// The reference's market routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the market, cargo and prices. Each takes
// values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the 1987 routine of its name computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it, in
// the same order and at the same width.

/// What ParseQuantity reads.
struct Quantity
{
  std::uint16_t value;   ///< each digit added to ten times the low byte of what came before it
  bool valid;            ///< a number, with nothing but spaces around it, and at most 250
  std::size_t end;       ///< the index of the character after the last it read
  std::uint8_t lastRead; ///< that character, less '0' where it was read as a digit
};

/// RunCargoTradeMenu (CS:6B1E): the cargo menu, with OpenCargoMessage, ShowCargoMessage, SellCargo, BuyCargo, RefuseBuy and
/// ReadQuantity as one unit (ADR-012 item 14): the cursor moved by the steering and the arrow keys, and B or S with a quantity
/// typed on the buy or sell screen (tradeScreenIsBuy), each with its message. Returns Esc or the F-key that ends it, other than
/// the screen's own. Waits for keys as a rule (ADR-015).
[[nodiscard]] Task<ScreenKey> RunCargoTradeMenu(GameState& _state, Hardware& _hardware);

/// ShowMarketPricesScreen (CS:5E2C): the F8 screen, the system's name cut at its first space and every commodity's buy and
/// sell price, then the wait for the key that closes it (WaitForScreenExitKey). Waits for keys as a rule (ADR-015).
[[nodiscard]] Task<ScreenKey> ShowMarketPricesScreen(GameState& _state, Hardware& _hardware);

/// SubtractCredits (CS:65BA), the body PayForEquipmentItem runs into and SpendCredits jumps to: _tenths of credits off
/// creditsTenths, and the balance reformatted. Returns false, and the credits as they were, when they are not enough.
bool SubtractCredits(GameState& _state, std::uint32_t _tenths);

/// SpendCredits (CS:65EC): a jump to SubtractCredits, for _tenths of credits. Returns false when they are not enough.
bool SpendCredits(GameState& _state, std::uint32_t _tenths);

/// AddCredits (CS:65EE): _tenths of credits onto creditsTenths, ADD and then ADC, and the balance reformatted by
/// FormatCredits.
void AddCredits(GameState& _state, std::uint32_t _tenths);

/// ComputeResalePrice (CS:6995): resalePriceInput less one, less a 32nd of it halved until it is below 100;
/// 0 for 0.
[[nodiscard]] std::uint16_t ComputeResalePrice(const GameState& _state);

/// ComputeMarketPrices (CS:69CE): each commodity's buy and sell price into screenPrices, for the current system.
void ComputeMarketPrices(GameState& _state);

/// NextMarketRandom (CS:6A85): NextRandom's step on marketRandomState, and the number it makes.
std::uint16_t NextMarketRandom(GameState& _state);

/// The start of ShowBuyCargoScreen, named so that the market's known answers can call it (D33): the market's random state,
/// marketRandomState and the two words after it, taken from randomState0-2.
void StartMarketRandom(GameState& _state);

/// ShowBuyCargoScreen's roll of the quantity of a commodity on sale, the first time the screen shows after an arrival, named
/// so that the market's known answers can call it (D33): the next market random number r (NextMarketRandom), and
/// ((r & 1Fh) - 7) xor (r >> 8 & 3), or nothing, which the screen shows as none, when r & 1Fh is below 7. The economy plays
/// no part in it.
[[nodiscard]] std::optional<std::uint8_t> RollQuantityOnSale(GameState& _state);

/// ParseQuantity (CS:6A99): the decimal number in _text, a buffer typed into, spaces around it allowed. The buffer holds a NUL
/// after what was typed, where the reading stops.
[[nodiscard]] Quantity ParseQuantity(std::span<const std::uint8_t> _text);

/// PrintCargoQuantity (CS:6AD9): _quantity, or "-" for 0, in the quantity column of menuSelectedRow's row, in its
/// attribute, and textAttribute put back.
void PrintCargoQuantity(GameState& _state, std::uint16_t _quantity);

/// AddContrabandPenalty (CS:6DC1): menuSelectedRow's legal penalty onto legalStatus, unless the sum, in 8 bits,
/// is 0.
void AddContrabandPenalty(GameState& _state);

// ── The market's rules (D53, D74): values in, values out, no state and no screen ──
//
// What the routines above decide, apart from what they draw and where the game keeps it (ADR-043). The routines call these, so
// the corpus checks them while the 1987 screens stand; the screens of piece 5 call them in their place. Every sum is at the
// width the original computes it at.

/// The hold's tonnes without the extension, and with it.
inline constexpr std::uint8_t CARGO_BAY_TONNES = 20;
inline constexpr std::uint8_t LARGE_CARGO_BAY_TONNES = 35;

/// The most of a commodity weighed in kilograms or grams the hold carries.
inline constexpr std::uint8_t PRECIOUS_HELD_MOST = 250;

/// What a system's market and shipyard price by.
struct MarketPlace
{
  std::uint8_t economy;    ///< 0 to 7
  std::uint8_t government; ///< 0 to 7
  std::uint8_t techLevel;  ///< as the system data has it, from 0
};

/// A commodity's two prices, in tenths of a credit.
struct CommodityPrices
{
  std::uint16_t buyTenths;  ///< what the market sells it for
  std::uint16_t sellTenths; ///< what it pays for it, ResaleTenths of the buy price
};

/// What a trade in one commodity reads and changes.
struct CargoLot
{
  std::uint8_t commodity;      ///< from 0, in the market's order
  std::uint8_t held;           ///< in the hold, in the commodity's unit
  std::uint8_t onSale;         ///< in the market
  std::uint8_t usedTonnes;     ///< the hold's tonnes in use, of every commodity weighed in tonnes
  std::uint8_t legalStatus;    ///< the commander's
  std::uint32_t creditsTenths; ///< the commander's
};

/// Why a trade in cargo does not happen.
enum class CargoRefusal : std::uint8_t
{
  None,
  NothingOnSale,    ///< the market has none
  NotForSale,       ///< alien items, which the player may sell and never buy
  FullToCapacity,   ///< already the most of a precious commodity
  BayFull,          ///< every tonne of the hold in use
  NothingToSell,    ///< none in the hold
  NotEnoughOnSale,  ///< more than the market has
  NotEnoughHeld,    ///< more than the hold has
  CannotCarry,      ///< more than the hold, or the most of a precious commodity, takes
  NotEnoughCredits, ///< more than the commander can pay for
};

/// A trade's outcome: the lot after it, or the lot as it was and why not.
struct CargoTrade
{
  CargoRefusal refusal;
  CargoLot lot;
};

/// ComputeResalePrice's rule: what an item or a commodity sells back for at _priceTenths, which is _priceTenths less one, less
/// a 32nd of it halved until it is below 100; 0 for 0.
[[nodiscard]] std::uint16_t ResaleTenths(std::uint16_t _priceTenths) noexcept;

/// The tech level a market prices by: the system's, at most 9.
[[nodiscard]] std::uint8_t MarketTechLevel(std::uint8_t _techLevel) noexcept;

/// ComputeMarketPrices' rule: commodity _commodity's prices in the market of _place. The economy's and the government's factor
/// and the base price, then the commodity's adjustment and its tech-level factor times MarketTechLevel, each scaled over 256.
[[nodiscard]] CommodityPrices PricesOf(std::uint8_t _commodity, const MarketPlace& _place) noexcept;

/// SubtractCredits' rule: _creditsTenths less _priceTenths, or nothing when they are not enough.
[[nodiscard]] std::optional<std::uint32_t> CreditsAfterPaying(std::uint32_t _creditsTenths, std::uint32_t _priceTenths) noexcept;

/// AddCredits' rule: _creditsTenths and _earnedTenths, in 32 bits, as the original's ADD and ADC wrap them.
[[nodiscard]] std::uint32_t CreditsAfterEarning(std::uint32_t _creditsTenths, std::uint32_t _earnedTenths) noexcept;

/// The hold's tonnes, with the extension fitted (_largeBay) or without it.
[[nodiscard]] std::uint8_t CargoBayTonnes(bool _largeBay) noexcept;

/// Whether commodity _commodity counts towards the hold's tonnes: all but gold, platinum and gem-stones, which are weighed in
/// kilograms and grams and kept apart, up to PRECIOUS_HELD_MOST each.
[[nodiscard]] bool WeighedInTonnes(std::uint8_t _commodity) noexcept;

/// AddContrabandPenalty's rule: _legalStatus after a sale of commodity _commodity, its legal penalty added in 8 bits, or as it
/// was when that sum is 0.
[[nodiscard]] std::uint8_t LegalStatusAfterSelling(std::uint8_t _legalStatus, std::uint8_t _commodity) noexcept;

/// Why the market will not sell _lot's commodity at all, before a quantity is asked for: it has none, it is alien items, the
/// hold has the most of it, or, weighed in tonnes, the hold is full. CargoRefusal::None when it will.
[[nodiscard]] CargoRefusal RefuseToBuy(const CargoLot& _lot, bool _largeBay) noexcept;

/// Why the player cannot sell _lot's commodity at all, before a quantity is asked for: none in the hold. CargoRefusal::None
/// when they can.
[[nodiscard]] CargoRefusal RefuseToSell(const CargoLot& _lot) noexcept;

/// _amount of _lot's commodity bought at _buyTenths each, once RefuseToBuy found no refusal: refused when the market has less,
/// when the hold cannot carry it, in 8 bits as the original adds the tonnes, or when the credits are not enough. The hold gains
/// it, the market loses it, the tonnes in use go up when it is weighed in tonnes, and the credits pay for it.
[[nodiscard]] CargoTrade CargoBought(const CargoLot& _lot, bool _largeBay, std::uint8_t _amount, std::uint16_t _buyTenths) noexcept;

/// _amount of _lot's commodity sold at _sellTenths each, once RefuseToSell found no refusal: refused when the hold has less.
/// The hold loses it, the market gains it, up to FFh, the tonnes in use go down when it is weighed in tonnes, the legal status
/// takes its penalty (LegalStatusAfterSelling), and the credits are paid.
[[nodiscard]] CargoTrade CargoSold(const CargoLot& _lot, std::uint8_t _amount, std::uint16_t _sellTenths) noexcept;

} // namespace Elite
