#pragma once

#include "Ports.h"
#include "Universe.h"

#include "Arith.h"
#include "Canvas.h"
#include "EliteTypes.h"
#include "Scanner.h"
#include "FlightLoop.h"
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
   * every one of them reads a `Ship`, which the arithmetic kernel does not know about.
   *
   * `K3` IS THE SAME TEN BYTES THE COMPASS USES, and `K3Block` is now its name in both places
   * (§6.121). The original shares the zero page between `SPS1`, `CIRCLE2` and these, and nothing
   * here changes that: a caller declares one and passes it, and no two of the three are ever live
   * at once.
   */

  /*
   * One axis of `K3` = this ship's coordinate MINUS the other object's.
   *
   * The original takes a pointer in `V(1 0)` and an index in Y of 2, 5 or 8 -- the SIGN byte of the
   * axis -- and walks down to the low byte with two `DEY`s, which leaves Y holding 0, 3 or 6: the
   * base index into both `INWK` and `K3`. The port takes that base directly as `_at`, because it is
   * what both arrays are indexed by and what `LoadPlanetAxis` already takes; the original's Y is
   * `_at + 2`.
   *
   * The subtraction FLIPS THE OTHER OBJECT'S SIGN BIT and then calls `MVT3`, which ADDS -- negate
   * one side and add is how sign-magnitude subtracts. `MVT3` leaves its answer's sign byte in the
   * accumulator as well as in `K+3`, and the store right after the call is reading that register;
   * the port reads the block `MVT3` hands back, which is the same byte.
   *
   * A pair of stores parks Y in `U` for a caller that wants it back, and no caller in this build
   * does; the port has no register to park.
   */
  [[nodiscard]] bool SubtractShipAxis(const Ship& _other, const Ship& _work, K3Block& _axes, std::uint8_t _at) noexcept;

  /*
   * All three axes, so `K3` becomes the vector FROM the other object TO this ship.
   *
   * `VCSU1` is the same routine with the pointer already set to the second ship block -- the space
   * station's slot, as `SPS4` uses it -- so in the port it is `SubtractStationAxes` below rather
   * than a separate body. `TACTICS` reaches `VCSUB` with the pointer taken out of `UNIV`, which is
   * the AI target's slot and can be any ship in the bubble; that is why this takes a block and not
   * a bubble.
   */
  /// Returns the carry the LAST of the three `MVT3`s exits with, which `TACTICS` rotates into the
  /// `DORND` at `TA64`: nothing between the two touches the flag (§6.126).
  [[nodiscard]] bool SubtractShipAxes(const Ship& _other, const Ship& _work, K3Block& _axes) noexcept;

  /// `VCSUB` with `V` pointing at `K%+NI%`, which is where `NWSPS` puts the station.
  [[nodiscard]] bool SubtractStationAxes(const Bubble& _bubble, const Ship& _work, K3Block& _axes) noexcept;

  /*
   * TAS3 and TAS4 -- the dot product of `XX15` with one of a ship's orientation vectors.
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
  [[nodiscard]] AddSignedResult DotProductWithShip(const Ship& _block, UnitVector _vector, std::uint8_t _at) noexcept;

  /*
   * Point `XX15` the other way.
   *
   * Three sign-bit flips, which is what negation is in sign-magnitude: the magnitude is untouched
   * and only the sign bit moves. `XX15` is `X1`, `Y1` and `X2` (§6.37), the same six bytes `TAS2`
   * and the line drawing share, and since M2-c the vector goes in and comes back.
   */
  [[nodiscard]] UnitVector NegateVector(UnitVector _vector) noexcept;

  /*
   * Move `K3` from the station to the IDEAL DOCKING POSITION, which is out in front
   * of the slot rather than at the station itself.
   *
   * IT OPENS BY CALLING THE REST OF ITSELF. The address three bytes on is the instruction after
   * the call, so the body runs once as a subroutine, returns into its own first instruction, and
   * runs again -- and each pass subtracts the nose vector doubled, so the two together subtract it
   * times four. The upstream comment says so in five words ("Run the following routine twice"); a
   * port that read the call as a call to something else, or skipped it as a no-op, would place the
   * docking point half as far out and would still dock.
   *
   * `TAS7` is the inner half, and it is a sign-magnitude add. Doubling the nose vector's high byte
   * pushes its SIGN into the carry; a zero rotated right pulls that carry back into bit 7, and a
   * flip negates it; and folding that against `K3`'s own sign is what decides add or subtract. So
   * the subtraction in the header is an addition of a negated vector, and the carry into the
   * addition is clean because the rotate shifted a zero out of bit 0.
   *
   * The upstream header's third line reads `K3(8 7 6) = K3(8 7 6) - nosev_x_hi * 4`; the code
   * reads `K%+NI%+14`, which is nosev_z_hi. A copy-paste in the commentary, and the arithmetic is
   * what the port follows (§6.121).
   */
  void OffsetDockingPosition(const Bubble& _bubble, K3Block& _axes) noexcept;

  /// The three nose-vector high bytes `DCS1` reads, paired with the 0, 3 and 6 that say
  /// which axis of `K3` each one moves.
  inline constexpr std::uint8_t NOSE_VECTOR_X = 10;
  inline constexpr std::uint8_t NOSE_VECTOR_Y = 12;
  inline constexpr std::uint8_t NOSE_VECTOR_Z = 14;

  /// The orientation vectors' high bytes, which is what `_at` selects in
  /// `DotProductWithShip`.
  inline constexpr std::uint8_t ORIENTATION_NOSE = 10;
  inline constexpr std::uint8_t ORIENTATION_ROOF = 16;
  inline constexpr std::uint8_t ORIENTATION_SIDE = 22;

  /// Byte 28 gets two and byte 30 gets it DOUBLED -- accelerate by two and dive at four, and
  /// the four is the two doubled rather than a second constant.
  inline constexpr std::uint8_t ANGRY_ACCELERATION = 2;

  /*
   * Tell the ship in slot `_slot` that we just hit it.
   *
   * FOUR THINGS AND A TRAP. It makes the station hostile if the ship was the station or was one of
   * its own (`NEWB` bit 5); it turns the ship's AI on, but ONLY if the AI byte was already
   * non-zero, so a cargo canister stays a cargo canister; it accelerates and dives it; and it sets
   * the ship's own hostile bit -- but that last test reads `TYPE`, the flight loop's global, and
   * not the type this routine was called with. `FRMIS` calls it with the TARGET's type in A and
   * whatever the loop last moved in `TYPE`, so which ships turn hostile after a missile lock
   * depends on loop state the caller never set. The port keeps both bytes separate because the
   * original does (§6.121).
   *
   * RETURNS THE CARRY IT EXITS WITH, which is a `CMP`'s every time and which part 11 of the flight
   * loop hands to `LL9` (§6.157): comparing against the station's type leaves it SET for the
   * station and set for any type above it, clear for one below; a ship with no AI byte returns on
   * that; one with an AI byte doubles the 2 it just stored -- CLEAR -- and then compares `TYPE`,
   * the loop's byte, against `CYL`.
   */
  bool Anger(Bubble& _bubble, const FlightState& _flight, std::uint8_t _slot, ShipType _type) noexcept;

  // ---- slice 4a-c: the AI, and the autopilot that shares its tail ------------------------------

  /// The turn rates and the firing cone `TACTICS` flies by. `DOCKIT` overwrites all three
  /// with 3, 6 and 29: an autopilot turns no faster but lines up twice as tightly and forgives a
  /// wider angle.
  inline constexpr std::uint8_t TACTICS_RAT = 3;
  inline constexpr std::uint8_t TACTICS_RAT2 = 4;
  inline constexpr std::uint8_t TACTICS_CNT2 = 22;
  inline constexpr std::uint8_t DOCKING_RAT2 = 6;
  inline constexpr std::uint8_t DOCKING_CNT2 = 29;

  /// What `OOPS` is given for a collision and for a missile going off.
  inline constexpr std::uint8_t COLLISION_DAMAGE = 80;
  inline constexpr std::uint8_t MISSILE_DAMAGE = 250;

  /// The AI byte a station gives the ship it launches, and the `NEWB` a rock hermit gives
  /// the pirate it turns into.
  inline constexpr std::uint8_t STATION_LAUNCH_AI = 0xF1;
  inline constexpr std::uint8_t HERMIT_PIRATE_NEWB = Mask(TraitBit::Innocent, TraitBit::Hostile);

  /// A station launches Vipers until there are four of them.
  inline constexpr std::uint8_t MAXIMUM_POLICE = 4;

  /// A trader runs from a random byte under 50, and a bounty hunter only turns on you once
  /// your legal status passes 40.
  inline constexpr std::uint8_t TRADER_FLEE_ROLL = 50;
  inline constexpr std::uint8_t BOUNTY_HUNTER_FIST = 40;

  /// "INCOMING MISSILE", which `SFRMIS` prints and `FRMIS` does not: the player's own launch
  /// is silent because the player pressed the key.
  inline constexpr std::uint8_t MESSAGE_INCOMING_MISSILE = 120;

  /*
   * TACTICS, all seven parts -- what a ship decides to do with the frame it was just moved
   * through. Returns FALSE when the player died, which is §6.122's answer to `OOPS` jumping to
   * `DEATH`.
   *
   * SEVEN PARTS AND ONE ROUTINE. The upstream splits it by page and the joins are fall-throughs:
   * part 2 runs into part 3 at `TA21`, part 3 into part 4 at `TA19`, part 4 into part 5 at `ta3`,
   * part 5 into part 6 at `TA3`, and part 6 into part 7 at `TA4`. Reading any part as a routine of
   * its own is §6.62's mistake, and part 1 is not the beginning: `TACTICS` is in part 2 and part 1
   * holds `TA18`, the missile's own logic, which part 2 branches back to.
   *
   * `MVEIT` calls it for one ship in eight and for a missile every pass, which is the whole reason
   * a missile is frightening and a Krait is not.
   */
  [[nodiscard]] bool RunTactics(Universe& _universe, Ports& _ports, std::uint8_t _slot) noexcept;

  /*
   * The docking computer, and it is the SAME TAIL as the AI.
   *
   * The plan had this as a slice of its own after `TACTICS` and the two cannot be split: `DOCKIT`
   * ends by jumping to `TA151` and refuses through `GOPL`, both inside `TACTICS`, while `TACTICS`
   * part 3 ends by jumping into `DOCKIT` (§6.122). What it adds in front of the shared steering is
   * an approach -- refuse unless the station is here, take the vector to it, then choose between
   * four cases by two dot products and a distance.
   *
   * IT ENDS BY READING A BYTE NOBODY GAVE IT. `K3+10` decides whether the ship has finished
   * docking, and the upstream comment says in as many words "I have no idea what K3+10 contains".
   * The port can settle it: `K3` is `SKIP 0` -- it has no storage of its own -- and it names the
   * first byte of `XX2`, which is `SKIP 14` and is `LL9`'s face-visibility array. So byte 10 is the
   * visibility of the ELEVENTH FACE OF THE LAST SHIP DRAWN, and whether an NPC completes its
   * docking depends on it (§6.125).
   */
  /*
   * IT ANSWERS NOTHING, and that is a finding rather than a simplification (M4-c-2).
   *
   * It returned "did the player survive" until 2026-09-07, and the answer was always yes: the
   * original `DOCKIT` reaches no `OOPS` and no `DEATH` -- every exit is a return or a jump to
   * `GOPL` or `TA151` -- so there is no path on which it could say no. Every caller discarded the
   * byte except one assertion in `TacticsTests`, which asserted the tautology.
   */
  void RunDockingComputer(Universe& _universe, Ports& _ports, std::uint8_t _slot) noexcept;

} // namespace Elite
