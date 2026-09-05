#pragma once

#include "Rng.h"

#include <array>
#include <cstdint>

namespace Elite
{

  /*
   * Declared rather than included, and the direction is the reason: `SIGHT` is what fills in the
   * count below, so `Controls.h` includes THIS and not the other way round. `SetRasterMode` is
   * reached from `Trumbles.cpp`, which includes the header the class is defined in.
   */
  class SightEffects;

  /*
   * The Trumbles on the screen (slice 4d-a).
   *
   * 6502: MVTRIBS, and the four variables it walks -- TRIBCT, TRIBVX, TRIBVXH and TRIBXH -- plus
   * the six VIC-II sprites they steer. The Trumbles the player is CARRYING are a two-byte number
   * in the commander block and are counted, bred, burnt and sold in five other files already; this
   * is the half that has a position.
   *
   * THE COORDINATES LIVE IN THE VIDEO CHIP AND NOWHERE ELSE. `MVTRIBS` reads `VIC+&04+Y`, adds a
   * velocity to it and writes it straight back, so the registers are the only copy of where a
   * Trumble is -- there is no shadow of them in RAM. That is why the sprite bank below holds
   * `coordinates` rather than the presenter holding them: a port that kept the position outside
   * `GameLogic` would have nothing to add the velocity to.
   */

  /// 6502: TRIBCT's range -- "the number of Trumble sprites we are showing, 0 to 6", and the six
  /// are VIC-II sprites 2 to 7 because 0 and 1 are the laser sights and the explosion.
  inline constexpr std::uint8_t TRUMBLE_SPRITE_MAX = 6;

  /// 6502: the twelve registers VIC+&04 to VIC+&0F -- x and y for sprites 2 to 7, interleaved,
  /// which is why one index Y reaches both (`VIC+4,Y` and `VIC+5,Y`).
  inline constexpr std::size_t TRUMBLE_COORDINATE_COUNT = 12;

  /*
   * 6502: TRIBVX, TRIBVXH and TRIBXH -- `SKIP 16` each, and sixteen is what the port keeps.
   *
   * Section 6.8's rule sizes a TABLE from what can index it, and by that rule these would be
   * twelve: Y is twice a Trumble number below six, so `TRIBVX+1,Y` reaches offset 11 and no
   * further. These are not tables, though -- they are RAM the game reserves, and the reservation
   * is the definition. Keeping all sixteen is also what makes the comparison against the oracle
   * TOTAL rather than partial: the test asserts the four bytes nothing should touch are untouched,
   * instead of declining to look at them.
   */
  inline constexpr std::size_t TRUMBLE_VELOCITY_COUNT = 16;

  /*
   * 6502: LDA #%101 / JSR SETL1 ... LDA #%100 / JSR SETL1 -- the bracket around everything below.
   *
   * The same two values `SIGHT` and the explosion use. %101 maps the I/O registers in so the VIC-II
   * can be written; %100 maps them back out to RAM. The port has no bank switching and the seam
   * carries the calls anyway, because the raster handler is self-modified by them (§6.59) and
   * dropping them would be dropping half of what the routine does to the machine.
   */
  inline constexpr std::uint8_t TRUMBLE_RASTER_IO = 0x05;
  inline constexpr std::uint8_t TRUMBLE_RASTER_RAM = 0x04;

  /// 6502: LDA MCNT / AND #7 -- one Trumble per pass, so any one of them moves every eighth frame.
  inline constexpr std::uint8_t TRUMBLE_TURN_MASK = 7;

  /// 6502: CMP #235 -- above this a Trumble picks a new direction, which is 21 rolls in 256 and so
  /// a little over 8%. The upstream comment rounds it to "8% of the time".
  inline constexpr std::uint8_t TRUMBLE_TURN_ROLL = 235;

  /// 6502: AND #3 -- the four entries of `TRIBDIR`, and both axes are chosen with the same mask.
  inline constexpr std::uint8_t TRUMBLE_DIRECTION_MASK = 3;

  /*
   * 6502: LDA #&48 / STA T / LDA #&01, and CMP #&50 -- the two edges of the screen, in the
   * nine-bit coordinates a VIC-II sprite has.
   *
   * &148 is 328 and &150 is 336. A sprite whose x has gone negative is put at 328 and one that
   * has reached 336 is put at 0, so the six wander round a cylinder rather than piling up in a
   * corner. Nothing clamps the y coordinate at all -- it is eight bits and it wraps by itself.
   */
  inline constexpr std::uint8_t TRUMBLE_RIGHT_EDGE_LOW = 0x48;
  inline constexpr std::uint8_t TRUMBLE_RIGHT_EDGE_LIMIT = 0x50;

  /*
   * Where the six Trumbles are, how fast, and how many of them there are.
   *
   * 6502: TRIBCT, TRIBVX, TRIBVXH, TRIBXH, VIC+&04 to VIC+&0F and VIC+&10. One struct because one
   * routine writes all of it and because splitting it would put the ninth bit of a coordinate in a
   * different object from the other eight.
   *
   * `count` IS `TRIBCT`, and `SIGHT` is what writes it -- from `TRIBTA` under the population's
   * high byte. `MVTRIBS` only reads it, and reads it as the number of sprites to cycle through.
   */
  struct TrumbleSprites
  {
    /// 6502: TRIBCT -- how many of the six are showing. Written by `SIGHT`, read here.
    std::uint8_t count = 0;

    /// 6502: TRIBVX -- the low byte of the x velocity at even offsets, and the WHOLE y velocity
    /// at odd ones. Two different quantities in one array because the original interleaves them.
    std::array<std::uint8_t, TRUMBLE_VELOCITY_COUNT> velocityX{};

    /// 6502: TRIBVXH -- the high byte of the x velocity, which is 0 or &FF.
    std::array<std::uint8_t, TRUMBLE_VELOCITY_COUNT> velocityXHigh{};

    /// 6502: TRIBXH -- bit 8 of the x coordinate, kept in RAM because the register that holds it
    /// on the chip is shared with seven other sprites and cannot be read back per sprite.
    std::array<std::uint8_t, TRUMBLE_VELOCITY_COUNT> coordinateXHigh{};

    /// 6502: VIC+&04 to VIC+&0F -- x, y, x, y ... for sprites 2 to 7.
    std::array<std::uint8_t, TRUMBLE_COORDINATE_COUNT> coordinates{};

    /// 6502: VIC+&10 -- the ninth x bit of all EIGHT sprites, so bits 0 and 1 belong to the laser
    /// sights and the explosion and are read-modify-written round rather than through.
    std::uint8_t coordinateMsb = 0;
  };

  /*
   * 6502: MVTRIBS -- move one Trumble sprite, and which one depends on the frame.
   *
   * ONE SPRITE PER FRAME, CHOSEN BY THE MAIN LOOP COUNTER. `LDA MCNT / AND #7` counts 0 to 7 and
   * the routine returns immediately when that is not below `TRIBCT`, so with six Trumbles showing
   * the sprites move on passes 0 to 5 and nothing happens on 6 and 7. Each one therefore moves
   * every eighth frame however many there are, rather than each getting a slower share as more
   * appear.
   *
   * THE SECOND `DORND` RUNS WITH THE CARRY SET, and only the first has it clear. The carry into
   * the first is the `ASL A` two instructions above -- A is at most 7, so it is clear -- and
   * `SETL1` in between leaves the flags alone. The second is reached only by falling through
   * `CMP #235` without branching, which means A was at or above 235 and the compare set the
   * carry; nothing between there and the call clears it. The generator reads C (§6.118), so the
   * two calls are not interchangeable and a port that passed the same flag to both is wrong on
   * the y axis about half the time.
   *
   * THE X AXIS IS SIXTEEN BITS AND THE Y AXIS IS EIGHT. The x velocity is `(TRIBVXH TRIBVX)` and
   * a negative one is &FFFF; the y velocity is one byte of the SAME table, so its -1 is &FF and
   * the addition simply wraps. That is why a Trumble that drifts off the top reappears at the
   * bottom with no code to put it there, while one that drifts off the left needs four
   * instructions.
   *
   * Returns nothing: everything it decides is in `_sprites`, which is what the presenter reads.
   */
  void MoveTrumbleSprites(TrumbleSprites& _sprites, Rng& _rng, std::uint8_t _mainLoopCounter, SightEffects& _effects) noexcept;

} // namespace Elite
