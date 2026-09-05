#pragma once

#include "Rng.h"
#include "VideoState.h"

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
   * Trumble is -- there is no shadow of them in RAM. That is why this routine takes a `VideoState`
   * and not a `SightEffects`: the register seams are WRITE-ONLY by design (`VideoState.h`), and a
   * routine that has to read a register back cannot be served by one. `SETL1` still is, because
   * it is memory banking rather than a register and nothing can read it.
   */

  /// 6502: TRIBCT's range -- "the number of Trumble sprites we are showing, 0 to 6", and the six
  /// are VIC-II sprites 2 to 7 because 0 and 1 are the laser sights and the explosion.
  inline constexpr std::uint8_t TRUMBLE_SPRITE_MAX = 6;

  /// 6502: the Trumbles are VIC-II sprites 2 to 7 -- 0 is the laser sights and 1 is the explosion,
  /// so Trumble N is sprite N + 2 and `VIC+4,Y` with Y = 2N is that sprite's x register.
  inline constexpr std::size_t FIRST_TRUMBLE_SPRITE = 2;

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

    /*
     * 6502: TRIBXH -- bit 8 of the x coordinate, and it is here rather than in `VideoState`
     * because the GAME keeps it here.
     *
     * The ninth bit of a sprite's x lives in a register shared by all eight sprites, so it cannot
     * be read back one sprite at a time -- which is why the game shadows it in RAM and why
     * `SPMASK` exists at all. `VideoState` gives each sprite a whole sixteen-bit x, so the port
     * needs no masks; it still needs this byte, because this byte is what the routine reads.
     */
    std::array<std::uint8_t, TRUMBLE_VELOCITY_COUNT> coordinateXHigh{};
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
   * `SPMASK` IS NOT PORTED AND THE ABSENCE IS THE POINT. Its twelve bytes are a pair of masks per
   * sprite for clearing and setting that sprite's bit in VIC+&10, and they exist because the ninth
   * x bit of eight sprites shares one register. `VideoState` gives each sprite a whole sixteen-bit
   * x (ADR-005 section 1), so there is nothing to mask: the read-modify-write of a shared byte
   * becomes a store. The two are equivalent because the masks are correct -- clearing a bit and
   * then setting it back cannot touch another sprite's.
   *
   * Returns nothing: everything it decides is in `_sprites` and `_video`, and the second of those
   * is what the presenter composites.
   */
  void MoveTrumbleSprites(TrumbleSprites& _sprites, VideoState& _video, Rng& _rng, std::uint8_t _mainLoopCounter,
                          SightEffects& _effects) noexcept;

} // namespace Elite
