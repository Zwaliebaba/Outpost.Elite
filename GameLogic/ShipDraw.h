#pragma once

#include "Arith.h"
#include "Canvas.h"
#include "LineHeap.h"
#include "ShipDraw2x.h"
#include "Rng.h"
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
  /// `_numerator` is (A P+1 P) -- a coordinate's shape, the sign in the top byte -- and the
  /// quotient is left in `K`, where `PLS1`, `PLS6` and `PLANET` read it (M2-c takes it further).
  /// Returns `K(3 2 1 0)`, the quotient -- a value since M2-c-3. `_math` is still here for one
  /// byte: `DV9`'s `STA Q`, which is the frame's `Q` (Modernize.md section 8; risk R22, closed).
  KBlock DivideByShipZ(const Ship& _ship, MathWorkspace& _math, SignMag24 _numerator) noexcept;

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
  [[nodiscard]] ScreenOffset DivideToScreenOffset(const Ship& _ship, MathWorkspace& _math, SignMag24 _numerator) noexcept;

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
  ProjectResult Project(const Ship& _ship, MathWorkspace& _math, Projection& _screen) noexcept;

  /*
   * 6502: LL155, with the LL27 loop it is the head of -- draw every line on a ship's line heap.
   *
   * Byte 0 of the heap is its length in bytes, and under four there is not a whole line there, so
   * nothing is drawn. Everything after it is groups of four: x1, y1, x2, y2, which are `XX15` to
   * `XX15+3` -- the SAME zero-page bytes as `X1`, `Y1`, `X2`, `Y2`, so the loop writes straight into
   * `LOIN`'s arguments and calls it. Each group is one `Line` here.
   *
   * `LOIN` plots by EOR, so this both draws a ship and rubs it out; which one it is depends only on
   * whether the same lines are already on the screen. That is the whole of Elite's ship animation.
   */
  void DrawShipLines(Canvas& _canvas, const LineHeap& _heap, HeapOffset _run, Picture* _picture = nullptr) noexcept;

  /*
   * 6502: LL81 -- store the heap's length in byte 0 and fall straight into `LL155`.
   *
   * Two instructions and a fall-through, and the fall-through is the routine: `LL9` reaches it
   * having built the heap and left the length in `U`, and `SHPPT` reaches `LL81+2` with the length
   * already in A. Both then draw. Ported as one function with the length as a parameter, because
   * the difference between the two entry points is only where the byte came from.
   */
  void StoreLineCountAndDraw(Canvas& _canvas, LineHeap& _heap, HeapOffset _run, std::uint8_t _count,
                             Picture* _picture = nullptr) noexcept;

  /*
   * 6502: EE51 -- take the ship off the screen, if it is on it.
   *
   * Bit 3 of `INWK+31` is the whole state: set means the lines on the heap are currently on the
   * screen. The routine clears it with an `EOR` (not an `AND`, because A already holds the mask and
   * the bit is known set) and redraws, which erases. If the bit is clear there is nothing there and
   * it returns through `LL10-1`, an `RTS` that belongs to the routine before it.
   *
   * RETURNS THE CARRY IT EXITS WITH, because the `EE55` block reads it (§6.157). It is not `LOIN`'s:
   * `LL155` ends `INY / CPY XX20 / BCC LL27 / RTS`, so a heap with lines on it leaves the flag SET
   * by the compare that ended the loop, whatever the line drawing did before it; a heap under four
   * bytes leaves `CMP #4 / BCC LL82` -- CLEAR; and a ship that was not on the screen returns through
   * a bare `RTS` with the flag the caller arrived with, which is `_carryIn`.
   */
  bool EraseShip(Canvas& _canvas, Ship& _ship, const LineHeap& _heap, bool _carryIn, Picture* _picture = nullptr) noexcept;

  /*
   * 6502: the six instructions after `JSR EE51` in `LL9` part 1, and the `EE55` loop -- set up a
   * newly killed ship's explosion cloud on its line heap.
   *
   * Byte 1 is 18, the counter `DOEXP` ages; byte 2 is `(XX0),7`, how many vertices the cloud
   * blooms from, which arrives as the blueprint's `explosionCount`; bytes 3 to 6 are `DORND`. The
   * FIRST `DORND` rolls in the carry `EE51` returned, and the other three run on the CLEAR that
   * `CPY #6` leaves while Y is still under six.
   *
   * THIS WAS A SEAM WITH NOTHING BEHIND IT UNTIL 2026-09-06, on the belief that the carry came out
   * of `LOIN` and could not be known (§6.91). With the six bytes never written, `DOEXP` read byte 2
   * as zero, ran its vertex copy from index 0 down through 255 to 7, and wrote two hundred and
   * fifty bytes of `XX3` over every line heap above the dying ship's -- which the ships owning those
   * heaps then drew as lines, all over the screen and past the bottom of the bitmap into screen
   * RAM. The death sequence was where it showed (§6.157).
   */
  void SeedExplosionCloud(LineHeap& _heap, HeapOffset _run, std::uint8_t _explosionCount, Rng& _rng, bool _carryIn) noexcept;

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
  void DrawShipAsPoint(Canvas& _canvas, Ship& _ship, LineHeap& _heap, MathWorkspace& _math, Projection& _screen,
                       Picture* _picture = nullptr) noexcept;

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
    std::array<std::uint8_t, 18> scaledOrientation{};

    /// 6502: XX12 -- three sign-magnitude dot products, magnitude then sign.
    std::array<std::uint8_t, 6> dotProducts{};

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
    std::array<std::uint8_t, 16> faceVisible{};

    /*
     * 6502: XX3 -- the projected vertices, four bytes each: x as sixteen bits, then y.
     *
     * In the original this is at 256, the bottom of the STACK page, and the 6502's own stack grows
     * down towards it from 511. Nothing here needs that, but the SIZE does come from it: the edge
     * data indexes this with a whole byte and reads up to `XX3+3,X`, so 259 bytes are reachable.
     * The game itself never fills more than 148 -- thirty-seven vertices, the Anaconda.
     */
    std::array<std::uint8_t, 260> projectedVertices{};

    /*
     * 6502: XX4, XX17, XX18, XX20, V(1 0) and CNT went with M2-c-3.
     *
     * They are `LL9`'s own -- the distance threshold, the three loops' counters, the position it
     * halves, the loops' bounds, the blueprint walker and where the next vertex goes in `XX3` --
     * and they were zero page because parts 1 to 11 are eleven entry points sharing one workspace.
     * `DrawShip` is one function, so they are its locals. `HFL5`'s `XX4` went with them: it counts
     * the hyperspace rings and `LL9` part 1 overwrites the eight it leaves before reading it.
     *
     * What is left in this struct is the four stage results parts 3 to 11 hand each other, two of
     * which have a reader outside the routine -- `DOEXP`'s copy of `XX3` and `DOCKIT`'s `XX2+10`
     * (§6.125). That is why the frame is still a struct and not four more locals.
     */
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
  void DotProducts(Vector16 _vector, GeometryWorkspace& _geometry) noexcept;

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

    /*
     * 6502: Q as the loop leaves it -- 0 after `LL122`, still the gradient after `LL121`.
     *
     * Not part of the step, and here because it is not the helpers' scratch either: `Q` is the
     * frame's, and the altitude check reads whatever the frame last left in it (M2-b, §8; risk
     * R22). `LL129` writes it and `LL122` shifts it, so a ship whose lines are clipped leaves a
     * different `Q` behind than one whose are not -- which is observable, so it is carried out
     * rather than made local with `R` and `S`.
     */
    std::uint8_t divisorLeft = 0;
  };

  /*
   * 6502: XX15(1 0) and XX15(3 2) -- one end of a line, sixteen bits an axis, as `LL118` clamps it.
   *
   * It goes in as two sixteen-bit coordinates and comes out as two eight-bit ones in the same
   * bytes: `xLow` and `yLow` hold the answer and the two high bytes are cleared. That aliasing is
   * `XX15`'s own and is why the port keeps the four bytes together rather than as two integers.
   */
  struct Point16
  {
    std::uint8_t xLow = 0;  ///< 6502: XX15
    std::uint8_t xHigh = 0; ///< 6502: XX15+1
    std::uint8_t yLow = 0;  ///< 6502: XX15+2
    std::uint8_t yHigh = 0; ///< 6502: XX15+3
  };

  /*
   * 6502: XX15's six bytes with XX12(1 0) -- the line `LL145` takes.
   *
   * The second end's y is in `XX12(1 0)` and not in `XX15`, because `XX15` is six bytes and a line
   * of two sixteen-bit points needs eight. `LL9` part 10 fills both, and so does `BLINE`.
   */
  struct Line16
  {
    Point16 first;  ///< 6502: XX15(1 0) and XX15(3 2)
    Point16 second; ///< 6502: XX15(5 4) and XX12(1 0)
  };

  /*
   * 6502: what `LL145` answers with -- the clipped line, the carry, `SWAP` and `XX13`.
   *
   * `ends` is `XX13`: 0 if the far end is on screen, 143 if neither end is, 71 if only the near one
   * is. The upstream header gives 0, 95 and 191, which are the BBC's -- they are `Y*2-1` and half
   * of it, and the C64's Y is 72 rather than 96. 143 has bit 7 SET, which is what `LL83` reads.
   * `BLINE` reads it after the call, which is why it is here and not a local.
   */
  struct ClipResult
  {
    Line line;             ///< 6502: X1, Y1, X2, Y2 -- the same bytes as `XX15`, four of them
    bool rejected = false; ///< 6502: the carry -- the line cannot be made to fit

    /*
     * 6502: SWAP, and a BYTE rather than a bool because `LL147` decrements it.
     *
     * `LL145` zeroes it first, so its answer is 0 or 255 and reads as "were the ends exchanged".
     * `LL147` does not, and `LL9` part 10 clips edge after edge through it -- so the byte walks
     * 255, 254, ... A `bool` would be right for the reader (`BLINE` tests it with `BNE`) and wrong
     * for the byte, and the byte is what the sweep compares.
     */
    std::uint8_t swap = 0;

    std::uint8_t ends = 0; ///< 6502: XX13
  };

  /*
   * 6502: XX12+2, XX12+3 and T -- the line's gradient, its direction and which axis it is measured
   * along, which is what `LL115` computes and `LL118`'s four clamps walk the point with.
   */
  struct Slope
  {
    std::uint8_t gradient = 0;  ///< 6502: XX12+2 -- `LL28`'s quotient, the smaller span over the larger
    std::uint8_t direction = 0; ///< 6502: XX12+3 -- the two spans' signs EOR'd
    std::uint8_t steep = 0;     ///< 6502: T -- 0 when the line moves further across than down, 255 when it does not
  };

  /// What `LL129` leaves: the divisor in `Q`, the magnitude in `(S R)`, and the sign in A.
  struct PreparedSlope
  {
    SignMag16 magnitude;        ///< 6502: (S R), made positive -- `lo` is R and `hi` is S
    std::uint8_t divisor = 0;   ///< 6502: Q, which is the gradient
    std::uint8_t sign = 0;      ///< 6502: A -- the original S EOR'd with the slope's direction
  };

  /*
   * 6502: LL129 -- Q = XX12+2, (S R) = |S R|, and the answer is the sign the step will get.
   *
   * The returned byte is the ORIGINAL S EOR'd with the slope direction in `XX12+3`, taken before
   * the magnitude is made positive. Both callers push it and use it at the very end to decide
   * whether to negate, so it is a return value here rather than a side effect.
   */
  [[nodiscard]] PreparedSlope PrepareSlope(Slope _slope, SignMag16 _distance) noexcept;

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
  /// `_distance` is `(S R)`: how far off the edge the point is. `LL120` overwrites `R` with the
  /// point's own low byte first, which is what `_xLow` is.
  [[nodiscard]] SlopeStep StepAlongX(Slope _slope, std::uint8_t _distanceHigh, std::uint8_t _xLow) noexcept;
  [[nodiscard]] SlopeStep StepAlongY(Slope _slope, SignMag16 _distance) noexcept;

  /*
   * 6502: LL118 -- move a point along its line until it is on the screen.
   *
   * Four clamps in order, one per edge, and each is "step along the slope by however far off you
   * are, then set the coordinate to the edge". The step is `LL120` for the x edges and `LL123` for
   * the y ones, which is the same code under a different reading of `T`.
   *
   * The point goes IN as two sixteen-bit coordinates and comes out as two eight-bit ones in the
   * same bytes -- `Point16`'s two high bytes are cleared on the way out, which is `XX15`'s aliasing
   * doing its work.
   *
   * `_math` is here for one byte: each clamp leaves `Q` where its multiply or divide stopped, and
   * that is the frame's `Q` (R22). `R` and `S` are the helpers' own and stop here.
   */
  void MovePointOnScreen(Point16& _point, Slope _slope, MathWorkspace& _math) noexcept;

  /*
   * 6502: XX13 and dontclip -- what `LL145` reports, and the one flag that switches it off.
   *
   * `dontclip` is NOT the clipper's own scratch: `TT23` sets it to 199 so that the short-range
   * chart can use the whole screen instead of being clipped to the space view, and `RES2` clears
   * it again -- state one screen writes and another screen's clipper reads, which is why it is
   * still a struct after M2-c-2 while `XX13` and `SWAP` became `ClipResult`'s fields. `TT23` writes
   * `Yx2M1` in the same two instructions and that byte is on `PlanetSunState`; whichever slice
   * wires `TT23` writes both, and that is where this byte belongs with it.
   */
  struct ClipState
  {
    /// 6502: dontclip -- bit 7 set means return the line unclipped.
    std::uint8_t clippingOff = 0;
  };

  /*
   * 6502: LL145 and LL147 -- clip a line to the screen.
   *
   * In: two sixteen-bit points, which the original holds in `XX15`'s six bytes and `XX12(1 0)`.
   * Out: four eight-bit coordinates in the SAME bytes, which is why `XX15` cannot be split into a
   * geometry vector and a line (§6.37) -- and since M2-c-2 both are values.
   *
   * `rejected` is the carry: the line cannot be made to fit.
   *
   * `LL147` is the second entry point and differs in ONE thing: it does not zero `SWAP` first, so a
   * caller that clips several segments in a row accumulates the flag -- `_swappedIn` is what it
   * accumulates onto. `LL9` part 10 is its only caller and it happens to have `XX15+5` in the
   * accumulator at the call, so `_secondXHigh` is passed explicitly rather than assumed.
   * `_swapIn` is the `SWAP` byte `LL147` decrements onto and `LL145` ignores.
   *
   * `_math` is here for one byte: `Q`. `LL115` leaves its divisor there and each of `LL118`'s
   * clamps leaves whatever its multiply or divide stopped on, and that is the frame's `Q` -- the
   * byte the altitude check reads (M2-b, §8; risk R22). `R`, `S` and `T` are the helpers' own since
   * M2-c-2 and no longer reach it.
   */
  [[nodiscard]] ClipResult ClipLine(Line16 _line, GeometryWorkspace& _geometry, MathWorkspace& _math, const ClipState& _clip) noexcept;
  [[nodiscard]] ClipResult ClipLineKeepingSwap(Line16 _line, GeometryWorkspace& _geometry, MathWorkspace& _math, const ClipState& _clip,
                                               std::uint8_t _swapIn, std::uint8_t _secondXHigh) noexcept;

  /*
   * `ShipDrawEffects` WAS HERE AND IS NOT ANY MORE (M6-0-a-3). It was the two places `LL9` leaves
   * its own code -- 6502: LL25's `JMP PLANET` and LL14's `JMP DOEXP` -- and it outlived every other
   * seam for one reason: in a flat oracle image the VIC-II's registers and `XX21` were the same
   * bytes, so an explosion drawn on the oracle side corrupted the blueprints of the ships drawn
   * after it (§6.108), and no whole frame with an explosion in it could be compared. `Cpu6502`
   * banks the I/O page now (M6-0-a-1), the frame fixture draws the cloud (M6-0-a-2), and the two
   * tail jumps are the calls into `PlanetDraw.cpp` and `Explosion.cpp` they always were. The
   * `EE55` block was a third seam here until 2026-09-06 and is `SeedExplosionCloud` above.
   */
  struct Universe;

  /*
   * 6502: LL9 parts 1 to 12 -- draw a ship.
   *
   * The hardest routine in Elite, and the shape of it is three passes: decide which of the ship's
   * faces you can see, project the vertices those faces touch, then walk the edges between visible
   * vertices and clip each one onto the screen. What comes out is the ship's line heap, which is
   * then drawn -- and drawn by EOR, so the same call erases the last frame's ship on the way.
   *
   * `_universe.work` is `INWK`, the zero-page copy. `_slot` is the same ship's block in `K%`, which
   * part 1 writes two bytes of directly through `INF` rather than waiting for the copy back; it is
   * the one thing the caller names, because which block `INF` points at is the caller's decision.
   * The type and the blueprint are `_universe.flight`'s, as `TYPE` and `XX0` are the game's.
   *
   * A ship too far away is drawn as a dot by `SHPPT` instead, and one behind the player or wider
   * than it is distant is rubbed out and abandoned. Both are `LL9` deciding not to draw, not the
   * caller.
   *
   * `_carryIn` is for one thing: a ship that arrives here killed and not yet exploding
   * has its cloud seeded with four `DORND`s, and the first of them rolls in the carry `JSR LL9` was
   * reached with when the ship was not on the screen to be erased (§6.157). Nothing between `LL9`'s
   * first instruction and `EE51` touches the flag -- `LDA`, `BIT`, `ORA`, `AND`, stores -- so the
   * caller's carry is the block's. Part 11 of the flight loop derives it; the title, the briefings
   * and the escape pod draw ships that are never killed, and pass a value nothing reads.
   */
  void DrawShip(Universe& _universe, Ship& _slot, bool _carryIn) noexcept;

} // namespace Elite
