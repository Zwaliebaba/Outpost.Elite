#pragma once

#include "Arith.h"
#include "Canvas.h"
#include "LineHeap.h"
#include "ShipSlot.h"

#include <array>
#include <cstdint>

namespace Elite
{

/*
 * Putting a ship on the screen (slice 3b).
 *
 * 6502: PROJ and the divide it is built on. Everything the space view draws goes through this:
 * a ship's position is where it is RELATIVE TO THE PLAYER in three dimensions, and turning that
 * into a pixel is one division per axis, x / z and -y / z, added to the centre of the view.
 *
 * The whole of the perspective in Elite is those two divisions. There is no matrix and no
 * near plane -- a ship behind the player is not clipped here at all, it is rejected earlier by
 * the sign of z, and a ship too far off to one side is rejected by the overflow test below.
 */

/// 6502: X and Y, from the constants block -- the centre of the space view, which the resolved
/// C64 source annotates as "the 256 x 144 space view". Canvas::SPACE_VIEW_HEIGHT is the 144.
inline constexpr std::uint8_t SPACE_VIEW_CENTRE_X = 128;
inline constexpr std::uint8_t SPACE_VIEW_CENTRE_Y = 72;

/// 6502: `#Y*2`, which the drawing code writes out every time rather than naming. It is the row
/// the dashboard starts on, so it is the first row a ship may not occupy.
inline constexpr std::uint8_t SPACE_VIEW_BOTTOM = 2 * SPACE_VIEW_CENTRE_Y;

/// 6502: the coordinate bytes of a ship's data block -- x, y and z, each a sixteen-bit magnitude
/// and a sign byte. Named here rather than in `ShipSlot.h` because this is the first code that
/// reads them as a POSITION rather than as bytes to copy about.
inline constexpr std::uint8_t SHIP_X_OFFSET = 0;
inline constexpr std::uint8_t SHIP_Y_OFFSET = 3;
inline constexpr std::uint8_t SHIP_Z_OFFSET = 6;

/*
 * What `PLS6` leaves behind. The original's contract is "(X K)", a sixteen-bit value split
 * between a register and a zero-page byte, plus the carry -- and plus A, which is not incidental
 * either: `SHPPT` reads what `PROJ` leaves in A instead of testing the carry.
 *
 * `low` and `high` are only meaningful when `overflow` is false. On the overflow path that
 * `PL21` takes, the original never loads X at all and it still holds whatever the caller left
 * there; nothing reads it, so nothing here pretends to know what it was.
 */
struct ScreenOffset
{
  std::uint8_t low = 0;  ///< 6502: K
  std::uint8_t high = 0; ///< 6502: X
  std::uint8_t a = 0;    ///< 6502: A on return
  bool overflow = false; ///< 6502: the C flag -- set when the magnitude reached 1024
};

/*
 * 6502: DVID3B2 -- K(3 2 1 0) = (A P+1 P) / (z_sign z_hi z_lo).
 *
 * The two-instruction preamble that turns `DVID3B` into "divide by this ship's z", and the
 * `ORA #1` in it is load-bearing: it is what guarantees the non-zero denominator the divide
 * needs, and a ship at z = 0 is a real thing the game produces.
 *
 * Filed under `ShipMove.cpp` by the ledger and built here instead, because what it is ABOUT is
 * the projection -- it exists to divide by a ship's distance, and its callers are `PLS6` here
 * and `PLANET`/`PLS1` in slice 3c. Nothing in the movement code calls it.
 */
void DivideByShipZ(const ShipBlock& _ship, MathWorkspace& _math, std::uint8_t _a) noexcept;

/*
 * 6502: PLS6 (with its PL21, PL44 and PL6 exits) -- (X K) = (A P+1 P) / z, overflowing at 1024.
 *
 * The overflow test is in two halves and both matter: the top two bytes of the quotient must be
 * zero, and then its high byte must be under four. 1024 rather than 256 because a planet's
 * CENTRE can be well off the screen while its edge is still visible, so the projection has to
 * survive coordinates the view cannot show.
 *
 * The negation at the end is two's complement across the pair -- the only place in the geometry
 * where a sign-magnitude number is converted, because a screen coordinate is an offset from the
 * centre and has to be added to it.
 */
[[nodiscard]] ScreenOffset DivideToScreenOffset(const ShipBlock& _ship, MathWorkspace& _math,
                                                std::uint8_t _a) noexcept;

/// 6502: K3(1 0) and K4(1 0) -- where a point landed on the screen, as sixteen bits per axis so
/// that a shape whose centre is off the edge still has somewhere to be drawn from. These are the
/// same zero-page bytes the short-range chart uses for the range circle's centre, one byte each;
/// `RangeCircle` in `Charts.h` is that use and this is not it.
struct Projection
{
  std::uint8_t x = 0;  ///< 6502: K3
  std::uint8_t x1 = 0; ///< 6502: K3+1
  std::uint8_t y = 0;  ///< 6502: K4
  std::uint8_t y1 = 0; ///< 6502: K4+1
};

/// What `PROJ` returns, which is not just the carry: `SHPPT` ignores the carry entirely and
/// branches on `A OR K3+1` instead, so the accumulator is part of the contract.
struct ProjectResult
{
  bool offScreen = false; ///< 6502: the C flag
  std::uint8_t a = 0;     ///< 6502: A -- K4+1 when the point projected, and not that when it did not
};

/*
 * 6502: PROJ -- project a ship, planet or sun onto the screen.
 *
 *   K3(1 0) = #X + 256 * x / z
 *   K4(1 0) = #Y - 256 * y / z
 *
 * The 256 is the scale `DVID3B` divides at and neither the upstream summary nor the routine's
 * name mentions -- see `Arith.h`. It is what makes one pixel a ratio of 1/256, so a ship a
 * quarter of the way to the screen edge is one whose x is an eighth of its z.
 *
 * The minus on y is an `EOR #128` on the sign byte before the divide, because space has y going
 * up and the screen has it going down.
 *
 * `_screen` is written a HALF AT A TIME. If x projects and y overflows, K3 has already been
 * stored and K4 has not, and the original leaves it that way; no caller reads either after an
 * overflow, but a port that computed both and assigned at the end would be a different routine.
 */
ProjectResult Project(const ShipBlock& _ship, MathWorkspace& _math, Projection& _screen) noexcept;

/*
 * 6502: LL155, with the LL27 loop it is the head of -- draw every line on a ship's line heap.
 *
 * Byte 0 of the heap is its length in bytes, and under four there is not a whole line there, so
 * nothing is drawn. Everything after it is groups of four: x1, y1, x2, y2, which are `XX15` to
 * `XX15+3` -- the SAME zero-page bytes as `X1`, `Y1`, `X2`, `Y2`, so the loop writes straight into
 * `LOIN`'s arguments and calls it. `DrawWorkspace` is those four bytes here.
 *
 * `LOIN` plots by EOR, so this both draws a ship and rubs it out; which one it is depends only on
 * whether the same lines are already on the screen. That is the whole of Elite's ship animation.
 */
void DrawShipLines(Canvas& _canvas, DrawWorkspace& _draw, const LineHeap& _heap,
                   std::uint16_t _address) noexcept;

/*
 * 6502: LL81 -- store the heap's length in byte 0 and fall straight into `LL155`.
 *
 * Two instructions and a fall-through, and the fall-through is the routine: `LL9` reaches it
 * having built the heap and left the length in `U`, and `SHPPT` reaches `LL81+2` with the length
 * already in A. Both then draw. Ported as one function with the length as a parameter, because
 * the difference between the two entry points is only where the byte came from.
 */
void StoreLineCountAndDraw(Canvas& _canvas, DrawWorkspace& _draw, LineHeap& _heap,
                           std::uint16_t _address, std::uint8_t _count) noexcept;

/*
 * 6502: EE51 -- take the ship off the screen, if it is on it.
 *
 * Bit 3 of `INWK+31` is the whole state: set means the lines on the heap are currently on the
 * screen. The routine clears it with an `EOR` (not an `AND`, because A already holds the mask and
 * the bit is known set) and redraws, which erases. If the bit is clear there is nothing there and
 * it returns through `LL10-1`, an `RTS` that belongs to the routine before it.
 */
void EraseShip(Canvas& _canvas, DrawWorkspace& _draw, ShipBlock& _ship, const LineHeap& _heap) noexcept;

/*
 * 6502: SHPPT, with its `Shpt` helper and its `nono` exit -- a distant ship, drawn as a dot.
 *
 * Two four-pixel horizontal lines one row apart, built onto the ship's line heap so that the next
 * frame erases them the same way it erases a wireframe. `LL9` comes here when a ship is too far
 * away to be worth drawing properly.
 *
 * IT DOES NOT TEST `PROJ`'S CARRY. It tests the accumulator ORed with `K3+1`, and on one of
 * `PROJ`'s two overflow exits the accumulator is zero and `K3+1` still holds the previous
 * projection's high byte. So a ship whose x coordinate overflowed can be drawn at wherever the
 * last one was, and reproducing that is why `_screen` is a parameter that outlives the call
 * rather than a local. It is a bug in the original, forty years old and shipped.
 */
void DrawShipAsPoint(Canvas& _canvas, DrawWorkspace& _draw, ShipBlock& _ship, LineHeap& _heap,
                     MathWorkspace& _math, Projection& _screen) noexcept;

/*
 * 6502: XX16 and XX12 -- the workspace `LL9`'s geometry runs in (slice 3b).
 *
 * Both are sized by what indexes them and both are confirmed by the zero-page layout, which is
 * §6.8's test passed three ways: `XX16` is at 69 and `XX0` at 87, eighteen bytes apart, and it
 * holds three vectors of six; `XX12` is at 113 and `K` at 119, six apart, and it holds three
 * results of two.
 *
 * They are arrays because registers index them -- `LDA XX16,X` and `STA XX12,Y` in `LL51` -- which
 * is the test for whether a workspace has to be addressable at all (§6.37).
 */
struct GeometryWorkspace
{
  /// 6502: XX16 -- the ship's three orientation vectors, scaled, as magnitude and sign pairs:
  /// sidev in 0 to 5, roofv in 6 to 11, nosev in 12 to 17.
  std::array<std::uint8_t, 18> xx16{};

  /// 6502: XX12 -- three sign-magnitude dot products, magnitude then sign.
  std::array<std::uint8_t, 6> xx12{};

  /*
   * 6502: XX2 -- one byte per face saying whether you can see it, and SIXTEEN of them.
   *
   * Three measurements agree (§6.37): the edge data indexes it with a NIBBLE, the largest face
   * count across the thirty-three blueprints is fifteen, and `XX2` is at 53 with `XX16` at 69.
   *
   * Those are the same bytes as `K3` and `K4`, which hold a projected screen position -- 53/54
   * and 67/68 sit inside this range. They are never live together, because `SHPPT` is a jump out
   * of `LL9` part 2 and `EE30` fills this in part 4. The port stores them apart; nothing may
   * assume that means they are independent.
   */
  std::array<std::uint8_t, 16> xx2{};

  /*
   * 6502: XX3 -- the projected vertices, four bytes each: x as sixteen bits, then y.
   *
   * In the original this is at 256, the bottom of the STACK page, and the 6502's own stack grows
   * down towards it from 511. Nothing here needs that, but the SIZE does come from it: the edge
   * data indexes this with a whole byte and reads up to `XX3+3,X`, so 259 bytes are reachable.
   * The game itself never fills more than 148 -- thirty-seven vertices, the Anaconda.
   */
  std::array<std::uint8_t, 260> xx3{};

  /// 6502: XX4 -- the ship's distance, as the visibility test's units. Thirty-one until part 2
  /// works out the real one, and zero while the ship is exploding so that everything is drawn.
  std::uint8_t xx4 = 0;

  /// 6502: XX17 -- how far each of `LL9`'s three loops has walked, in its own units: vertices in
  /// part 6, edges in part 10, and a shift count in part 5.
  std::uint8_t xx17 = 0;

  /// 6502: XX18 -- the ship's position copied out of `INWK`, halved until it fits, and then
  /// replaced by its own dot products with the orientation vectors.
  std::array<std::uint8_t, 9> xx18{};

  /// 6502: XX20 -- what each loop counts up to, taken from the blueprint's header.
  std::uint8_t xx20 = 0;

  /// 6502: CNT -- where the next projected vertex goes in `XX3`, in bytes.
  std::uint8_t cnt = 0;

  /// 6502: V(1 0) -- a walker into the blueprint. An address here, because the blueprints are one
  /// address-indexed region (§6.32) rather than an array per ship.
  std::uint16_t v = 0;
};

/*
 * 6502: LL51 (with its `ll51` loop) -- the three dot products of `XX15` with each of `XX16`'s
 * vectors, left in `XX12`.
 *
 * This is how Elite decides both what a ship looks like and which of its faces you can see. `LL9`
 * calls it twice with the same code and different contents: from part 5 with the ship's own
 * position and the orientation vectors, which gives the ship's position in ITS frame; and from
 * part 6 with a vertex and the TRANSPOSED vectors, which rotates that vertex into the player's.
 * Part 6 does the transposing itself, by swapping six pairs of bytes in `XX16` in place.
 *
 * Filed under `Arith.cpp` and `ShipMove.cpp` by the ledger, deferred to 3a because it reads
 * `XX15` and `XX16`. Those exist as part of `LL9`, it is called from `LL9` and nowhere else, and
 * it is built here (§6.37).
 */
void DotProducts(const DrawWorkspace& _draw, GeometryWorkspace& _geometry, MathWorkspace& _math) noexcept;

/*
 * The line clipper's arithmetic (slice 3b).
 *
 * `LL145` clips a line whose ends are sixteen-bit coordinates down to the eight-bit screen, and
 * to do that it has to step along the line by a gradient. These three are that step. They are
 * one routine with four entry points in the original and they jump into each other's bodies, so
 * they are ported together.
 */

/// What `LL120` and `LL123` leave in (Y X) -- a sixteen-bit signed step along the line.
struct SlopeStep
{
  std::uint8_t low = 0;  ///< 6502: X
  std::uint8_t high = 0; ///< 6502: Y
};

/*
 * 6502: LL129 -- Q = XX12+2, (S R) = |S R|, and the answer is the sign the step will get.
 *
 * The returned byte is the ORIGINAL S EOR'd with the slope direction in `XX12+3`, taken before
 * the magnitude is made positive. Both callers push it and use it at the very end to decide
 * whether to negate, so it is a return value here rather than a side effect.
 */
[[nodiscard]] std::uint8_t PrepareSlope(MathWorkspace& _math, const GeometryWorkspace& _geometry) noexcept;

/*
 * 6502: LL120 and LL123, which are the same code with the dispatch the other way round.
 *
 * `T` says whether the line is steep or shallow, and that decides whether the step is a multiply
 * or a divide: `LL120` multiplies on a shallow slope and divides on a steep one, `LL123` does the
 * opposite. `LL120` also sets `R` from `x1_lo` first, which `LL123` does not.
 *
 * The result takes the OPPOSITE sign to the slope direction, which is why both end by negating
 * when `LL129`'s byte came out positive rather than negative.
 */
[[nodiscard]] SlopeStep StepAlongX(MathWorkspace& _math, const GeometryWorkspace& _geometry,
                                   const DrawWorkspace& _draw) noexcept;
[[nodiscard]] SlopeStep StepAlongY(MathWorkspace& _math, const GeometryWorkspace& _geometry) noexcept;

/*
 * 6502: LL118 -- move a point along its line until it is on the screen.
 *
 * Four clamps in order, one per edge, and each is "step along the slope by however far off you
 * are, then set the coordinate to the edge". The step is `LL120` for the x edges and `LL123` for
 * the y ones, which is the same code under a different reading of `T`.
 *
 * The point goes IN as two sixteen-bit coordinates in `XX15(1 0)` and `XX15(3 2)` and comes out
 * as two eight-bit ones in `XX15` and `XX15+2` -- so in this port's field names, x1 in `x1` and
 * y1 in `x2`. That is `XX15`'s aliasing doing its work: `Y1` and `Y2` hold the high bytes going
 * in and are cleared on the way out.
 */
void MovePointOnScreen(DrawWorkspace& _draw, const GeometryWorkspace& _geometry,
                       MathWorkspace& _math) noexcept;

/*
 * 6502: XX13, SWAP and dontclip -- what `LL145` reports, and the one flag that switches it off.
 *
 * `dontclip` is NOT the clipper's own state: `TT23` sets it to 199 so that the short-range chart
 * can use the whole screen instead of being clipped to the space view, and `RES2` clears it
 * again. It is a slice-2 routine reaching into a slice-3b one, which the ledger's row does not
 * say, so it is a parameter here rather than a constant.
 */
struct ClipState
{
  /*
   * 6502: XX13 -- 0 if the far end is on screen, 143 if neither end is, 71 if only the near one
   * is. The upstream header gives 0, 95 and 191, which are the BBC's: they are `Y*2-1` and half
   * of it, and the C64's Y is 72 rather than 96. 143 has bit 7 SET, which is what the test at
   * `LL83` reads.
   */
  std::uint8_t xx13 = 0;

  /// 6502: SWAP -- &FF when the two ends came back exchanged, which the caller needs because a
  /// line that was reversed cannot be joined onto the end of the one before it.
  std::uint8_t swap = 0;

  /// 6502: dontclip -- bit 7 set means return the line unclipped.
  std::uint8_t dontclip = 0;
};

/*
 * 6502: LL145 and LL147 -- clip a line to the screen.
 *
 * In: three sixteen-bit coordinates, x1 in `XX15(1 0)`, y1 in `XX15(3 2)` and x2 in `XX15(5 4)`,
 * with y2 in `XX12(1 0)`. Out: four eight-bit ones in `X1`, `Y1`, `X2`, `Y2` -- the SAME bytes,
 * which is why `XX15` cannot be split into a geometry vector and a line (§6.37).
 *
 * Returns true when the line cannot be made to fit, which is the carry the original sets.
 *
 * `LL147` is the second entry point and differs in ONE thing: it does not zero `SWAP` first, so a
 * caller that clips several segments in a row accumulates the flag. `LL9` part 10 is its only
 * caller and it happens to have `XX15+5` in the accumulator at the call, so the byte is passed
 * explicitly here rather than assumed.
 */
[[nodiscard]] bool ClipLine(DrawWorkspace& _draw, GeometryWorkspace& _geometry, MathWorkspace& _math,
                            ClipState& _clip) noexcept;
[[nodiscard]] bool ClipLineKeepingSwap(DrawWorkspace& _draw, GeometryWorkspace& _geometry,
                                       MathWorkspace& _math, ClipState& _clip,
                                       std::uint8_t _a) noexcept;


/*
 * The two places `LL9` leaves its own code (slice 3b).
 *
 * `PLANET` is a tail jump taken when the type is negative, and belongs to 3c. `DOEXP` is the
 * explosion, filed under `Explosion.cpp`; `SeedExplosionCloud` is the six instructions that set
 * one up, and they are behind the seam rather than in `LL9` for two reasons -- what they write is
 * `DOEXP`'s state, and the `JSR DORND` among them runs on whatever carry `LOIN` last left, which
 * this port cannot say without reading all thirty-two of `LOIN`'s unrolled copies.
 */
class ShipDrawEffects
{
public:
  virtual ~ShipDrawEffects() = default;

  /// 6502: LL25 -- JMP PLANET, for a type with bit 7 set.
  virtual void DrawPlanetOrSun() = 0;

  /// 6502: LL14's JMP DOEXP -- redraw the explosion cloud, which is how it is erased.
  virtual void DrawExplosion() = 0;

  /// 6502: the EE55 block -- byte 1 of the heap is the cloud's size, byte 2 its particle count
  /// from the blueprint, and bytes 3 to 6 are random.
  virtual void SeedExplosionCloud(LineHeap& _heap, std::uint16_t _address,
                                  std::uint16_t _blueprint) = 0;
};

/*
 * 6502: LL9 parts 1 to 12 -- draw a ship.
 *
 * The hardest routine in Elite, and the shape of it is three passes: decide which of the ship's
 * faces you can see, project the vertices those faces touch, then walk the edges between visible
 * vertices and clip each one onto the screen. What comes out is the ship's line heap, which is
 * then drawn -- and drawn by EOR, so the same call erases the last frame's ship on the way.
 *
 * `_work` is `INWK`, the zero-page copy. `_slot` is the same ship's block in `K%`, which part 1
 * writes two bytes of directly through `INF` rather than waiting for the copy back.
 *
 * A ship too far away is drawn as a dot by `SHPPT` instead, and one behind the player or wider
 * than it is distant is rubbed out and abandoned. Both are `LL9` deciding not to draw, not the
 * caller.
 */
void DrawShip(Canvas& _canvas, DrawWorkspace& _draw, GeometryWorkspace& _geometry,
              MathWorkspace& _math, ClipState& _clip, Projection& _screen, ShipBlock& _work,
              ShipBlock& _slot, LineHeap& _heap, std::uint16_t _blueprint, std::uint8_t _type,
              ShipDrawEffects& _effects) noexcept;

} // namespace Elite
