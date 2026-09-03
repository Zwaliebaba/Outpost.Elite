#pragma once

#include "Rng.h"

#include <array>
#include <cstdint>

namespace Elite
{

/*
 * Elite's economy (slice 2c, the price model).
 *
 * Seventeen goods, and no price is stored for any of them. A price is the item's base, plus a
 * masked slice of a per-market random byte, adjusted by the system's economy through a gradient
 * that can push either way -- so an agricultural world is cheap for food and dear for machinery
 * without either fact being written down anywhere.
 *
 * The gradient's sign does OPPOSITE things to the two halves of the model. For a price, a
 * negative gradient subtracts; for the quantity on the shelf, a negative gradient ADDS. That is
 * the economics rather than an oversight -- what a world makes cheaply it also has plenty of --
 * and the two branches are forty instructions apart in the original, in routines that otherwise
 * read the same.
 */

/// The number of tradeable goods. The market screen loops to 17, so seventeen it is -- but see
/// GenerateMarket, which fills only sixteen.
inline constexpr int MARKET_ITEM_COUNT = 17;

/// 6502: QQ23 -- four bytes an item, in the order the routines index them.
struct MarketItem
{
  std::uint8_t basePrice = 0;    ///< QQ23+0
  std::uint8_t gradient = 0;     ///< QQ23+1, sign in bit 7 and magnitude in the low five bits
  std::uint8_t baseQuantity = 0; ///< QQ23+2
  std::uint8_t mask = 0;         ///< QQ23+3, how much of the market's random byte reaches this item
};

/// The market as a docked player sees it: what everything costs and how much there is.
struct MarketState
{
  std::uint8_t randomiser = 0; ///< 6502: QQ26, one DORND byte that perturbs the whole market
  std::array<std::uint8_t, MARKET_ITEM_COUNT> price{};
  std::array<std::uint8_t, MARKET_ITEM_COUNT> availability{}; ///< 6502: AVL
};

/// 6502: QQ23 -- the shipped table, read out of the assembled game.
[[nodiscard]] MarketItem MarketItemAt(int _item) noexcept;

/*
 * 6502: var -- the economy's contribution, which is the gradient's magnitude times the economy.
 *
 * The original spells the multiply as repeated addition with `ADC` and no `CLC` inside the loop,
 * so a carry out of one addition feeds the next. With an economy of at most 7 and a magnitude of
 * at most 31 the sum never reaches 256 and the carry never fires -- but the port keeps the chain
 * rather than writing a multiply, because "never" here is a property of the data rather than of
 * the code, and the data is not this function's to assume.
 */
[[nodiscard]] std::uint8_t EconomyAdjustment(std::uint8_t _gradient, std::uint8_t _economy) noexcept;

/*
 * 6502: TT151's arithmetic half -- what one item costs, in tenths of a credit.
 *
 * The printing half of TT151 belongs with the market screen; this is the part that decides the
 * number, and it is the part every other price in the game is derived from.
 */
[[nodiscard]] std::uint8_t MarketPrice(int _item, std::uint8_t _economy, std::uint8_t _randomiser) noexcept;

/*
 * 6502: GVL -- roll a new market, which happens on arrival at every system.
 *
 * Takes the RNG rather than a byte because the original calls DORND itself, and where the
 * randomness comes from is part of the behaviour: a caller that supplied its own byte would give
 * a different market from the same save.
 *
 * Fills sixteen items, not seventeen: the loop's bound is a comparison against 63 on an index
 * that steps by four, so it stops one short of Alien Items.
 *
 * Alien Items are still zeroed, but somewhere unexpected. The original does it inside `var`,
 * which computes the economy adjustment and, on its way out, writes zero to AVL+16 -- so every
 * path that works out a price also enforces that Alien Items cannot be bought. The port zeroes
 * it here instead, because EconomyAdjustment is arithmetic and has no market to reach into. The
 * effect is the same for every caller the game has; if a future one computes a price without
 * generating a market, it has to do this itself.
 */
void GenerateMarket(Rng& _rng, std::uint8_t _economy, MarketState& _outMarket) noexcept;

} // namespace Elite
