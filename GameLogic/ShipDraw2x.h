#pragma once

#include "HeapOffset.h"
#include "LineHeap.h"
#include "Picture.h"

#include <array>
#include <cstdint>

namespace Elite
{

  /*
   * The 640x400 picture's lines (Design/Resolution.md section 4.1, slice RS-2).
   *
   * THIS IS WHERE THE RESOLUTION IS ACTUALLY SPENT. Everything before it doubled a picture the game
   * had already drawn; this draws a second one, from the same decisions, with one more bit of
   * precision in every coordinate -- and one bit is all there is, honestly. Section 1's table says
   * why: every projecting divide in Elite truncates to a pixel, so a twin can recover the bit the
   * truncation dropped and nothing beyond it. What that buys is lines half as thick and placed
   * twice as finely, which on a wireframe is most of what the eye reads.
   *
   * RULE T2 IS THE WHOLE DESIGN OF THIS FILE: the precision comes from running the SAME divide on
   * the SAME operands at twice the scale, never from interpolating between the faithful routine's
   * answers. `Divide512` is `LL28` at 512 rather than 256; halved, it is `LL28` again, and a sweep
   * asserts exactly that. Nothing here smooths, averages or subdivides.
   *
   * AND RULE T1 IS WHY THERE IS NO CLIPPING DECISION HERE. `LL145` decides whether a line is drawn
   * at all and `PushEdges` decides whether it reaches the heap; by the time a twin sees a line, the
   * game has already said it is visible. `ClipLine2x` therefore CUTS a line it has been told to
   * draw, and is a plain Cohen-Sutherland rather than a port of `LL145` -- the two do different
   * jobs, and a port of the clipper would be a second decision that could disagree with the first.
   */

  /*
   * A line in the wide SPACE VIEW's coordinates: x 0..511, y 0..287, centred on (256, 144).
   *
   * VIEW COORDINATES AND NOT SURFACE ONES, which is the same distinction that caught RS-1 between
   * `XC` and a canvas cell, and it is worth stating in the same words. The game's line drawing works
   * in the view's own 256x144 space and `ylookup` adds the four-cell margin on the way to the
   * bitmap; everything here works in the doubled 512x288 space and `Bresenham2x` adds the doubled
   * margin at the moment it plots. So a twin's arithmetic is the faithful arithmetic with the same
   * origin, and only the plot knows where the view sits on the surface.
   *
   * Sixteen bits an axis because a projected vertex is off the screen far more often than on it.
   */
  struct Line2x
  {
    std::int16_t x1 = 0;
    std::int16_t y1 = 0;
    std::int16_t x2 = 0;
    std::int16_t y2 = 0;

    /*
     * Whether there is a line here at all, and it defaults to NOTHING.
     *
     * A flag rather than a coordinate sentinel because (0, 0) is a real place on the wide view, and
     * a rejected cut writing four zeros would put a dot in the top-left corner of the space view --
     * ink the canvas has nowhere, which is the second clause of §8.1's property. The case it marks
     * is rare and real: `LL145` accepts an edge and the wide line, half a pixel away, falls outside.
     */
    bool drawn = false;

    [[nodiscard]] constexpr bool operator==(const Line2x&) const noexcept = default;
  };

  /*
   * The twin of the ship line heap, and the reason it exists is rule T3.
   *
   * The game erases a ship by drawing its lines a second time, and the lines it draws are the ones
   * on its heap -- so a surface with no record of what it drew cannot rub anything out. This is that
   * record for the wide surface: the same lines, at twice the resolution, addressed by THE SAME
   * `HeapOffset` as the bytes they twin.
   *
   * ONE ENTRY PER ARENA BYTE, which wastes three quarters of it and is worth every byte. A line
   * occupies four bytes of the faithful heap starting at some offset, and indexing this by that
   * offset means `KILLSHP` shuffling a run and `NWSHP` carving one need no second arithmetic here:
   * the offset IS the index. The arena is 1,728 bytes, so this is 13,824 -- smaller than the
   * mistake a cleverer packing would eventually cause.
   */
  class LineHeap2x
  {
  public:
    static constexpr std::size_t SIZE = LineHeap::SIZE;

    [[nodiscard]] Line2x Read(HeapOffset _at) const noexcept
    {
      return (_at.up < SIZE) ? m_lines[_at.up] : Line2x{};
    }

    void Write(HeapOffset _at, Line2x _line) noexcept
    {
      if (_at.up < SIZE)
      {
        m_lines[_at.up] = _line;
      }
    }

  private:
    std::array<Line2x, SIZE> m_lines{};
  };

  /*
   * The wide view's centre and extent, which are the canvas's doubled: `SPACE_VIEW_CENTRE_X`,
   * `SPACE_VIEW_CENTRE_Y` and the 256x144 they are the middle of.
   */
  inline constexpr int SPACE_VIEW_CENTRE_X_2X = 256;
  inline constexpr int SPACE_VIEW_CENTRE_Y_2X = 144;
  inline constexpr int SPACE_VIEW_WIDTH_2X = 512;

  /// The last row the clipper counts as on screen, doubled: `LL145`'s 143, which is `Y * 2 - 1`.
  inline constexpr int SPACE_VIEW_BOTTOM_2X = 287;

  /// And the same while `dontclip` is set, which is the short-range chart using the whole screen:
  /// the game's 199 doubled, which is the wide surface's last row.
  inline constexpr int WHOLE_SCREEN_BOTTOM_2X = Picture::HEIGHT - 1;

  static_assert(SPACE_VIEW_WIDTH_2X == Picture::WIDTH - 2 * Picture::SPACE_VIEW_MARGIN, "the view is what the margins leave");
  static_assert(SPACE_VIEW_BOTTOM_2X == Picture::SPACE_VIEW_HEIGHT - 1, "and it ends where the dashboard starts");

  /*
   * A vertex of `XX3` as the signed sixteen-bit number it is, DOUBLED.
   *
   * THIS IS WHERE THE DESIGN WAS WRONG AND THE BUILDING CORRECTED IT (Resolution.md section 1's
   * table, section 4.1, and the journal). The design had the ship's wireframe gaining a bit of
   * precision from a twin projection at scale 512 -- `LL28` truncates a quotient, the reasoning
   * went, so running the same divide at twice the scale recovers the bit it dropped.
   *
   * `LL28` DOES NOT TRUNCATE A QUOTIENT. It is a LOGARITHM-TABLE lookup, and measured against exact
   * division over all 32,640 pairs it is out by up to 3, by 0.70 on average, and by more than one in
   * thirteen percent of them. So there is no dropped bit to recover: a twin that divided exactly at
   * 512 would put vertices up to three canvas pixels from where the game puts them, which is a
   * DIFFERENT wireframe rather than a finer one, and rule T2 forbids exactly that.
   *
   * So the wide vertex is the faithful vertex doubled, and the ship gains THINNESS and not
   * placement: one wide pixel of line where the canvas has a two-pixel-wide one, cut to the wide
   * view at the finer boundary. Where a real bit does exist -- the stardust's `SXL`/`SYL` fractions,
   * the scanner's `x_lo` -- it is a byte the faithful plot throws away rather than a table's
   * resolution, and those twins do recover it.
   */
  [[nodiscard]] constexpr std::int16_t Doubled(std::uint8_t _low, std::uint8_t _high) noexcept
  {
    return static_cast<std::int16_t>(2 * static_cast<std::int16_t>(static_cast<std::uint16_t>(_low) |
                                                                  (static_cast<std::uint16_t>(_high) << 8)));
  }

  /*
   * Cut a line to the wide space view, Cohen-Sutherland, integer throughout.
   *
   * `_bottomRow` is the last row that counts as on screen, which the game keeps in `Yx2M1` and
   * moves: 143 in flight and 199 while the short-range chart is up. It arrives doubled, because
   * everything here is in the wide surface's own coordinates.
   *
   * Returns false when nothing of the line is inside, which the faithful clipper having accepted it
   * makes very nearly impossible -- and "very nearly" is why the case is handled rather than
   * asserted: the 2x line's ends are half a pixel from the faithful line's, so a line that ended
   * exactly on a boundary can fall the other side of it. That is the one pixel of slack section 8.1
   * allows, and the only place the two surfaces are permitted to disagree.
   */
  [[nodiscard]] bool ClipLine2x(Line2x _line, int _bottomRow, Line2x& _outClipped) noexcept;

  /// 2x of: DrawLine -- one line, one pixel wide, exclusive-ored so that drawing it again erases it.
  /*
   * NOT a port of `LOIN`, and the difference is the point. `LOIN`'s two loops plot one BIT at a time
   * through a mask table so that a line alternates between a multicolour cell's two colours; the
   * wide surface is a plane of bits in standard mode, where that alternation is what a bit plane
   * does for free. So this is ordinary Bresenham, which is the same line without the table.
   */
  /// `_line` is in VIEW coordinates; the doubled left margin is added here and nowhere else.
  void Bresenham2x(Picture& _picture, Line2x _line) noexcept;

  /// 2x of: PushHeapLine -- record a line where the faithful heap recorded its four bytes. A line
  /// whose `drawn` is false records that there is nothing to draw there, which is not the same as
  /// leaving the entry alone: the entry may hold a previous ship's line.
  void PushHeapLine2x(LineHeap2x& _heap, HeapOffset _at, Line2x _line) noexcept;

  /*
   * 2x of: DrawShipLines -- draw every line of a run, from the wide heap.
   *
   * THE FAITHFUL HEAP IS STILL WHAT SAYS HOW MANY. Byte 0 of the run is the length and the walk is
   * `LL155`'s own, four bytes at a time; this reads that byte and steps that walk, so a run the
   * game considers empty draws nothing here either. Rule T1: the faithful routine decides.
   */
  void DrawShipLines2x(Picture& _picture, const LineHeap& _heap, const LineHeap2x& _wide, HeapOffset _run) noexcept;

} // namespace Elite
