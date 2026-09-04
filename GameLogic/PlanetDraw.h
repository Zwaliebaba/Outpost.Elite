#pragma once

#include "Arith.h"
#include "Canvas.h"
#include "ShipDraw.h"

#include <array>
#include <cstdint>

namespace Elite
{

/*
 * The planet and sun line heaps (slice 3c).
 *
 * The ships have one heap between them and the planet and the sun have TWO MORE, of different
 * shapes, for the same reason: everything is drawn by EOR, so erasing last frame's picture means
 * knowing what last frame's picture was. What differs is what "a picture" is.
 *
 * The SUN is a stack of horizontal lines, one per screen row, and all that has to be remembered
 * is each row's HALF-WIDTH -- the centre comes from `SUNX`, which is one value for the whole
 * disc. So the sun's heap is 200 bytes indexed by row, and `WPLS` walks it drawing each row's
 * line again to rub it out.
 *
 * The PLANET is a circle drawn as a run of straight segments, so what has to be remembered is
 * every segment's endpoints. That is `LSX2` and `LSY2`, and `WPLS2` walks them calling `LOIN`.
 *
 * The sizes are not estimates. `LSO` is at 1408 and the next label, `BUF`, at 1608 -- 200 bytes,
 * which is the screen's height. `LSX2` is at 9892, `LSY2` at 10148 and `UNIV` at 10404 -- 256
 * bytes each, and ADJACENT, which is not incidental: `BLINE` reads `LSY2-1,Y`, so with `LSP` at
 * zero it reads the last byte of `LSX2`. The two are one 512-byte block here for that reason,
 * the same way `XX3` is 260 bytes because its 256th is the stack page.
 */
inline constexpr std::size_t SUN_HEAP_SIZE = 200;  ///< 6502: LSO, 1408 to 1607
inline constexpr std::size_t BALL_HEAP_SIZE = 256; ///< 6502: LSX2, and LSY2 immediately after it

struct PlanetSunState
{
  /*
   * 6502: LSO -- one half-width per screen row, and `LSX` is THE SAME ADDRESS.
   *
   * Entry 0 is not a row: it is the flag that says whether there is a sun to rub out at all,
   * which `FLFLLS` sets to 255 and `WPLS` sets back to 255 when it has finished. Two names for
   * one byte because the byte does two jobs, and `LSO,Y` is only ever indexed from 1.
   */
  std::array<std::uint8_t, SUN_HEAP_SIZE> sun{};

  /// 6502: LSX2 then LSY2 -- one block, because `BLINE` indexes across the join.
  std::array<std::uint8_t, 2 * BALL_HEAP_SIZE> ball{};

  /// 6502: LSP -- how far up the ball heap the last circle got.
  std::uint8_t lsp = 0;

  /// 6502: SUNX(1 0) -- where the sun's centre is. One value for every row of it.
  std::uint8_t sunX = 0;
  std::uint8_t sunXNext = 0;

  /*
   * 6502: Yx2M1 -- the bottom row that counts as on-screen, and it is a VARIABLE.
   *
   * The upstream header for `CHKON` documents `CPX #2*Y-1`; this build assembles `CPX Yx2M1`, a
   * byte at 184 that `TT23` sets to 199 so the short-range chart can use the whole screen and
   * `TT23`'s own tail and `RES2` set back to 143. It moves in lockstep with `dontclip` -- the
   * same two-instruction pairs write both -- so it is the second byte of the view-extent state
   * §6.38 found, and whichever slice makes `TT23` write one must write both (§6.45).
   *
   * Only four routines read it: `CHKON`, and `SUN` parts 1 and 2. `WPLS` uses the LITERAL 143 in
   * the same build, which is why it cannot be folded into a single constant.
   */
  std::uint8_t yx2M1 = 0;

  /// 6502: LSX2,Y and LSY2,Y -- named because the second is the first plus 256.
  [[nodiscard]] std::uint8_t BallX(std::uint8_t _at) const noexcept { return ball[_at]; }
  [[nodiscard]] std::uint8_t BallY(std::uint8_t _at) const noexcept { return ball[BALL_HEAP_SIZE + _at]; }
  void SetBallX(std::uint8_t _at, std::uint8_t _value) noexcept { ball[_at] = _value; }
  void SetBallY(std::uint8_t _at, std::uint8_t _value) noexcept { ball[BALL_HEAP_SIZE + _at] = _value; }

  /// 6502: LSY2-1,Y -- which for Y = 0 is the last byte of `LSX2`, and that is reachable.
  [[nodiscard]] std::uint8_t BallYBefore(std::uint8_t _at) const noexcept
  {
    return ball[BALL_HEAP_SIZE + _at - 1u];
  }
};

/*
 * 6502: EDGES -- where does a horizontal line of half-width A, centred on YY(1 0), start and end?
 *
 * Returns the carry: SET means the line is entirely off one side and the row has been cleared;
 * CLEAR means `X1` and `X2` are its ends, clamped to the screen. It is the sun's clipper, and it
 * is a different routine from `LL145` because a horizontal line needs no slope.
 */
[[nodiscard]] bool ClipSunRow(PlanetSunState& _state, MathWorkspace& _math, DrawWorkspace& _draw,
                              std::uint8_t _a, std::uint8_t _row) noexcept;

/*
 * 6502: HLOIN2 -- clip the row, forget it, and draw it.
 *
 * It IGNORES what `EDGES` returned. Both of its callers only reach it for a row the heap says
 * has a line on it, so the off-screen exit is not a case they can produce -- but the routine as
 * written would draw whatever `X1` and `X2` happened to hold, and the port reproduces that rather
 * than adding the branch the game does not have (ADR-003).
 */
void EraseSunRow(Canvas& _canvas, PlanetSunState& _state, MathWorkspace& _math,
                 DrawWorkspace& _draw, std::uint8_t _a, std::uint8_t _row) noexcept;

/// 6502: FLFLLS -- forget the whole sun. Rows 1 to 199 are zeroed and entry 0 becomes 255.
void ClearSunHeap(PlanetSunState& _state) noexcept;

/// 6502: WP1 -- and the whole ball. `LSP` goes to 1 rather than 0, which is what makes
/// `LSY2-1,Y` read entry 0 on the first pass rather than the byte before the array.
void ClearBallHeap(PlanetSunState& _state) noexcept;

/*
 * 6502: WPLS -- rub the sun out, one row at a time, from row 143 upwards.
 *
 * The 143 is the literal `2*Y-1` and not `Yx2M1`, in the same build where `CHKON` reads the
 * variable. Reproduced as written.
 */
void EraseSun(Canvas& _canvas, PlanetSunState& _state, MathWorkspace& _math,
              DrawWorkspace& _draw) noexcept;

/*
 * 6502: WPLS2 -- rub the planet out, one segment at a time.
 *
 * The heap is a run of coordinate pairs with 255 as a break: a break means the next pair is a new
 * run's START rather than another segment's end. `BLINE` writes those breaks when a segment is
 * clipped away, which is how a circle half off the screen comes back as several polylines.
 */
void EraseBall(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw) noexcept;

/// 6502: PL2 -- rub out whichever of the two this is. `TYPE` is 128 for the planet and 129 for
/// the sun, and the routine tells them apart with an `LSR` rather than a comparison.
void ErasePlanetOrSun(Canvas& _canvas, PlanetSunState& _state, MathWorkspace& _math,
                      DrawWorkspace& _draw, std::uint8_t _type) noexcept;

/*
 * 6502: CHKON -- is a circle of radius K at (K3, K4) worth drawing?
 *
 * Returns the carry: SET means no part of it is on screen. It also leaves the top and bottom of
 * the circle in P+1 and P+2, which `CIRCLE`'s caller reads, so the answer is three values and not
 * one flag.
 *
 * Its `BMI PL44` branches into the tail of `PLS6` -- ported with slice 3b -- and not into the
 * `EDGES` sitting next to it in the source, which also defines a `PL44` behind an `IF` this
 * build does not take. Both are `CLC / RTS`, so a port that picked the wrong one would be right
 * by luck (§6.45).
 */
[[nodiscard]] bool CircleOffScreen(const PlanetSunState& _state, MathWorkspace& _math,
                                   const Projection& _centre) noexcept;

} // namespace Elite
