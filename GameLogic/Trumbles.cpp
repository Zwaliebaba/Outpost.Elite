#include "pch.h"

#include "Trumbles.h"

#include "Controls.h"
#include "EliteTypes.h"
#include "LookupTables.h"

namespace Elite
{

  void MoveTrumbleSprites(TrumbleSprites& _sprites, VideoState& _video, Rng& _rng, std::uint8_t _mainLoopCounter,
                          SightEffects& _effects) noexcept
  {
    /*
     * 6502: LDA MCNT / AND #7 / CMP TRIBCT / BCC P%+5 / JMP NOMVETR.
     *
     * The compare is against `TRIBCT` and the branch is `BCC`, so the routine runs when the
     * counter is BELOW the number of sprites and returns otherwise. It is called only with
     * `TRIBCT` non-zero, so the accumulator is somewhere between 0 and 5 when it gets past here.
     */
    const std::uint8_t turn = static_cast<std::uint8_t>(_mainLoopCounter & TRUMBLE_TURN_MASK);
    if (turn >= _sprites.count)
    {
      return;
    }

    // 6502: ASL A / TAY -- and Y indexes the three RAM tables at two bytes a Trumble, while the
    // sprite it steers is Y/2 + 2, because sprites 0 and 1 belong to other slices.
    const std::size_t at = static_cast<std::size_t>(turn) * 2u;
    const std::size_t sprite = static_cast<std::size_t>(turn) + FIRST_TRUMBLE_SPRITE;

    // 6502: LDA #%101 / JSR SETL1 -- the video chip's registers, mapped in.
    _effects.SetRasterMode(TRUMBLE_RASTER_IO);

    /*
     * 6502: JSR DORND / CMP #235 / BCC MVTR1.
     *
     * The carry going in is the `ASL A` above, which cannot carry out of a value below eight, and
     * `SETL1` does not touch the flags. So this call is the carry-clear one.
     */
    const RngResult turnRoll = _rng.Next(false);
    if (turnRoll.value >= TRUMBLE_TURN_ROLL)
    {
      // 6502: AND #3 / TAX / LDA TRIBDIR,X / STA TRIBVX,Y / LDA TRIBDIRH,X / STA TRIBVXH,Y.
      const std::size_t xDirection = static_cast<std::size_t>(turnRoll.value & TRUMBLE_DIRECTION_MASK);
      _sprites.velocityX[at] = TRUMBLE_DIRECTION_TABLE[xDirection];
      _sprites.velocityXHigh[at] = TRUMBLE_DIRECTION_HIGH_TABLE[xDirection];

      /*
       * 6502: JSR DORND / AND #3 / TAX / LDA TRIBDIR,X / STA TRIBVX+1,Y.
       *
       * With the carry SET, because `CMP #235` set it on the way past and nothing since has
       * touched it. And the y axis reads `TRIBDIR` alone -- there is no high byte for it, so the
       * table's &FF is a whole velocity of -1 here where it is half of one above.
       */
      const RngResult driftRoll = _rng.Next(true);
      const std::size_t yDirection = static_cast<std::size_t>(driftRoll.value & TRUMBLE_DIRECTION_MASK);
      _sprites.velocityX[at + 1u] = TRUMBLE_DIRECTION_TABLE[yDirection];
    }

    /*
     * 6502: .MVTR1 LDA SPMASK,Y / AND VIC+&10 / STA VIC+&10 -- and there is no line for it here.
     *
     * The mask clears this sprite's ninth x bit out of the register the eight of them share, so
     * that the code below can put it back only if the new coordinate needs it. `VideoState` gives
     * each sprite a whole x (ADR-005 section 1), so the bit is not shared and there is nothing to
     * clear: the store at the bottom writes both halves at once.
     */

    /*
     * 6502: LDA VIC+5,Y / CLC / ADC TRIBVX+1,Y / STA VIC+5,Y.
     *
     * Eight bits, wrapping, and the source says why in as many words: "we don't worry about
     * whether the addition overflows, so Trumbles that move off the top or bottom of the screen
     * simply reappear on the opposite side".
     */
    _video.y[sprite] = AddWithCarry(_video.y[sprite], _sprites.velocityX[at + 1u], false).value;

    // 6502: CLC / LDA VIC+4,Y / ADC TRIBVX,Y / STA T -- the low byte, and it READS the register
    // back, which is why this takes a `VideoState` and not a write-only seam.
    const std::uint8_t previousLow = static_cast<std::uint8_t>(_video.x[sprite] & 0xFFu);
    const AddResult low = AddWithCarry(previousLow, _sprites.velocityX[at], false);
    std::uint8_t coordinateLow = low.value;

    // 6502: LDA TRIBXH,Y / ADC TRIBVXH,Y -- and the high byte, on the low byte's carry.
    std::uint8_t high = AddWithCarry(_sprites.coordinateXHigh[at], _sprites.velocityXHigh[at], low.carry).value;

    /*
     * 6502: BPL nominus / LDA #&48 / STA T / LDA #&01.
     *
     * The high byte is negative only when the sprite has walked off the left edge -- x was &0000
     * and the velocity was &FFFF -- so this is the wrap, and it puts the sprite at &148 rather
     * than at the width of the screen, because a sprite is still partly visible past 320.
     */
    if ((high & 0x80u) != 0u)
    {
      coordinateLow = TRUMBLE_RIGHT_EDGE_LOW;
      high = 0x01u;
    }

    /*
     * 6502: .nominus AND #1 / BEQ oktrib, then LDA T / CMP #&50 / LDA #1 / BCC oktrib, then
     * LDA #0 / STA T.
     *
     * Three ways to arrive at `oktrib`, and the accumulator is 0, 1 and 0 respectively -- which is
     * the ninth bit of the new coordinate. Only the middle one needs the right edge tested, and
     * the `LDA #1` sits BEFORE the branch that uses the compare's carry, so the value is loaded on
     * both paths and only the branch chooses.
     */
    high = static_cast<std::uint8_t>(high & 0x01u);
    if (high != 0u && coordinateLow >= TRUMBLE_RIGHT_EDGE_LIMIT)
    {
      high = 0u;
      coordinateLow = 0u;
    }

    // 6502: .oktrib STA TRIBXH,Y -- the shadow the game keeps because the shared register cannot
    // be read back one sprite at a time.
    _sprites.coordinateXHigh[at] = high;

    /*
     * 6502: BEQ NOHIBIT / LDA SPMASK+1,Y / ORA VIC+&10 / SEI / STA VIC+&10 / .NOHIBIT LDA T /
     * STA VIC+4,Y / CLI -- the ninth bit and the low eight, which are one store here.
     *
     * The `SEI`/`CLI` pair around the shared register is not modelled: it is there because a
     * raster interrupt could read VIC+&10 between the load and the store, and nothing in the port
     * races this.
     */
    _video.x[sprite] = static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8) | coordinateLow);

    // 6502: LDA #%100 / JSR SETL1 / JMP NOMVETR -- the registers mapped back out, and the jump
    // back into the flight loop that makes this a call written as two jumps (§6.82).
    _effects.SetRasterMode(TRUMBLE_RASTER_RAM);
  }

} // namespace Elite
