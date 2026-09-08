#pragma once

#include "Canvas.h"
#include "Colours.h"
#include "Picture.h"
#include "ShipSlot.h"
#include "ShipType.h"

#include <cstdint>

namespace Elite
{

  /*
   * The 640x400 picture's dashboard (Design/Resolution.md section 5, slice RS-4).
   *
   * THE LOWER REGION IS AN INDEX PLANE AND NOT A BIT PLANE, which is the one structural difference
   * between this file and the three twin files above it. A scanner cell holds a red blip, a yellow
   * one and the ellipse's own colour at once; two colours a cell cannot express, which is why the
   * original puts the dashboard in multicolour mode and why the dashboard is chunky. An index plane
   * is the 2x answer to the same constraint (section 3.2), and everything here writes indices.
   *
   * SO A TWIN HAS TO KNOW WHAT COLOUR A PATTERN IS, and it is not the pattern's business. `COL`
   * holds four two-bit codes and the codes select from the CELL: %00 the background register, %01
   * the high nibble of screen RAM, %10 the low nibble, %11 colour RAM (`Colours.h`). So every twin
   * here takes the canvas as well as the picture, and resolves the pattern through the same cell
   * the faithful store lands in -- `PatternIndex2x` below. The canvas decides the colours; the twin
   * computes where they go, which is rule T1 in the one place it might have been tempting to break.
   *
   * WHAT "TWICE THE DETAIL" IS WORTH HERE, measured rather than assumed (ruling 1, and section 13):
   *
   *   - The BARS gain a real bit. `DIL` draws `value >> shifts` of sixteen steps and the twin draws
   *     `value >> (shifts - 1)` of thirty-two, from the same byte -- so the speed, the shields, the
   *     fuel and the two temperatures are all twice as finely placed. The four ENERGY bars are the
   *     exception and are named as one: `DILX` is entered with no shift at all and the value is
   *     already 0..16, so there is no bit under it and the twin doubles.
   *   - The ROLL indicator gains one and the PITCH does not. `DIALS` builds the roll from
   *     `alp1 >> 2` -- two bits thrown away -- so `alp1 >> 1` is one of them back. The pitch's
   *     `beta` is `bet1` with its sign on, and `bet1` was shifted down in the FLIGHT LOOP four
   *     instructions from the joystick; the bit is gone before the dial sees it, and recovering it
   *     would mean recomputing game state rather than rendering it.
   *   - The SCANNER gains two bits across and one down. `SCAN` reads `x_hi`, `z_hi >> 2` and
   *     `y_hi >> 1` and never touches `x_lo`, `z_lo` or `y_lo`, which are bytes the game maintains
   *     on every ship in the bubble.
   *   - The COMPASS gains NOTHING, and that is the one place the building declined a bit it had.
   *     `DVID4`'s whole part is exact division -- swept over all 65,280 pairs -- so the twin could
   *     honestly divide at twice the radius. It may not, because `COMPAS` erases last frame's dot by
   *     drawing it again out of `COMX` and `COMY`: a finer position is state the game does not keep,
   *     and keeping it beside those two bytes is the parallel-record shape RS-3 spent a slice
   *     deleting. See `DrawCompassDot2x`.
   */

  /*
   * The colour index one multicolour pixel of the dashboard takes for one pattern.
   *
   * `_canvasX` and `_canvasY` are a pixel on the CANVAS, 0..319 by 144..199; which of the four
   * choices the pattern selects depends on where in the byte the pixel falls, because `Striped` --
   * the Thargoid's -- is %01 %01 %10 %10 and is the one entry of `scacol` that is not four of the
   * same code.
   */
  [[nodiscard]] std::uint8_t PatternIndex2x(const Canvas& _canvas, int _canvasX, int _canvasY, PixelPattern _pattern) noexcept;

  /*
   * 2x of: Canvas::ResolveCell -- one dashboard cell of the canvas, decoded and doubled into the
   * plane.
   *
   * THIS IS THE BOOTSTRAP AND ALSO THE PALETTE TWIN, and it is one function for a reason worth
   * stating. Section 5.3 asked for `DASHBOARD_IMAGE_2X`, a 71,680-byte generated table holding the
   * faithful picture upscaled, plus a tool command to produce it. **CORRECTED at RS-4**: a
   * generated table that is a pure function of a table already in the tree is a copy, and this
   * repository has spent three slices removing copies that two people have to keep in step. The
   * bootstrap is computed where it is needed instead, from `DASHBOARD_IMAGE` through the canvas's
   * own decode -- the same pixels, no second file, and `WritePicturePng` already hands the owner a
   * PNG to paint over. RS-4-art replaces this call with an imported table; nothing else changes.
   *
   * It is ALSO what a palette change twins to. `MSBAR` writes a screen-RAM byte and the bulbs
   * exclusive-or one, and what that does on the hardware is recolour whatever bits are already in
   * the cell -- so the honest twin is to decode the cell again and write what comes out. The cost
   * is named: a blip the scanner twin drew finely inside such a cell is flattened back to the
   * canvas's resolution until the next `SCAN` redraws it, which is one frame.
   */
  void ResolveDashboardCell2x(Picture& _picture, const Canvas& _canvas, int _cellColumn, int _cellRow) noexcept;

  /// The whole dashboard, cell by cell: `wantdials`' picture copy, doubled into the plane.
  void CopyDashboardPicture2x(Picture& _picture, const Canvas& _canvas) noexcept;

  /*
   * 2x of: DrawBar -- one dial, at thirty-two steps of two hi-res pixels.
   *
   * `_sc` is the screen pointer as the faithful routine received it, which names the leftmost of
   * the four character cells and the row within it; `_steps` is how many of the thirty-two are lit,
   * which the caller computes because the shift count is `DILX`'s entry point and not this
   * routine's; `_ink` is the pattern `DIL` chose, because the danger flash is `PZW`'s decision (T1).
   *
   * It STORES rather than exclusive-ors, as `DIL` does, so the trough is blanked by the same pass
   * that fills the bar and no dial needs an erase.
   */
  void DrawBar2x(Picture& _picture, const Canvas& _canvas, std::uint16_t _screenPointer, int _steps, PixelPattern _ink) noexcept;

  /// 2x of: DrawIndicator -- the roll and pitch markers, one lit block of thirty-two. `_position`
  /// is the wide slot, which the caller derives: the roll from `alp1 >> 1` and the pitch by
  /// doubling, for the reason in the header.
  void DrawIndicator2x(Picture& _picture, const Canvas& _canvas, std::uint16_t _screenPointer, int _position) noexcept;

  /// 2x of: SetMissileIndicator -- the cell `MSBAR` recoloured, decoded again with its new palette.
  void SetMissileIndicator2x(Picture& _picture, const Canvas& _canvas, std::uint8_t _missile) noexcept;

  /// 2x of: ToggleEcmIndicator -- the two cells a bulb owns, decoded again after the exclusive-or.
  /// `_cell` is the canvas offset of the upper one, in screen RAM, as the faithful routine has it.
  void ToggleBulb2x(Picture& _picture, const Canvas& _canvas, std::uint16_t _cell) noexcept;

  /*
   * 2x of: DrawScannerBlip -- the dot and its stick, exclusive-ored so that the second call erases
   * them (rule T3).
   *
   * `_across` and `_row` are `X1` and `Y1` as `CPIX4` receives them, and `_stick` is the signed
   * height `SCAN` computed: all three are the faithful routine's answers, so which ships appear and
   * where they sit on the ellipse stays its decision. What the twin adds is `_acrossBit`: the top
   * bit of `x_lo`, signed the way the magnitude is, which is half a canvas pixel and a quarter of
   * the fat pixel the faithful dot snaps to.
   */
  void DrawScannerBlip2x(Picture& _picture, const Canvas& _canvas, std::uint8_t _across, int _acrossBit, std::uint8_t _row,
                         std::uint8_t _stickHeight, bool _stickUp, PixelPattern _pattern) noexcept;

  /*
   * 2x of: DrawCompassDot -- a 4x4 block for a target ahead and a 4x2 dash for one behind, which is
   * `DOT`'s branch on the colour.
   *
   * IT DOUBLES `COMX` AND `COMY` AND RECOVERS NOTHING, which is a correction to ruling 1's table
   * (§13). The bit is there and `DVID4`'s whole part is exact -- swept over all 65,280 pairs -- so
   * the twin COULD divide at twice the radius. It may not, because `COMPAS` erases last frame's dot
   * by drawing it again out of `COMX` and `COMY`, and those two bytes are the game's: a finer
   * position would have to be remembered beside them, unhashed, and kept in step by hand. RS-3
   * spent a slice deleting exactly that shape of parallel state, and what it would buy here is half
   * a dot's width on a disc twenty canvas pixels across.
   */
  void DrawCompassDot2x(Picture& _picture, const Canvas& _canvas, std::uint8_t _across, std::uint8_t _row,
                        PixelPattern _pattern) noexcept;

  /*
   * A sign-magnitude byte with its seven-bit magnitude doubled.
   *
   * What an indicator's value becomes when the dial has twice as many slots and the byte under it
   * has no more bits to give -- the pitch's case. The roll does not use it: `alp1 >> 1` is a real
   * bit rather than a doubling, and the difference between the two is the header's third bullet.
   */
  [[nodiscard]] constexpr std::uint8_t DoubledMagnitude(std::uint8_t _value) noexcept
  {
    const std::uint8_t magnitude = static_cast<std::uint8_t>(_value & 0x7Fu);
    const std::uint8_t doubled = (magnitude < 64u) ? static_cast<std::uint8_t>(magnitude * 2u) : std::uint8_t{127};
    return static_cast<std::uint8_t>((_value & 0x80u) | doubled);
  }

} // namespace Elite
