#pragma once

#include <cstdint>

namespace Elite
{

  /*
   * 6502: the VIC-II sprite registers, as DATA the port owns (ADR-005 §1, plan §6.133).
   *
   * WHY THIS EXISTS, and it is a design decision rather than a convenience. `SIGHT` and `PTCLS2`
   * both write VIC-II registers, and both did it through WRITE-ONLY seams -- `SightEffects` for
   * the laser sights and the Trumbles, `ExplosionEffects` for the burst. Write-only means nothing
   * downstream could read them, so nothing composited the sprites and three things the player
   * should see did not appear: the crosshairs (§6.100), the Trumbles, and the explosion sprite.
   *
   * ADR-005 §1 settled that compositing belongs in `Canvas::Resolve` -- not because `GameLogic` is
   * where tests live, which is the reasoning it started with and which does not survive being
   * pushed on, but because `Resolve` is ALREADY the un-oracled "what a person would see" layer
   * downstream of the bytes the oracle compares. That decision needs the register state to be
   * readable, and this is it.
   *
   * IT IS NOT A GETTER ON THE SEAMS, and that was the explicit instruction. The reason is already
   * written on `SightEffects::MaskSprites`: a getter invites a port to COMPUTE what the hardware is
   * holding, and part 15's read-modify-write exists precisely because the game does not know how
   * many Trumble sprites are showing. A struct the game owns can be read without inviting that,
   * because reading it is not asking the hardware a question.
   *
   * WHAT IS STILL WRITE-ONLY AND STAYS THAT WAY: `SetRasterMode`. That is `SETL1`, the 6510's
   * input/output port -- memory banking, not a VIC-II register -- and §6.59 refused it a place in
   * `GameLogic` because it is self-modifying code inside the interrupt handler. It has no effect a
   * composited image can show, so it is not here.
   *
   * WHAT THIS IS NOT VERIFIED BY, said plainly. Nothing. The game never rendered a composited
   * image into memory, so there is no oracle for the blit wherever it lives. What IS verified is
   * everything upstream of it: the seams' arguments are compared against the shipped code's
   * register writes, read-modify-writes included (§6.144), the sprite pointers are canvas bytes
   * compared since §6.73, and `SPRITE_DEFINITIONS` is byte-checked against the assembler's own
   * output. The blit rule itself -- sprite-over-bitmap priority, the multicolour bit pairs, the
   * expand flags -- is documented VIC-II behaviour with nothing here to compare against, and the
   * mitigation is a golden hash plus a hand-checked screenshot, not a coverage claim.
   */

  /// 6502: how many hardware sprites the VIC-II has. Bit N of the enable register is sprite N.
  inline constexpr std::size_t SPRITE_COUNT = 8;

  /// 6502: 24 pixels across, 21 rows down, three bytes per row -- 63 bytes and a padding byte,
  /// which is why a sprite pointer steps 64 bytes at a time.
  inline constexpr std::size_t SPRITE_BYTES = 64;
  inline constexpr int SPRITE_ROWS = 21;
  inline constexpr int SPRITE_ROW_BYTES = 3;
  inline constexpr int SPRITE_WIDTH = 24;

  /*
   * 6502: the VIC-II's own screen origin, which is what sprite coordinates are measured from.
   *
   * A sprite at x = 24, y = 50 sits in the top-left corner of the 320x200 display. Both numbers are
   * the hardware's and neither is a choice this port makes; they are here rather than at the blit
   * so that the one place they are subtracted can name them.
   */
  inline constexpr int SPRITE_ORIGIN_X = 24;
  inline constexpr int SPRITE_ORIGIN_Y = 50;

  /*
   * Which of the seven definitions are HI-RES rather than multicolour.
   *
   * `spritp` is written with two macros and the choice is per definition, not per sprite: the four
   * laser sights and the explosion cloud are `SPRITE2` (one bit per pixel, 24 across) and the two
   * Trumbles are `SPRITE4` (two bits per pixel, 12 across, each pixel double width). The VIC-II
   * carries this in bit N of register &1C, which this build never writes -- the loader sets it once
   * and the game leaves it alone -- so the port takes it from the DEFINITION, which is where the
   * distinction actually lives, rather than modelling a register nothing writes.
   */
  inline constexpr std::size_t SPRITE_DEFINITION_COUNT = 7;
  inline constexpr std::size_t FIRST_MULTICOLOUR_DEFINITION = 5;

  /// 6502: SPOFF% -- `(SPRITELOC% - SCBASE) / 64`, and `SPRITELOC%` is `SCBASE + &2800`, which is
  /// one byte past everything `Canvas` holds. So a pointer of 160 selects definition 0.
  inline constexpr std::uint8_t SPRITE_POINTER_ORIGIN = 160;

  /// 6502: VIC+&25 and VIC+&26 -- the two shared multicolour registers, which every multicolour
  /// sprite draws %01 and %11 from. The loader sets them and the game never does.
  inline constexpr std::uint8_t SPRITE_MULTICOLOUR_1 = 0x0A;
  inline constexpr std::uint8_t SPRITE_MULTICOLOUR_2 = 0x02;

  struct VideoState
  {
    /*
     * 6502: VIC+&15 -- which sprites are switched on, one bit each.
     *
     * Bit 0 is the laser sights, bit 1 is the explosion, and bits 2 to 7 are the Trumbles. `SIGHT`
     * writes the whole byte with the sights' bit ORed into the Trumbles', because neither can be
     * written without the other; part 15 ANDs it down to the lowest two when the cabin gets hot
     * enough to kill the Trumbles; and `PTCLS2` ORs bit 1 in on its own.
     */
    std::uint8_t enabled = 0;

    /*
     * 6502: VIC+&17 and VIC+&1D -- y-expand and x-expand, written with the SAME byte.
     *
     * One field and not two, because there is one write: `PTCLS2` stores the same value to both, so
     * a sprite is double size in both directions or in neither. Modelling them apart would invent a
     * state the game cannot reach.
     */
    std::uint8_t expanded = 0;

    /*
     * 6502: VIC+&10 with VIC+&0 to VIC+&F -- each sprite's position.
     *
     * `x` is NINE bits: the low eight in the per-sprite register and the ninth in bit N of VIC+&10.
     * It is stored whole here and split only where a register is actually being imitated, which is
     * the presenter's business and not the game's.
     */
    std::uint16_t x[SPRITE_COUNT] = {};
    std::uint8_t y[SPRITE_COUNT] = {};

    /// 6502: VIC+&27 to VIC+&2E -- each sprite's own colour, which for a multicolour sprite is
    /// only the %10 bit pair; %01 and %11 come from the two shared registers above.
    std::uint8_t colour[SPRITE_COUNT] = {};
  };

  /*
   * The seam calls, applied to a `VideoState`.
   *
   * These are free functions rather than methods so that the presenter's seam implementations are
   * one line each and cannot drift from what the register does. Each one is named for the seam
   * method it serves, and the comment on that method is the authority for what it means.
   */

  /// 6502: STA VIC+&27 -- sprite 0's colour, which is the sights'.
  void ApplySightColour(VideoState& _video, std::uint8_t _colour) noexcept;

  /// 6502: STA VIC+&15 -- the whole enable byte, sights and Trumbles together.
  void ApplySpritesEnabled(VideoState& _video, std::uint8_t _mask) noexcept;

  /// 6502: LDA VIC+&15 / AND #.. / STA VIC+&15 -- part 15's read-modify-write, which is why this
  /// is a separate call and not a second `ApplySpritesEnabled`.
  void ApplyMaskSprites(VideoState& _video, std::uint8_t _mask) noexcept;

  /// 6502: STA VIC+&17 / STA VIC+&1D -- the same byte into both expand registers.
  void ApplySpriteExpansion(VideoState& _video, std::uint8_t _mask) noexcept;

  /*
   * 6502: STX VIC+&2 / STY VIC+&3, the ninth x bit into bit 1 of VIC+&10, and bit 1 of VIC+&15.
   *
   * Sprite 1 is the explosion's, always. `PTCLS2` does not call this when the burst would be off
   * the screen -- it draws the particles anyway -- so an off-screen burst leaves the sprite where
   * it was, which is the original's behaviour and not an omission.
   */
  void ApplyExplosionSprite(VideoState& _video, std::uint16_t _x, std::uint8_t _y) noexcept;

  /// 6502: NOSPRITES -- every sprite off. It does not move or recolour anything, so neither does
  /// this: a sprite switched back on reappears exactly where it was.
  void ApplyHideAllSprites(VideoState& _video) noexcept;

} // namespace Elite
