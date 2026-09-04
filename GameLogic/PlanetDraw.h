#pragma once

#include "Arith.h"
#include "Canvas.h"
#include "ShipDraw.h"
#include "Rng.h"
#include "ShipSlot.h"

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

  /*
   * 6502: PLTOG -- whether the planet gets its detail drawn, and it has ONE reader and NO WRITER.
   *
   * `PL9` reads it and nothing in the whole C64 build stores to it. Other versions toggle it from
   * a key; this one takes whatever the loader left in the byte at 7439 and keeps it for the life
   * of the game. So it is initialised from the image rather than defaulted here, and a port that
   * assumed "off" would draw featureless planets for ever.
   */
  std::uint8_t pltog = 0;

  /*
   * 6502: V(1 0) -- 91 and 92, and the sun uses them as a COUNTER PAIR.
   *
   * Eleven files write these two bytes and they do not agree about what they are. `DETOK`,
   * `TACTICS`, `TAS1`, `EX` and `LL9` part 5 use them as a POINTER, read through `LDA (V),Y`;
   * `SUN` uses them as a signed distance from the sun's centre that it walks with `DEC V` and
   * `INX / STX V`. One storage, two meanings.
   *
   * The port keeps them apart -- `GeometryWorkspace::v` is a `std::uint16_t` because the
   * blueprints are an address-indexed region (§6.32), and making it a byte pair to share with a
   * counter would make `LL9` worse to read for no gain. That is §6.49's expensive branch, so
   * here is the measurement it requires: **every one of the eleven users initialises the pair
   * before reading it**, none passes it to another, and no two are live at once. A divergence
   * cannot be observed.
   */
  std::uint8_t v = 0;
  std::uint8_t vNext = 0;

  /*
   * 6502: K5, K6, STP and FLAG -- the ball's walk, and they are the planet's state rather than
   * the arithmetic's.
   *
   * `K5` is the segment's start and `K6` its end, four bytes each because both coordinates are
   * sixteen bits: a circle whose centre is off the screen still has an arc on it. `BLINE` copies
   * `K6` into `K5` on the way out, so each segment starts where the last one ended.
   *
   * `STP` is the step in sixty-fourths of a turn -- 8, 4 or 2, chosen by `CIRCLE` from the
   * radius, so a small planet is a coarser polygon and nobody can tell. `FLAG` is 255 for the
   * first segment of a circle, which is the one that has a start but no end yet.
   *
   * `STP` and `LSP` are also written by `TT128`, the short-range chart's range circle, which is
   * slice 2 and already ported: the third outward pointer this slice has found, after `dontclip`
   * and `Yx2M1`.
   */
  std::array<std::uint8_t, 4> k5{};
  std::array<std::uint8_t, 4> k6{};
  std::uint8_t stp = 0;
  std::uint8_t flag = 0;

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

/*
 * 6502: BLINE -- one segment of a circle: clip it, remember it, draw it.
 *
 * The routine is a straight line with three branches into one tail, not a loop. What makes it
 * interesting is the HEAP FORMAT it maintains, which is the format `WPLS2` walks: a run of
 * endpoints with 255 as a break, and a break means the pair after it is a new run's start. A
 * segment that clips away entirely, or that comes back with an end moved, ends the run -- so a
 * circle crossing the screen edge is stored as several polylines and erased as several.
 *
 * It takes X and the carry because both are operands: `TXA / ADC K4` is the first instruction,
 * and `PLS22` reaches it with a carry `CIRCLE2` never produces.
 *
 * It returns the new `CNT`, which is what both callers loop on.
 */
[[nodiscard]] std::uint8_t DrawBallLine(Canvas& _canvas, PlanetSunState& _state,
                                        DrawWorkspace& _draw, GeometryWorkspace& _geometry,
                                        MathWorkspace& _math, ClipState& _clip,
                                        const Projection& _centre, std::uint8_t _x,
                                        bool _carryIn) noexcept;

/*
 * 6502: CIRCLE2 -- walk a whole circle, sixty-four steps at most, `STP` at a time.
 *
 * The two coordinates come from the same sine table a quarter-turn apart, which is how one table
 * gives both, and each is negated for the half of the turn where it points the other way. The
 * negation is what the `CMP #33` tests are for: 33 rather than 32 because the compare is against
 * a count that has already been advanced.
 */
void DrawBall(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
              GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
              const Projection& _centre, bool _carryIn) noexcept;

/*
 * 6502: CIRCLE -- is it worth drawing, how coarse should it be, and then draw it.
 *
 * Returns the carry: set means `CHKON` refused it and nothing was drawn. The step is 8 for a
 * radius under 8, 4 under 60 and 2 above -- so a planet gets 32 segments and a distant one gets
 * 8, and the `LSR A` pair that chooses is two instructions rather than a table.
 */
[[nodiscard]] bool DrawCircle(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                              GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                              const Projection& _centre) noexcept;

/*
 * 6502: PLS1 -- one axis of the planet's position, divided by its distance.
 *
 * Three values come back and the original returns them in three places: A is the magnitude
 * (saturated at 254 when it will not fit in a byte), Y is the sign from `K+3`, and X has been
 * STEPPED ON BY TWO so the caller can walk the axes without counting.
 */
struct AxisResult
{
  std::uint8_t value = 0; ///< 6502: A
  std::uint8_t sign = 0;  ///< 6502: Y
  std::uint8_t at = 0;    ///< 6502: X, after its two INXs
};

[[nodiscard]] AxisResult DivideAxisByZ(const ShipBlock& _ship, MathWorkspace& _math,
                                       std::uint8_t _at) noexcept;

/// 6502: PLS3 -- the same, scaled by 222/256, and returned as a signed sixteen-bit value with the
/// high half in Y. `X` is preserved here rather than stepped, through `U`.
[[nodiscard]] AxisResult ScaleAxisByZ(const ShipBlock& _ship, MathWorkspace& _math,
                                      std::uint8_t _at) noexcept;

/// 6502: PLS4 -- where a meridian starts, as an angle: `ARCTAN` of the ratio, flipped by the
/// roof vector's sign, and divided by four to index a sixty-fourth of a turn.
void SetMeridianAngle(const ShipBlock& _ship, MathWorkspace& _math, std::uint8_t _a) noexcept;

/// 6502: PLS5 -- two axes into `K2+2`/`K2+3` and their signs into `XX16+2`/`XX16+3`.
void LoadTwoAxes(const ShipBlock& _ship, MathWorkspace& _math, GeometryWorkspace& _geometry,
                 std::uint8_t _at) noexcept;

/*
 * 6502: PLS22, and PLS2 which is the two instructions above it.
 *
 * An ellipse rather than a circle: the two axes are `K2(1 0)` and `K2(3 2)` with their signs in
 * `XX16`, so a meridian drawn edge-on is a line and one drawn face-on is a circle, and every
 * angle between is the same code. It walks `CNT2` by `STP` and stops when `BLINE` reports it has
 * passed `TGT` -- 31 for a meridian and 64 for a crater, which is why one is a half-turn and the
 * other a whole one.
 */
void DrawEllipse(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                 GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                 const Projection& _centre) noexcept;

void DrawHalfEllipse(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                     GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                     const Projection& _centre) noexcept;

/*
 * 6502: PL9, in its three parts -- the planet's outline and then its markings.
 *
 * Elite's planets have two looks and the game picks between them with one bit of the system's
 * tech level: `SOS1` spawns the planet as `128 OR (tek AND 2)`, so type 128 gets MERIDIANS -- two
 * great circles seen at whatever angle the planet is turned to -- and type 130 gets a CRATER,
 * which is one small ellipse offset from the centre.
 *
 * Neither is drawn at all unless `PLTOG` says so, and the crater is skipped for a planet turned
 * away from you (`INWK+20` negative, which is the nose vector pointing off).
 */
void DrawPlanetDetail(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                      GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                      const ShipBlock& _ship, Projection& _centre, std::uint8_t _type) noexcept;

/*
 * 6502: PLANET -- the entry the main loop calls for both the planet and the sun.
 *
 * It rejects the object before projecting it: a distance sign byte of 48 or more is too far to
 * see, and a zero distance is the case the divide cannot take. Then `PROJ`, then the radius --
 * `96 * 256 / z`, saturated at 248 -- and then the bottom bit of `TYPE` chooses which of the two
 * things it is.
 *
 * It reaches the sun with a `JMP` rather than a `JSR`, so that is a tail call and not a return --
 * which is why the two share this entry rather than the caller choosing between them.
 */

/*
 * 6502: SUN, in its four parts -- and it is not a filled circle drawn the obvious way.
 *
 * The sun is a stack of horizontal lines, one per screen row, whose half-widths come from
 * `sqrt(K^2 - v^2)` with a few random bits added so the edge is ragged rather than smooth. What
 * makes the routine worth reading is that it never erases and redraws: for each row it holds the
 * OLD half-width and the NEW one, clips both, and draws only the two pieces that DIFFER -- so a
 * sun drifting across the screen costs two short lines a row instead of two long ones.
 *
 * That is also why it takes the old centre as well as the new: `SUNX` is last frame's, `K3` is
 * this frame's, and the routine ends by copying one into the other.
 *
 * `CNT` is the raggedness, three bits rolled straight out of three comparisons against the
 * radius, so a small sun is smooth and a large one is not.
 */
void DrawSun(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw, MathWorkspace& _math,
             Rng& _rng, const Projection& _centre) noexcept;

void DrawPlanetOrSun(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                     GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                     Rng& _rng, const ShipBlock& _ship, Projection& _centre,
                     std::uint8_t _type) noexcept;

} // namespace Elite
