#pragma once

#include "Arith.h"
#include "Canvas.h"
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
  void AddToShipCoordinate(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a, std::uint8_t _x, bool _maskSign) noexcept;

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
  [[nodiscard]] std::uint8_t AddShipCoordinateToP(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a, std::uint8_t _x) noexcept;

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
  void RotateShipVector(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _y, std::uint8_t _alpha, std::uint8_t _beta) noexcept;

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
  void RotateCoordinatePair(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _x, std::uint8_t _y, std::uint8_t _rat2) noexcept;

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
  [[nodiscard]] std::uint8_t OrientationComponent(const ShipBlock& _work, MathWorkspace& _math, std::uint8_t _a, std::uint8_t _x,
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

  /*
   * 6502: MV40 -- move a PLANET or a SUN, which is the whole of `MVEIT` for a negative ship type.
   *
   * It is a BRANCH of `MVEIT` rather than a subroutine of it: `MV3` reaches it with `JMP MV40` and
   * it leaves with `JMP MV45`, back into `MVEIT`'s tail. So it skips the scanner, the acceleration
   * clamp and the ordinary rotation, and does its own -- which is right, because a planet is not on
   * the scanner, does not accelerate, and rotates about the player rather than about itself.
   *
   * The arithmetic is the player's roll and pitch applied in the opposite direction, which is what
   * makes the world turn when the ship does. Both `K` and `K2` are live across it, which is why
   * `MathWorkspace` carries two blocks.
   *
   * ITS ADDITIONS DISCARD THEIR LOW BYTE. `LDA K / CLC / ADC K2` throws the result away and keeps
   * only the carry, because the answer is stored from K+1 upwards -- the bottom byte exists solely
   * to carry into the byte above it. A port that stored it would be writing a fourth byte nothing
   * reads, and one that skipped the addition would lose the carry.
   */
  void MovePlanetOrSun(ShipBlock& _work, MathWorkspace& _math, std::uint8_t _alpha, std::uint8_t _beta) noexcept;

  /*
   * What `MVEIT` still reaches outside itself.
   *
   * There were two, and `SCAN` was the other: slice 3d-a built it, so `MoveShip` now calls it
   * directly and the seam is gone (§6.59). What is left is `TACTICS`, the combat AI, which is
   * PHASE 4 -- and `MVEIT` calls it directly too, so a port without it would quietly do nothing
   * where the game decides what a hostile ship does next.
   */
  class ShipEffects
  {
  public:
    virtual ~ShipEffects() = default;

    /// 6502: TACTICS -- decide what a hostile ship does next. Phase 4.
    virtual void RunTactics(ShipBlock& _work) = 0;
  };

  /*
   * 6502: ALPHA, ALP1, ALP2, BETA, BET1, BET2, DELTA, MCNT, XSAV, TYPE and RAT2 -- the flight state
   * `MVEIT` reads, none of which belongs to the ship it is moving.
   *
   * The roll and pitch are each kept THREE WAYS in the original -- signed, magnitude and sign -- and
   * that is not redundancy the port can collapse: `MVEIT` uses `ALPHA` for one multiply, `ALP1` for
   * another and `ALP2` for the sign of a third, all in the same rotation. Which one it reaches for
   * is part of the arithmetic.
   */
  struct FlightState
  {
    std::uint8_t alpha = 0;    ///< 6502: ALPHA -- roll, signed
    std::uint8_t alp1 = 0;     ///< 6502: ALP1 -- its magnitude
    std::uint8_t alp2 = 0;     ///< 6502: ALP2 -- its sign
    std::uint8_t alp2Next = 0; ///< 6502: ALP2+1 -- the sign flipped, which MVEIT uses as well
    std::uint8_t beta = 0;     ///< 6502: BETA -- pitch, signed
    std::uint8_t bet1 = 0;     ///< 6502: BET1 -- its magnitude
    std::uint8_t bet2 = 0;     ///< 6502: BET2 -- its sign
    std::uint8_t bet2Next = 0; ///< 6502: BET2+1 -- flipped, which the stardust uses as ALP2+1 is used
    std::uint8_t delta = 0;    ///< 6502: DELTA -- the player's speed

    /// 6502: DELT4(1 0) -- the speed times four, as sixteen bits. The stardust subtracts it from
    /// every particle's z on every frame, which is what makes the stars stream past.
    std::uint8_t delt4 = 0;
    std::uint8_t delt4Next = 0;

    /// 6502: MCNT and XSAV -- the main loop counter and the slot being moved. `MVEIT` uses their
    /// EOR to spread expensive work across iterations, so that `TIDY` runs on one ship every
    /// sixteenth pass and `TACTICS` on one every eighth, rather than on all of them at once.
    std::uint8_t mainLoopCounter = 0;
    std::uint8_t slot = 0;

    std::uint8_t type = 0; ///< 6502: TYPE -- negative for the planet and the sun

    /*
     * 6502: XX0(1 0) -- the blueprint the loop is working from, AND IT IS NOT RESET PER SHIP.
     *
     * Part 4 writes it only for a ship with a blueprint: `LDA TYPE / BMI MA21` skips the two loads
     * for the planet and the sun. So a body inherits whatever the last real ship left, and `MVEIT`
     * reads byte 15 of it to clamp the speed -- on the one path a body reaches that clamp, which is
     * when it is exploding or dead. It is loop state and not ship state, which is why it sits here
     * beside `TYPE` and `XSAV` rather than being passed down (§6.90).
     */
    std::uint16_t blueprint = 0;

    /// 6502: RAT and RAT2 -- scratch, but `MVEIT` leaves `RAT2` set and `PLUT` writes both as sign
    /// masks. Two routines, two meanings, the same two bytes; they are never live together because
    /// `PLUT` runs when the view changes and `MVS5` while a ship moves.
    std::uint8_t rat = 0;
    std::uint8_t rat2 = 0;
  };

  // 6502: MSL -- `SHIP_TYPE_MISSILE` is in `ShipSlot.h` with the other type numbers. `MVEIT`
  // singles it out so that a missile runs its tactics on EVERY iteration rather than one in eight;
  // a missile that thought once every eighth of a second would be trivial to outrun.

  /*
   * 6502: MVEIT -- move one ship, and the whole of slice 3a's arithmetic meets here.
   *
   * `_blueprint` is `XX0`, which the routine reads exactly once: byte 15, the maximum speed, to
   * clamp acceleration against. That single read is why the blueprints had to be extracted before
   * this slice could be compared against the game at all (§6.32).
   *
   * FOUR PATHS LEAVE IT DIFFERENT SHAPES. An exploding or dead ship (bits 5 or 7 of INWK+31) skips
   * everything but the scanner. A negative type is the planet or the sun and goes through `MV40`,
   * skipping the scanner, the acceleration and the ordinary rotation. The SUN skips the orientation
   * rotation too -- `AND #&81 / CMP #&81` is a test for type 129 and nothing else. Everything else
   * runs the lot.
   */
  /*
   * IT DRAWS, which is why the canvas is an argument. `MVEIT` calls `SCAN` twice -- once at `MV30`
   * for every ship, and again at the end of `MV5` for one that is neither exploding nor dead -- so
   * an ordinary ship's blip is EORed onto the scanner and off it again in the same call, at two
   * different positions: the old one and the new one. That is the whole of how a blip moves.
   *
   * `_view` is `QQ11`, which `SCAN` reads and `MVEIT` does not: the flight loop sets it, and the
   * port has no single home for it until 3d-d.
   */
  void MoveShip(Canvas& _canvas, DrawWorkspace& _draw, ShipBlock& _work, MathWorkspace& _math, FlightState& _flight, ShipEffects& _effects,
                std::uint16_t _blueprint, std::uint8_t _view) noexcept;

  /*
   * 6502: PLUT and PU1 -- flip a ship's axes for the view the player is looking through.
   *
   * Elite draws all four views with one piece of geometry: rather than four projections, it turns
   * the SHIP round. The rear view flips eight sign bytes; the left and right views swap x with z
   * throughout and then flip one of the two, which is what `RAT` and `RAT2` are -- one mask each,
   * built from a single `LDA #0 / CPX #2 / ROR A`.
   *
   * The ledger files this with the ship drawing, and it is not drawing: it is a transform of
   * `INWK`, in the same family as `MVS4` and `MVS5`, and the source's own category for it is
   * Flight. Its caller is the main flight loop, which calls both entry points -- `PLUT` reads the
   * view for itself and `PU1` is entered with it already in X.
   *
   * The third `PUS1` is a FALL-THROUGH rather than a call, and what it falls into is the `RTS` at
   * `LO2` -- the first byte of the next routine's file. The upstream comment says it falls into
   * `LOOK1`, which is thirteen bytes further on; it is `LO2` that returns it.
   */
  void FlipAxesForView(ShipBlock& _work, FlightState& _flight, std::uint8_t _view) noexcept;
  void FlipAxes(ShipBlock& _work, FlightState& _flight, std::uint8_t _view) noexcept;

} // namespace Elite
