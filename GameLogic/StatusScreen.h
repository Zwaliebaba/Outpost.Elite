#pragma once

#include "Commander.h"
#include "MarketScreen.h"
#include "Universe.h"

#include <cstdint>

namespace Elite
{

/*
 * 6502: STATUS -- the Status Mode screen (slice 2c).
 *
 * A report rather than a transaction: what the ship is carrying, what condition it is in, how
 * legal it is, what the player's rating is, and what equipment is fitted. It reads no keys, so
 * unlike the trading screens it needed no seam to be portable -- only the commander block, which
 * slice 2d built.
 */

/*
 * 6502: QQ12, JUNK, FRIN and ENERGY -- the four bytes the condition line reads.
 *
 * None of this belongs to slice 2c and none of it exists yet: JUNK, FRIN and ENERGY are the local
 * ship bubble and the energy banks, which are phase 3. They arrive as VALUES for the same reason
 * the market's state does when the charts read it (§6.12): the arithmetic that reads them is this
 * slice's even though the bytes are not.
 */
struct ShipCondition
{
  std::uint8_t docked = 1;      ///< 6502: QQ12 -- non-zero when docked, and then nothing else is read
  std::uint8_t junkCount = 0;   ///< 6502: JUNK -- how many of the ship slots hold junk
  std::uint8_t firstShip = 0;   ///< 6502: FRIN+2,Y with Y = JUNK -- the first slot past the junk
  std::uint8_t energy = 0;      ///< 6502: ENERGY -- the energy banks
};

/*
 * 6502: STATUS -- and the one part of it that is not a straight read is the rating.
 *
 * The rating is derived from the kill tally by counting SHIFTS rather than by comparing against
 * thresholds: the low byte is shifted right twice and then once at a time until it reaches zero,
 * and the number of shifts is the rating. So the bands double in width -- 1, 2, 4, 8, 16 kills
 * and so on -- which is why the gap between Average and Above Average is nothing like the gap
 * between Deadly and Elite. Only when the tally passes 256 does it switch to comparisons.
 *
 * `TT111` is called at the top, and not for anything it prints. It leaves the selected system in
 * QQ15, which is what the screen's own title line reads -- so the crosshair position is an
 * argument and the galaxy and the current system come out of the commander block, where the
 * original reads them from too.
 *
 * The rule under the title is NLIN3's and belongs to the canvas, so a caller draws it -- the same
 * split the market screen and the inventory already use.
 */
void StatusScreen(TradeScreen& _screen, const CommanderBlock& _commander, const ShipCondition& _condition,
                  std::uint8_t _crosshairX, std::uint8_t _crosshairY, SystemSeeds& _outSelected) noexcept;

} // namespace Elite
