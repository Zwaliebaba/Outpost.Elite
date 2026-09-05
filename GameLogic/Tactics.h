#pragma once

#include "Arith.h"
#include "Canvas.h"
#include "EliteTypes.h"
#include "Scanner.h"
#include "ShipMove.h"
#include "ShipSlot.h"

#include <cstdint>

namespace Elite
{

  /*
   * Slice 4a's geometry: the vectors `TACTICS` and `DOCKIT` decide with.
   *
   * Six routines that do nothing but arithmetic on two things' positions and orientations, and are
   * the whole of what phase 3 was missing before an AI could be written. They are here rather than
   * in `Scanner.h` -- which owns `K3` and the compass routines that fill it -- because their
   * callers are the tactics and the autopilot; and they are here rather than in `Arith.h` because
   * every one of them reads a `ShipBlock`, which the arithmetic kernel does not know about.
   *
   * `K3` IS THE SAME TEN BYTES THE COMPASS USES, and `K3Block` is now its name in both places
   * (§6.121). The original shares the zero page between `SPS1`, `CIRCLE2` and these, and nothing
   * here changes that: a caller declares one and passes it, and no two of the three are ever live
   * at once.
   */

  /*
   * 6502: TAS1 -- one axis of `K3` = this ship's coordinate MINUS the other object's.
   *
   * The original takes a pointer in `V(1 0)` and an index in Y of 2, 5 or 8 -- the SIGN byte of the
   * axis -- and walks down to the low byte with two `DEY`s, which leaves Y holding 0, 3 or 6: the
   * base index into both `INWK` and `K3`. The port takes that base directly as `_at`, because it is
   * what both arrays are indexed by and what `LoadPlanetAxis` already takes; the original's Y is
   * `_at + 2`.
   *
   * The subtraction is `EOR #%10000000` on the other object's sign and then `MVT3`, which ADDS --
   * negate one side and add is how sign-magnitude subtracts. `MVT3` leaves its answer's sign byte
   * in A as well as in `K+3`, and the `STA K3+2,X` right after the call is reading that register;
   * the port reads `_math.k[3]`, which is the same byte.
   *
   * `LDY U` after the call restores Y for a caller that wants it, and no caller in this build does.
   */
  void SubtractShipAxis(const ShipBlock& _other, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math, std::uint8_t _at) noexcept;

  /*
   * 6502: VCSUB -- all three axes, so `K3` becomes the vector FROM the other object TO this ship.
   *
   * `VCSU1` is the same routine with the pointer already set to the second ship block -- the space
   * station's slot, as `SPS4` uses it -- so in the port it is `SubtractStationAxes` below rather
   * than a separate body. `TACTICS` reaches `VCSUB` with the pointer taken out of `UNIV`, which is
   * the AI target's slot and can be any ship in the bubble; that is why this takes a block and not
   * a bubble.
   */
  void SubtractShipAxes(const ShipBlock& _other, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math) noexcept;

  /// 6502: VCSU1 -- `VCSUB` with `V` pointing at `K%+NI%`, which is where `NWSPS` puts the station.
  void SubtractStationAxes(const Bubble& _bubble, const ShipBlock& _work, K3Block& _axes, MathWorkspace& _math) noexcept;

  /*
   * 6502: TAS3 and TAS4 -- the dot product of `XX15` with one of a ship's orientation vectors.
   *
   * ONE BODY AND TWO LABELS. `TAS3` reads `INWK,Y` and `TAS4` reads `K%+NI%,Y`, and every other
   * instruction is the same, so the port has one function taking the block: `DOCKIT` passes the
   * station's, `TACTICS` passes the ship it is flying. `_at` is 10, 16 or 22 -- the nose, roof and
   * side vectors' x high bytes -- and the routine reads `_at`, `_at + 2` and `_at + 4`, the three
   * HIGH bytes, because an orientation vector's low bytes are not a direction.
   *
   * It ends by falling into `MAD` rather than calling it, so the answer is `MAD`'s (A X) pair:
   * the dot product as a sign-magnitude sixteen-bit value, with the sign in A's bit 7.
   */
  [[nodiscard]] AddSignedResult DotProductWithShip(const ShipBlock& _block, const DrawWorkspace& _draw, MathWorkspace& _math,
                                                   std::uint8_t _at) noexcept;

  /*
   * 6502: TAS6 -- point `XX15` the other way.
   *
   * Three `EOR #%10000000`s, which is what negation is in sign-magnitude: the magnitude is
   * untouched and only the sign bit moves. `XX15` is `X1`, `Y1` and `X2` in the draw workspace
   * (§6.37), the same six bytes `TAS2` and the line drawing share.
   */
  void NegateVector(DrawWorkspace& _draw) noexcept;

  /*
   * 6502: DCS1 -- move `K3` from the station to the IDEAL DOCKING POSITION, which is out in front
   * of the slot rather than at the station itself.
   *
   * IT OPENS `JSR P%+3`, WHICH CALLS THE REST OF ITSELF. The address three bytes on is the
   * instruction after the `JSR`, so the body runs once as a subroutine, returns into its own first
   * instruction, and runs again -- and each pass subtracts the nose vector doubled, so the two
   * together subtract it times four. The upstream comment says so in five words ("Run the following
   * routine twice"); a port that read the `JSR` as a call to something else, or skipped it as a
   * no-op, would place the docking point half as far out and would still dock.
   *
   * `TAS7` is the inner half, and it is a sign-magnitude add: `ASL A` doubles the nose vector's
   * high byte and pushes its SIGN into the carry, `LDA #0 / ROR A / EOR #%10000000` turns that
   * carry into the NEGATED sign, and the `EOR K3+2,X` after it decides add or subtract. So the
   * subtraction in the header is an addition of a negated vector, and the carry into the `ADC` is
   * clean because the `ROR` shifted a zero out of bit 0.
   *
   * The upstream header's third line reads `K3(8 7 6) = K3(8 7 6) - nosev_x_hi * 4`; the code
   * reads `K%+NI%+14`, which is nosev_z_hi. A copy-paste in the commentary, and the arithmetic is
   * what the port follows (§6.121).
   */
  void OffsetDockingPosition(const Bubble& _bubble, K3Block& _axes) noexcept;

  /// 6502: LDA K%+NI%+10 / +12 / +14 -- the three nose-vector high bytes `DCS1` reads, and the
  /// `LDX #0 / #3 / #6` that says which axis of `K3` each one moves.
  inline constexpr std::uint8_t NOSE_VECTOR_X = 10;
  inline constexpr std::uint8_t NOSE_VECTOR_Y = 12;
  inline constexpr std::uint8_t NOSE_VECTOR_Z = 14;

  /// 6502: LDY #10 / #16 / #22 -- the orientation vectors' high bytes, which is what `_at` selects
  /// in `DotProductWithShip`.
  inline constexpr std::uint8_t ORIENTATION_NOSE = 10;
  inline constexpr std::uint8_t ORIENTATION_ROOF = 16;
  inline constexpr std::uint8_t ORIENTATION_SIDE = 22;

  /// 6502: LDA #2 / STA (INF),Y with Y = 28, then `ASL A` and Y = 30 -- accelerate by two and dive
  /// at four, and the four is the two doubled rather than a second constant.
  inline constexpr std::uint8_t ANGRY_ACCELERATION = 2;

  /// 6502: bit 5 of NEWB -- "this ship is on the station's side", so hitting it angers the station
  /// as well; and bit 2, which is the hostile flag `ANGRY` sets.
  inline constexpr std::uint8_t NEWB_STATION_ALLY = 0x20;
  inline constexpr std::uint8_t NEWB_HOSTILE = 0x04;

  /*
   * 6502: ANGRY -- tell the ship in slot `_slot` that we just hit it.
   *
   * FOUR THINGS AND A TRAP. It makes the station hostile if the ship was the station or was one of
   * its own (`NEWB` bit 5); it turns the ship's AI on, but ONLY if the AI byte was already
   * non-zero, so a cargo canister stays a cargo canister; it accelerates and dives it; and it sets
   * the ship's own hostile bit -- but that last test reads `TYPE`, the flight loop's global, and
   * not the type this routine was called with. `FRMIS` calls it with the TARGET's type in A and
   * whatever the loop last moved in `TYPE`, so which ships turn hostile after a missile lock
   * depends on loop state the caller never set. The port keeps both bytes separate because the
   * original does (§6.121).
   */
  void Anger(Bubble& _bubble, const FlightState& _flight, std::uint8_t _slot, std::uint8_t _type) noexcept;

} // namespace Elite
