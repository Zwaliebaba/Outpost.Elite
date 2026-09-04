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

/*
 * 6502: MVS4 -- roll and pitch one of a ship's three orientation vectors.
 *
 * Four multiply-accumulates through `MAD`: y -= alpha*x, x += alpha*y, then y -= beta*z,
 * z += beta*y. `MVEIT` calls it three times, at INWK+9, +15 and +21, which are the ship's nose,
 * roof and side vectors -- so the whole orientation is rotated by running the same six bytes of
 * arithmetic on three different sixes.
 *
 * The subtractions are done by FLIPPING A SIGN BIT rather than by subtracting: `LDA INWK+1,Y /
 * EOR #128` hands `MAD` the same magnitude with the opposite sign, because these are
 * sign-magnitude numbers and negating one is a single bit.
 */
void RotateShipVector(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _y, std::uint8_t _alpha,
                      std::uint8_t _beta) noexcept;

/*
 * 6502: MVS5 -- rotate a PAIR of coordinates by a sixteenth, for the ship's own roll and pitch.
 *
 * Two halves that are the same code with the two indices swapped, and one `EOR #128` between them
 * which is what makes the pair rotate rather than both drift the same way. The angle is fixed:
 * `LSR A / ROR P` four times is a division by sixteen, so a ship rolls in sixteenths regardless of
 * how fast it is turning, and `_rat2` is the direction.
 *
 * The shrinking is not a rounding artefact either -- `LDA INWK+1,X / AND #127 / LSR A / STA T`
 * then subtracting T is a deliberate reduction of the vector's length on every step, which is
 * what stops the rotation from growing without bound. `TIDY` (through `NORM`) is what puts the
 * length back.
 */
void RotateCoordinatePair(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _x, std::uint8_t _y,
                          std::uint8_t _rat2) noexcept;

/*
 * 6502: TIS3, which FALLS INTO DVIDT -- one component of the third orientation vector, worked out
 * from the other two.
 *
 * `(-x_a * z_a - x_b * z_b) / x_c`, in the indices `_x`, `_y` and `_a`. The three arguments are
 * axis selectors into the two vectors at INWK+10 and INWK+16, stepping in twos, and `_a` picks
 * the axis the division is BY -- which is why `TIDY` chooses it from whichever component is
 * largest rather than always the same one.
 *
 * The fall-through is not incidental: `TIS3` sets up P, Q and A and then runs off its end into
 * the divider, so a caller of `TIS3` gets a division whether it wanted one or not.
 */
[[nodiscard]] std::uint8_t OrientationComponent(const ShipBlock& _work, MathWorkspace& _math,
                                               std::uint8_t _a, std::uint8_t _x,
                                               std::uint8_t _y) noexcept;

/*
 * 6502: TIDY -- put a ship's orientation vectors back into shape.
 *
 * `MVEIT` runs this on one ship every sixteenth iteration of the main loop, because the rounding
 * in `MVS4` and `MVS5` accumulates: the vectors slowly stop being unit length and stop being at
 * right angles to each other. This normalises the first, RECOMPUTES the third from the other two,
 * normalises that, and then rebuilds the second as their cross product.
 *
 * WHICH COMPONENT IT DIVIDES BY IS CHOSEN, not fixed. `AND #&60` on each component in turn asks
 * whether it is large enough to divide by safely, and `TI1`/`TI2` are the fallbacks when it is
 * not -- so the routine has three shapes depending on which way the ship happens to be pointing,
 * and a port that always used the first would be right until a ship pointed down an axis.
 */
void TidyOrientation(ShipBlock& _work, MathWorkspace& _math) noexcept;

} // namespace Elite
