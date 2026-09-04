#pragma once

#include "Arith.h"
#include "ShipSlot.h"

#include <cstdint>

namespace Elite
{

/*
 * Putting a ship on the screen (slice 3b).
 *
 * 6502: PROJ and the divide it is built on. Everything the space view draws goes through this:
 * a ship's position is where it is RELATIVE TO THE PLAYER in three dimensions, and turning that
 * into a pixel is one division per axis, x / z and -y / z, added to the centre of the view.
 *
 * The whole of the perspective in Elite is those two divisions. There is no matrix and no
 * near plane -- a ship behind the player is not clipped here at all, it is rejected earlier by
 * the sign of z, and a ship too far off to one side is rejected by the overflow test below.
 */

/// 6502: X and Y, from the constants block -- the centre of the space view, which the resolved
/// C64 source annotates as "the 256 x 144 space view". Canvas::SPACE_VIEW_HEIGHT is the 144.
inline constexpr std::uint8_t SPACE_VIEW_CENTRE_X = 128;
inline constexpr std::uint8_t SPACE_VIEW_CENTRE_Y = 72;

/// 6502: the coordinate bytes of a ship's data block -- x, y and z, each a sixteen-bit magnitude
/// and a sign byte. Named here rather than in `ShipSlot.h` because this is the first code that
/// reads them as a POSITION rather than as bytes to copy about.
inline constexpr std::uint8_t SHIP_X_OFFSET = 0;
inline constexpr std::uint8_t SHIP_Y_OFFSET = 3;
inline constexpr std::uint8_t SHIP_Z_OFFSET = 6;

/*
 * What `PLS6` leaves behind. The original's contract is "(X K)", a sixteen-bit value split
 * between a register and a zero-page byte, plus the carry -- and plus A, which is not incidental
 * either: `SHPPT` reads what `PROJ` leaves in A instead of testing the carry.
 *
 * `low` and `high` are only meaningful when `overflow` is false. On the overflow path that
 * `PL21` takes, the original never loads X at all and it still holds whatever the caller left
 * there; nothing reads it, so nothing here pretends to know what it was.
 */
struct ScreenOffset
{
  std::uint8_t low = 0;  ///< 6502: K
  std::uint8_t high = 0; ///< 6502: X
  std::uint8_t a = 0;    ///< 6502: A on return
  bool overflow = false; ///< 6502: the C flag -- set when the magnitude reached 1024
};

/*
 * 6502: DVID3B2 -- K(3 2 1 0) = (A P+1 P) / (z_sign z_hi z_lo).
 *
 * The two-instruction preamble that turns `DVID3B` into "divide by this ship's z", and the
 * `ORA #1` in it is load-bearing: it is what guarantees the non-zero denominator the divide
 * needs, and a ship at z = 0 is a real thing the game produces.
 *
 * Filed under `ShipMove.cpp` by the ledger and built here instead, because what it is ABOUT is
 * the projection -- it exists to divide by a ship's distance, and its callers are `PLS6` here
 * and `PLANET`/`PLS1` in slice 3c. Nothing in the movement code calls it.
 */
void DivideByShipZ(const ShipBlock& _ship, MathWorkspace& _math, std::uint8_t _a) noexcept;

/*
 * 6502: PLS6 (with its PL21, PL44 and PL6 exits) -- (X K) = (A P+1 P) / z, overflowing at 1024.
 *
 * The overflow test is in two halves and both matter: the top two bytes of the quotient must be
 * zero, and then its high byte must be under four. 1024 rather than 256 because a planet's
 * CENTRE can be well off the screen while its edge is still visible, so the projection has to
 * survive coordinates the view cannot show.
 *
 * The negation at the end is two's complement across the pair -- the only place in the geometry
 * where a sign-magnitude number is converted, because a screen coordinate is an offset from the
 * centre and has to be added to it.
 */
[[nodiscard]] ScreenOffset DivideToScreenOffset(const ShipBlock& _ship, MathWorkspace& _math,
                                                std::uint8_t _a) noexcept;

/// 6502: K3(1 0) and K4(1 0) -- where a point landed on the screen, as sixteen bits per axis so
/// that a shape whose centre is off the edge still has somewhere to be drawn from. These are the
/// same zero-page bytes the short-range chart uses for the range circle's centre, one byte each;
/// `RangeCircle` in `Charts.h` is that use and this is not it.
struct Projection
{
  std::uint8_t x = 0;  ///< 6502: K3
  std::uint8_t x1 = 0; ///< 6502: K3+1
  std::uint8_t y = 0;  ///< 6502: K4
  std::uint8_t y1 = 0; ///< 6502: K4+1
};

/// What `PROJ` returns, which is not just the carry: `SHPPT` ignores the carry entirely and
/// branches on `A OR K3+1` instead, so the accumulator is part of the contract.
struct ProjectResult
{
  bool offScreen = false; ///< 6502: the C flag
  std::uint8_t a = 0;     ///< 6502: A -- K4+1 when the point projected, and not that when it did not
};

/*
 * 6502: PROJ -- project a ship, planet or sun onto the screen.
 *
 *   K3(1 0) = #X + x / z
 *   K4(1 0) = #Y - y / z
 *
 * The minus on y is an `EOR #128` on the sign byte before the divide, because space has y going
 * up and the screen has it going down.
 *
 * `_screen` is written a HALF AT A TIME. If x projects and y overflows, K3 has already been
 * stored and K4 has not, and the original leaves it that way; no caller reads either after an
 * overflow, but a port that computed both and assigned at the end would be a different routine.
 */
ProjectResult Project(const ShipBlock& _ship, MathWorkspace& _math, Projection& _screen) noexcept;

} // namespace Elite
