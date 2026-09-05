#pragma once

#include "Arith.h"
#include "Canvas.h"
#include "LineHeap.h"
#include "Rng.h"
#include "ShipDraw.h"
#include "ShipSlot.h"

#include <cstdint>

namespace Elite
{

  /*
   * The explosion cloud (slice 4b-b).
   *
   * 6502: DOEXP with PTCLS inside it, and the C64's own PTCLS2 beside them. This is the third and
   * last of `LL9`'s exits -- `PLANET` went in slice 3c, `DOEXP` is here -- and it is the routine
   * that makes a dying ship into a bloom of particles rather than a wireframe.
   *
   * THE CLOUD IS NOT AN ANIMATION OVER STORED PARTICLES. Every particle is generated fresh from
   * the random generator on every frame, and the reason the same cloud can be ERASED is that the
   * four generator seeds are reloaded from bytes 3 to 6 of the ship's line heap before each
   * vertex's particles are drawn. So the sequence repeats exactly, the same pixels are EORed a
   * second time, and the cloud disappears. Bytes 3 to 6 are what `EE55` randomised when the ship
   * was killed; nothing else in the game reads them.
   *
   * That is also why this cannot be approximated. A port that drew a visually similar cloud from
   * a different generator would draw over the first one instead of erasing it, and the screen
   * would fill with debris that never goes away.
   */

  /// 6502: the cloud counter `EE55` writes into byte 1 of the line heap, and the value `DOEXP`
  /// compares `frump` against to decide that this is the explosion's first frame.
  inline constexpr std::uint8_t EXPLOSION_CLOUD_START = 18;

  /// 6502: CPX #2*Y-1 -- a particle at or below this row is off the bottom of the space view.
  /// `SPACE_VIEW_BOTTOM` is the `2*Y` it is one less than.
  inline constexpr std::uint8_t EXPLOSION_PARTICLE_BOTTOM = SPACE_VIEW_BOTTOM - 1;

  /// 6502: CPY #2*Y+50 -- the sprite is allowed FIFTY ROWS FURTHER DOWN than a particle is,
  /// because it is placed by its top-left corner and the fifty is how far behind the dashboard
  /// the original is willing to let it start.
  inline constexpr std::uint8_t EXPLOSION_SPRITE_BOTTOM = SPACE_VIEW_BOTTOM + 50;

  /*
   * What `PTCLS2` reaches that is not memory (slice 4b-b).
   *
   * `PTCLS2` is `PTCLS` with an explosion SPRITE drawn at the centre of each vertex's cloud, and
   * the sprite is VIC-II registers rather than the bitmap -- so the particles are canvas writes
   * this port compares byte for byte, and the sprite is a seam. The split is the same one §6.73
   * made inside `SIGHT`, and for the same reason: filing the whole routine as hardware would throw
   * away the sixty instructions that are not.
   *
   * The register state this writes is write-only for now, exactly as `SightEffects` is. ADR-005 §1
   * closed on making it a `VideoState` the port owns (§6.133) and that work is not done; when it
   * is, this seam is one of the things it absorbs.
   */
  class ExplosionEffects
  {
  public:
    virtual ~ExplosionEffects() = default;

    /*
     * 6502: JSR SETL1 -- the 6510 input/output port register, which `PTCLS2` brackets itself with:
     * %101 to map the I/O page in so the VIC-II registers exist, %100 to map it back to RAM.
     *
     * The same routine and the same two values as `SightEffects::SetRasterMode`, which is why the
     * signature matches: a class implementing both interfaces overrides one method for both, and
     * that is the truth of it -- there is one `SETL1` in the game.
     */
    virtual void SetRasterMode(std::uint8_t _mode) = 0;

    /*
     * 6502: STA VIC+&17 / STA VIC+&1D -- the sprite y-expand and x-expand registers, written with
     * the SAME byte, so a sprite is either double size in both directions or neither.
     *
     * %11111101 clears bit 1 and %11111111 sets it, and bit 1 is sprite 1's. So a ship exploding
     * at z_hi of 7 or more gets a normal-sized burst and a closer one gets a double-sized burst;
     * every other sprite is left expanded, which is what the other six bits say.
     */
    virtual void SetSpriteExpansion(std::uint8_t _mask) = 0;

    /*
     * 6502: the five register writes that place sprite 1 and switch it on.
     *
     * `_x` is NINE BITS, because the C64's sprite x-coordinate is: the low eight go to VIC+&2 and
     * the ninth is bit 1 of VIC+&10, which the original sets with `LDA VIC+&10 / AND #%11111101 /
     * ORA exlook,X` -- a two-byte table holding %00 and %10 whose only job is to shift a 0 or 1
     * left by one place. Both that register and the enable register at VIC+&15 are READ-MODIFY-
     * WRITE, because the other seven sprites' bits live in them, so this is one method over the
     * pair rather than two setters the port would have to compose (the argument on `MaskSprites`).
     *
     * It is not called when the burst would be off the screen; `PTCLS2` skips it and draws the
     * particles anyway, so an off-screen burst leaves the sprite wherever it was.
     */
    virtual void ShowExplosionSprite(std::uint16_t _x, std::uint8_t _y) = 0;
  };

  /*
   * 6502: EXS1 -- (A X) = (A R) +/- random * cloud size, with the flags set for the high byte.
   *
   * The one piece of arithmetic in the explosion, and it is an "other entry point" in the upstream
   * source rather than private to `PTCLS`, so it is exported and compared on its own: three of the
   * routine's five borrowed flags are in these twenty instructions.
   *
   * Half the particles go one way from the vertex and half the other, and the choice is bit 7 of
   * the random byte AFTER the `ROL A` that doubles it -- which is to say bit 6 of what the
   * generator produced. That doubling is a ROTATE, not a shift: it takes the carry the generator
   * left in at bit 0, so the multiplier is nine bits wide from an eight-bit source.
   *
   * `_a` is the high byte of the coordinate and `R` the low; `Q` is the cloud size and `S` and `T`
   * come out holding what the routine left in them, because the caller's next call overwrites both
   * and a port that returned them instead would hide it.
   */
  struct ExplosionOffset
  {
    std::uint8_t high = 0; ///< 6502: A -- non-zero means the particle is off the screen
    std::uint8_t low = 0;  ///< 6502: X
  };

  [[nodiscard]] ExplosionOffset OffsetByCloud(MathWorkspace& _math, Rng& _rng, std::uint8_t _a) noexcept;

  /*
   * 6502: PTCLS -- draw one frame of the cloud, and PTCLS2 -- the same with the burst sprite.
   *
   * They are ONE BODY. Instruction for instruction the C64's `PTCLS2` is `PTCLS` with a prologue
   * that sizes the sprite, an insert inside the vertex loop that places it, and a `SETL1` pair
   * around the whole thing; every other instruction, branch and flag is identical. So they are one
   * implementation and a flag here, rather than sixty duplicated lines that could drift apart --
   * and both are exported because both are entry points the shipped game reaches, `PTCLS` by
   * falling through and `PTCLS2` through `PTCLS2S`.
   *
   * `_work` is `INWK` and is read, not written: the routine reads the heap pointer out of it, and
   * `PTCLS2` reads z_hi to size the sprite.
   *
   * WHAT THEY LEAVE IN THE GENERATOR IS PART OF THE ANSWER. `RAND+1` is pushed on entry and pulled
   * back on the way out, `RAND+3` is overwritten with the PLANET's z_lo, and `RAND` and `RAND+2`
   * are left wherever the last particle put them. Three different fates for four bytes, and the
   * next `DORND` anywhere in the game runs on the result.
   */
  void DrawExplosionParticles(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Rng& _rng, const ShipBlock& _work,
                              LineHeap& _heap, const Bubble& _bubble) noexcept;

  void DrawExplosionParticlesWithSprite(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Rng& _rng, const ShipBlock& _work,
                                        LineHeap& _heap, const Bubble& _bubble, ExplosionEffects& _effects) noexcept;

  /*
   * 6502: DOEXP (with EX2, EXL1 and TT48) -- age the cloud by one frame and draw it.
   *
   * The shape is: rub out the last frame if there was one, work out how big this frame's cloud is,
   * copy the ship's visible vertices onto its line heap as the cloud's origins, and draw. Byte 1
   * of the heap is the counter that ages, byte 0 is the size it produces, and byte 2 is how many
   * vertices to bloom from.
   *
   * THE COUNTER GROWS BY FOUR OR BY FIVE, AND WHICH IT IS DEPENDS ON DISTANCE. `ADC #4` has no
   * `CLC` in front of it and the carry reaching it is the `CMP #32` that asked whether the ship
   * was far away: a ship at z_hi of 32 or more leaves it SET and ages at five a frame, a nearer one
   * clears it through the `ROL A` that scales the distance and ages at four. Sixty frames of
   * explosion or forty-eight. The upstream comment says only "add 4".
   *
   * `EX2` is the end of the explosion, reached when that addition overflows: bits 5 and 7 of byte
   * 31 go on, and `MVEIT` takes the ship out of the bubble on the next pass.
   *
   * The C64 draws the first frame -- and only the first, `CPY #18` against the counter BEFORE it
   * grew -- through `PTCLS2`, so the burst sprite appears once and is never moved again.
   */
  void DrawExplosionCloud(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Rng& _rng, ShipBlock& _work, LineHeap& _heap,
                          const GeometryWorkspace& _geometry, const Bubble& _bubble, ExplosionEffects& _effects) noexcept;

} // namespace Elite
