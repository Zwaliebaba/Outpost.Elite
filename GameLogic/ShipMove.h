#pragma once

#include "Arith.h"
#include "ShipSlot.h"

#include <cstdint>

namespace Elite
{

/*
 * Moving a ship (slice 3a).
 *
 * 6502: MVEIT and the primitives it is built from. A ship's position is three SIGN-MAGNITUDE
 * numbers of twenty-four bits each -- not two's complement -- so "add" is a comparison of signs
 * followed by either an addition or a subtraction, and the subtraction has to negate its own
 * result when it goes past zero. That is what these three routines are, in three shapes.
 */

/*
 * 6502: MVT1, and MVT1-2 which is the same routine two bytes earlier.
 *
 * INWK+X(3) = INWK+X(3) + (S R), where the sign of the addend is bit 7 of `_a`. The two entry
 * points differ only in whether `_a` is masked to its sign bit first, which is `_maskSign`:
 * `MVEIT` calls both, two instructions apart, because on one path A already holds nothing but a
 * sign and on the other it holds a whole coordinate byte.
 *
 * THE CARRY IS CLEARED BY AN `LSR`, not by a `CLC`. `ASL A / ... / LSR S` leaves bit 0 of a value
 * that was just shifted left, which is always zero, so the addition below it starts clean. A port
 * that read the `LSR` as arithmetic and dropped the flag would be adding an extra one about half
 * the time.
 */
void AddToShipCoordinate(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a, std::uint8_t _x,
                         bool _maskSign) noexcept;

/*
 * 6502: MVT3 -- K(4) = K(4) + INWK+X(3), sign-magnitude, with K's own sign in K+3.
 *
 * The same shape as MVT1 with the operands the other way round: the ship's coordinate is what is
 * added, and the answer stays in K. `MV40` -- the planet and sun path through `MVEIT` -- is what
 * reaches it.
 */
void AddShipCoordinateToK(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _x) noexcept;

/*
 * 6502: MVT6 -- (P+1 P+2) = (P+1 P+2) + INWK+X(2), and the sign comes back in A.
 *
 * Sixteen bits rather than twenty-four, and the sign is RETURNED rather than stored, because the
 * caller is in the middle of a rotation and wants it in the accumulator. The negation branch
 * returns `_a` with its sign flipped, which is the one place in this family where the answer's
 * sign is not the sign that went in.
 */
[[nodiscard]] std::uint8_t AddShipCoordinateToP(const ShipBlock& _work, MathWorkspace& _math,
                                                std::uint8_t _a, std::uint8_t _x) noexcept;

} // namespace Elite
