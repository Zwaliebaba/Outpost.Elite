#include "pch.h"

#include "PlanetDraw.h"

#include "EliteTypes.h"
#include "Lines2x.h"
#include "LookupTables.h"

#include <algorithm>

namespace Elite
{

  SunRow ClipSunRow(PlanetSunState& _state, SignMag16 _centre, std::uint8_t _halfWidth, std::uint8_t _row) noexcept
  {
    // 6502: EDGES computes the right-hand end first. `T` is the kernel's byte and this routine's
    // own since M2-c-2; `X1` and `X2` are the answer.
    SunRow row;

    const AddResult right = AddWithCarry(_halfWidth, _centre.lo, false);
    row.x2 = right.value;
    const AddResult rightHigh = AddWithCarry(_centre.hi, 0u, right.carry);

    if ((rightHigh.value & 0x80u) != 0u)
    {
      /*
       * 6502: ED1 -- even the right-hand end is off the left of the screen. `X1` IS NOT WRITTEN on
       * this exit, and `HLOIN2` draws anyway (it drops the carry), so the original's row runs from
       * whatever `X1` last held -- a stale zero-page read. The port's `x1` is this struct's zero.
       * No game state reaches the exit: `SUNX` is written from `K3` only after a sun has been
       * drawn, so its high byte is 0 or 1 and a row's right end cannot land left of the screen.
       * A fixture that seeds `SUNX` with a random high byte does reach it, and M6-0-d found the
       * two machines differing there (§8); the fixture seeds a reachable centre now.
       */
      if (_row < _state.sun.size())
      {
        _state.sun[_row] = 0;
      }
      row.offScreen = true;
      return row;
    }

    // 6502: a high byte of zero means the right end is already a screen coordinate. Anything else
    // positive means the line runs off the right, so it is clamped.
    if (rightHigh.value != 0u)
    {
      row.x2 = 255;
    }

    const SubResult left = SubtractWithCarry(_centre.lo, _halfWidth, true);
    row.x1 = left.value;
    const SubResult leftHigh = SubtractWithCarry(_centre.hi, 0u, left.carry);

    if (leftHigh.value == 0u)
    {
      return row; // 6502: both ends are on screen as they stand, and the carry is cleared
    }

    // 6502: ED3 -- the left end needs deciding.
    if ((leftHigh.value & 0x80u) == 0u)
    {
      // Positive and non-zero: the whole line is off the right.
      if (_row < _state.sun.size())
      {
        _state.sun[_row] = 0;
      }
      row.offScreen = true;
      return row;
    }

    row.x1 = 0;
    return row;
  }

  void EraseSunRow(Canvas& _canvas, PlanetSunState& _state, SignMag16 _centre, std::uint8_t _halfWidth, std::uint8_t _row,
                   Picture* _picture) noexcept
  {
    // 6502: HLOIN2 clips the row, clears its heap entry and draws. The carry is dropped.
    const SunRow row = ClipSunRow(_state, _centre, _halfWidth, _row);

    if (_row < _state.sun.size())
    {
      _state.sun[_row] = 0;
    }

    DrawHorizontalLine(_canvas, row.x1, row.x2, _row); // 6502: HLOIN
    if (_picture != nullptr)
    {
      DrawCanvasRow2x(*_picture, row.x1, row.x2, _row);
    }
  }

  void ClearSunHeap(PlanetSunState& _state) noexcept
  {
    // 6502: FLFLLS and SAL6 -- the heap zeroed from the bottom row upwards.
    //
    // The loop stops at entry 1, so entry 0 is never zeroed -- and then the index falls to 255 and
    // that is what lands in it. One byte, two meanings, and the loop bound is what keeps them
    // apart.
    for (std::size_t row = _state.sun.size() - 1u; row != 0u; --row)
    {
      _state.sun[row] = 0;
    }
    _state.sun[0] = 0xFF;
  }

  void ClearBallHeap(PlanetSunState& _state) noexcept
  {
    // 6502: WP1 -- the heap top back to one and the first x marked empty.
    _state.ballHeapTop = 1;
    _state.SetBallX(0, 0xFF);
  }

  void EraseSun(Canvas& _canvas, PlanetSunState& _state, Picture* _picture) noexcept
  {
    // 6502: WPLS -- a negative first entry branches to `WPLS-1`, which is `WP1`'s own return. One
    // of the six backward label-with-offset targets §6.35 counted that land in the file BEFORE the
    // one naming them, and the second of them to be confirmed by building it.
    if ((_state.sun[0] & 0x80u) != 0u)
    {
      return;
    }

    // 6502: `YY(1 0)` is `EDGES`'s centre and a parameter since M2-c-3; what it holds here is where
    // the sun WAS.
    const SignMag16 wasAt{_state.sunX, _state.sunXNext};

    // 6502: the literal 143, in the same build where `CHKON` reads `Yx2M1`.
    for (std::uint8_t row = SPACE_VIEW_BOTTOM - 1u; row != 0u; --row)
    {
      const std::uint8_t width = _state.sun[row];
      if (width != 0u)
      {
        EraseSunRow(_canvas, _state, wasAt, width, row, _picture);
      }
    }

    _state.sun[0] = 0xFF;
  }

  void EraseBall(Canvas& _canvas, PlanetSunState& _state, Picture* _picture) noexcept
  {
    // 6502: WPLS2 -- entry 0 of the x heap is the flag: `CIRCLE` clears it when it starts filling,
    // so anything else means there is nothing to rub out.
    std::uint8_t at = _state.BallX(0);
    if (at != 0u)
    {
      ClearBallHeap(_state);
      return;
    }

    /*
     * 6502: the four endpoint bytes -- and the walk STARTS FROM WHATEVER THE FIRST TWO HOLD. A
     * run's first segment has no predecessor, so when entry 0 is not a break the first `LOIN`
     * draws from the
     * point the last routine to write those bytes left there. `WS2` makes entry 0 a break, so the
     * game reaches that only through a heap nothing cleared, and `WS2` is what makes entry 0 a
     * break -- so the port starts the walk from a zeroed `Line` rather than from bytes the last
     * drawer left (M2-c-2, and the sweep that pins it seeds the same zeroes).
     */
    Line line;

    while (true)
    {
      // 6502: WPL1 -- past the heap's top and there is nothing left to erase.
      if (at >= _state.ballHeapTop)
      {
        ClearBallHeap(_state);
        return;
      }

      const std::uint8_t y = _state.BallY(at);
      if (y == 0xFFu)
      {
        /*
         * 6502: WP2 -- a break. The pair after it is a new run's START, not another segment's
         * end, which is how a circle that runs off the screen comes back as several polylines
         * rather than one with a chord across it.
         */
        ++at;
        line.x1 = _state.BallX(at);
        line.y1 = _state.BallY(at);
        ++at;
        continue;
      }

      line.y2 = y;
      line.x2 = _state.BallX(at);
      if (_picture != nullptr)
      {
        // The wide segment is the faithful one doubled, taken BEFORE the call because `LOIN` hands
        // the four bytes back the other way round when it drew right to left.
        DrawLine2x(*_picture, line);
      }
      const DrawnLine drawn = DrawLine(_canvas, line);
      line = drawn.ends; // the four bytes as `LOIN` leaves them, the other way round when it swapped
      ++at;

      /*
       * 6502: `LOIN` swaps its endpoints when it draws right-to-left, and
       * when it has, the coordinates in X2/Y2 are no longer this segment's end. So the hand-off to
       * the next segment is SKIPPED, and the next `LOIN` starts from whatever X1/Y1 now hold.
       */
      if (!drawn.swapped)
      {
        line.x1 = line.x2;
        line.y1 = line.y2;
      }
    }
  }

  void ErasePlanetOrSun(Canvas& _canvas, PlanetSunState& _state, ShipType _type, Picture* _picture) noexcept
  {
    // 6502: PL2 -- the planet is 128 and the sun 129, so the bottom bit is the whole of the test
    // and no comparison is needed.
    if ((Byte(_type) & 0x01u) != 0u)
    {
      EraseSun(_canvas, _state, _picture);
    }
    else
    {
      EraseBall(_canvas, _state, _picture);
    }
  }

  CircleExtent CircleOffScreen(const PlanetSunState& _state, std::uint8_t _radius, const Projection& _centre) noexcept
  {
    CircleExtent extent;
    // 6502: CHKON -- four sixteen-bit comparisons, and each one's high byte is all that is read.
    const AddResult rightLow = AddWithCarry(_centre.x, _radius, false);
    (void)rightLow;
    const AddResult right = AddWithCarry(_centre.x1, 0u, rightLow.carry);
    if ((right.value & 0x80u) != 0u)
    {
      extent.offScreen = true; // 6502: PL21 -- set the carry and return
      return extent;
    }

    const SubResult leftLow = SubtractWithCarry(_centre.x, _radius, true);
    const SubResult left = SubtractWithCarry(_centre.x1, 0u, leftLow.carry);
    if ((left.value & 0x80u) == 0u && left.value != 0u)
    {
      extent.offScreen = true;
      return extent;
    }

    // 6502: PL31 -- and the y half also STORES, so the answer is three values rather than a flag.
    const AddResult bottomLow = AddWithCarry(_centre.y, _radius, false);
    extent.bottom = bottomLow.value;
    const AddResult bottom = AddWithCarry(_centre.y1, 0u, bottomLow.carry);
    if ((bottom.value & 0x80u) != 0u)
    {
      extent.offScreen = true;
      return extent;
    }
    extent.bottomHigh = bottom.value;

    const SubResult topLow = SubtractWithCarry(_centre.y, _radius, true);
    const SubResult top = SubtractWithCarry(_centre.y1, 0u, topLow.carry);
    if ((top.value & 0x80u) != 0u)
    {
      return extent; // 6502: PL44 -- which is `PLS6`'s carry clear, not `EDGES`'s (§6.45)
    }
    if (top.value != 0u)
    {
      extent.offScreen = true;
      return extent;
    }

    // 6502: the carry from the last comparison IS the return value.
    extent.offScreen = topLow.value >= _state.lowestVisibleRow;
    return extent;
  }

  std::uint8_t DrawBallLine(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                            const Projection& _centre, SignMag16 _offset, std::uint8_t _angle, bool _carryIn, Picture* _picture) noexcept
  {
    // 6502: the segment's far end, as an offset from the circle's centre, with both halves running
    // on the caller's carry. The two halves of that offset are parameters since M2-c-3.
    const AddResult low = AddWithCarry(_offset.lo, _centre.y, _carryIn);
    _state.segmentEnd[2] = low.value;
    _state.segmentEnd[3] = AddWithCarry(_centre.y1, _offset.hi, low.carry).value;

    // 6502: BL1 -- the first segment of a circle has a start and no end yet, so it goes straight to
    // the break rather than being drawn.
    bool endTheRun = _state.flag != 0u;
    if (endTheRun)
    {
      ++_state.flag;
    }
    else
    {
      // 6502: BL1 -- the eight bytes of the two endpoints into the clipper's own workspace.
      Line16 segment;
      segment.first.xLow = _state.segmentStart[0];
      segment.first.xHigh = _state.segmentStart[1];
      segment.first.yLow = _state.segmentStart[2];
      segment.first.yHigh = _state.segmentStart[3];
      segment.second.xLow = _state.segmentEnd[0];
      segment.second.xHigh = _state.segmentEnd[1];

      // 6502: `XX12(1 0)` -- the far end's y, which `LL145` reads there and `LL9`'s frame owns.
      _geometry.dotProducts[0] = _state.segmentEnd[2];
      _geometry.dotProducts[1] = _state.segmentEnd[3];
      segment.second.yLow = _geometry.dotProducts[0];
      segment.second.yHigh = _geometry.dotProducts[1];

      const ClipResult clipped = ClipLine(segment, _geometry, _math, _clip);
      Line line = clipped.line;

      if (clipped.rejected)
      {
        endTheRun = true; // 6502: BL5 -- clipped away entirely
      }
      else
      {
        // 6502: BL9 -- the clipper may hand the ends back the other way round, and the heap has to
        // hold them in the order the walk produced them.
        if (clipped.swap != 0u)
        {
          std::swap(line.x1, line.x2);
          std::swap(line.y1, line.y2);
        }

        /*
         * 6502: BL9 / BL8 -- the segment onto the heap.
         *
         * The start point is written only when the entry before is a break, which is what makes a
         * run of segments N+1 points rather than 2N.
         */
        std::uint8_t at = _state.ballHeapTop;
        if (_state.BallYBefore(at) == 0xFFu)
        {
          _state.SetBallX(at, line.x1);
          _state.SetBallY(at, line.y1);
          ++at;
        }
        _state.SetBallX(at, line.x2);
        _state.SetBallY(at, line.y2);
        ++at;
        _state.ballHeapTop = at;

        (void)DrawLine(_canvas, line);
        if (_picture != nullptr)
        {
          DrawLine2x(*_picture, line);
        }

        // 6502: BL5 -- an end that had to be moved ends the run too, because the next segment does
        // not start where this one was drawn to.
        endTheRun = clipped.ends != 0u;
      }
    }

    if (endTheRun)
    {
      // 6502: BL5 -- a break, unless the last thing written was already one.
      const std::uint8_t at = _state.ballHeapTop;
      if (_state.BallYBefore(at) != 0xFFu)
      {
        _state.SetBallY(at, 0xFF);
        ++_state.ballHeapTop;
      }
    }

    // 6502: BL7 -- this segment's end is the next one's start, and the angle moves on.
    _state.segmentStart = _state.segmentEnd;
    return AddWithCarry(_angle, _state.circleStep, false).value;
  }

  namespace
  {

    /*
     * 6502: the negate-into-sixteen-bits block, which `CIRCLE2` has twice and `PLS22` twice more.
     *
     * It negates a byte into a sixteen-bit value: the low half two's-complemented and the high half
     * either 255 or 0 depending on whether the negation carried. Both halves add zero on the carry
     * the comparison above left, which is SET -- that set bit is the "+1" of the two's complement,
     * and the block would be wrong without it.
     */
    struct Negated
    {
      std::uint8_t low = 0;
      std::uint8_t high = 0;
    };

    Negated NegateWide(std::uint8_t _a, bool _carryIn) noexcept
    {
      const AddResult low = AddWithCarry(static_cast<std::uint8_t>(_a ^ 0xFFu), 0u, _carryIn);
      const AddResult high = AddWithCarry(0xFFu, 0u, low.carry);
      return {low.value, high.value};
    }

  } // namespace

  void DrawBall(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                const Projection& _centre, std::uint8_t _radius, bool _carryIn, Picture* _picture) noexcept
  {
    // 6502: the flag set and the angle zeroed. `CNT` is the angle this walk is at, `CIRCLE2`'s own
    // since M2-c-3: `BLINE` advances it and hands it back, which is what the loop below reads.
    _state.flag = 0xFF;
    std::uint8_t angle = 0;

    bool carry = _carryIn;

    for (;;)
    {
      /*
       * 6502: PLL3 -- the two coordinates, a quarter-turn apart in the same table.
       *
       * `FMLTU2` masks to five bits, so the table is a quarter-wave and the sign has to be put
       * back by hand; that is what the two comparisons against 33 do. 33 rather than 32 because
       * what is being compared is a count the loop has already stepped.
       */
      const LogProduct sine = MultiplyBySine(_radius, angle, carry);
      std::uint8_t across = sine.value;
      std::uint8_t high = 0; // 6502: T -- the offset's high byte, this loop's own since M2-c-3

      carry = angle >= 33u; // 6502: past the quarter-wave, so the sign has to be put back
      if (carry)
      {
        const Negated negated = NegateWide(across, carry);
        across = negated.low;
        high = negated.high;
        carry = false; // 6502: the carry cleared for the addition below
      }

      // 6502: PL37 -- and the centre added on, sixteen bits at a time.
      const AddResult xLow = AddWithCarry(across, _centre.x, carry);
      _state.segmentEnd[0] = xLow.value;
      _state.segmentEnd[1] = AddWithCarry(_centre.x1, high, xLow.carry).value;

      // 6502: the same table a quarter-turn on, which is the cosine.
      const AddResult quarter = AddWithCarry(angle, 16u, false);
      const LogProduct cosine = MultiplyBySine(_radius, quarter.value, false);
      std::uint8_t down = cosine.value;
      high = 0;

      /*
       * 6502: fifteen added to the angle, masked to six bits, and compared against 33.
       *
       * Nothing clears the carry before that addition, so it runs on `FMLTU2`'s exit flag -- set on
       * both of its antilog exits and clear on the one that returns zero. Fifteen plus that carry
       * is the sixteen used above, so the quarter-turn is only a quarter-turn when the multiply
       * produced something (§6.50).
       */
      const AddResult stepped = AddWithCarry(angle, 15u, cosine.carry);
      carry = static_cast<std::uint8_t>(stepped.value & 0x3Fu) >= 33u;
      if (carry)
      {
        const Negated negated = NegateWide(down, carry);
        down = negated.low;
        high = negated.high;
        carry = false; // 6502: CLC
      }

      // 6502: PL38 -- and the segment is drawn, with the y offset still in X.
      const std::uint8_t reached =
        DrawBallLine(_canvas, _state, _geometry, _math, _clip, _centre, SignMag16{down, high}, angle, carry, _picture);
      angle = reached;

      // 6502: sixty-four steps of one, or eight of eight.
      if (reached >= 65u)
      {
        return; // 6502: the carry cleared on the way out
      }
      carry = false; // the branch was not taken, so the comparison left it clear
    }
  }

  bool DrawCircle(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                  const Projection& _centre, std::uint8_t _radius, Picture* _picture) noexcept
  {
    // 6502: RTS2 -- `CIRCLE` wants `CHKON`'s carry only; the extent it also computes is `SUN`'s.
    if (CircleOffScreen(_state, _radius, _centre).offScreen)
    {
      return true;
    }

    // 6502: zeroing entry 0 of the x heap is the flag that tells `WPLS2` there is something to rub out.
    _state.SetBallX(0, 0);

    /*
     * 6502: PL89 -- the step halved once above a radius of 8 and again above 60.
     *
     * Eight steps for a speck, sixteen for a planet, thirty-two for one you are close to. Two
     * shifts and two comparisons rather than a table, and the carry the second comparison leaves is
     * the one `CIRCLE2` starts its first multiply on.
     */
    std::uint8_t step = 8;
    bool carry = _radius >= 8u;
    if (carry)
    {
      step >>= 1;
      carry = _radius >= 60u;
      if (carry)
      {
        step >>= 1;
      }
    }
    _state.circleStep = step;

    DrawBall(_canvas, _state, _geometry, _math, _clip, _centre, _radius, carry, _picture);
    return false;
  }

  AxisResult DivideAxisByZ(const Ship& _ship, MathWorkspace& _math, std::uint8_t _at) noexcept
  {
    // 6502: PLS1 -- the component's two bytes with the sign split off. The index is 9, 11, 21 or
    // 23: one COMPONENT of an orientation vector, whose sign is bit 7 of its high byte -- not a
    // position axis, whose sign has a byte of its own.
    const SignMag16& component = _ship.ComponentAt(_at);
    const KBlock quotient = DivideByShipZ(
      _ship, _math,
      SignMag24{component.lo, static_cast<std::uint8_t>(component.hi & 0x7Fu), static_cast<std::uint8_t>(component.hi & 0x80u)});

    /*
     * 6502: the quotient's low byte, replaced by 254 when the middle byte is not zero.
     *
     * The branch skips that replacement, so a result which fits in a byte comes back as itself and
     * anything larger SATURATES rather than wrapping. A planet close enough for the division to
     * overflow is one whose markings run off the disc, and 254 is what keeps them there.
     */
    std::uint8_t value = quotient.low;
    if (quotient.mid != 0u)
    {
      value = 254;
    }

    return {value, quotient.top, static_cast<std::uint8_t>(_at + 2u)};
  }

  AxisResult ScaleAxisByZ(const Ship& _ship, MathWorkspace& _math, std::uint8_t _at) noexcept
  {
    // 6502: PLS3 -- PLS1, then * 222/256, and X is SAVED rather than stepped because the caller
    // wants to divide the same axis twice.
    const AxisResult axis = DivideAxisByZ(_ship, _math, _at);

    // 6502: the index is saved AFTER the call, and `PLS1` has already stepped it twice -- so what
    // comes back is the STEPPED index, not the one this call was given. `PL26` calls this twice in
    // a row without touching the index in between and gets two different axes (§6.53).
    const std::uint8_t stepped = axis.at;

    // 6502: MULTU scales the axis by 222/256.
    const std::uint8_t scaled = MultiplyUnsigned(axis.value, 222).high;

    // 6502: PL12 -- a positive axis returns as it is with a zero high byte. The sign is what `PLS1`
    // handed back, which is `AxisResult::sign` since slice 3b.
    if ((axis.sign & 0x80u) == 0u)
    {
      return {scaled, 0, stepped};
    }

    // 6502: a negative one is negated into a sixteen-bit value whose high half is 255. Negating
    // zero gives zero, which needs a high half of ZERO, and the branch is what catches it.
    const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(scaled ^ 0xFFu), 1u, false);
    if (negated.value == 0u)
    {
      return {negated.value, 0, stepped};
    }

    return {negated.value, 0xFF, stepped};
  }

  std::uint8_t SetMeridianAngle(const Ship& _ship, std::uint8_t _numerator, std::uint8_t _denominator) noexcept
  {
    // 6502: PLS4 -- the arctangent, then the roof vector's sign decides which way round.
    std::uint8_t angle = Arctan(_numerator, _denominator);

    // 6502: the branch SKIPS the flip, so it is the POSITIVE roof vector that gets it.
    if ((_ship.nose.z.hi & 0x80u) == 0u)
    {
      angle = static_cast<std::uint8_t>(angle ^ 0x80u);
    }

    // Two shifts: a byte turn becomes a sixty-fourth, which is what the ellipse walk counts in.
    return static_cast<std::uint8_t>(angle >> 2); // 6502: CNT2
  }

  std::pair<std::uint8_t, std::uint8_t> LoadTwoAxes(const Ship& _ship, MathWorkspace& _math, GeometryWorkspace& _geometry,
                                                    std::uint8_t _at) noexcept
  {
    // 6502: PLS5 -- two of PLS1 into the second half of the ellipse's axes.
    AxisResult axis = DivideAxisByZ(_ship, _math, _at);
    const std::uint8_t second = axis.value; // 6502: the first of the pair
    _geometry.scaledOrientation[2] = axis.sign;

    axis = DivideAxisByZ(_ship, _math, axis.at);
    _geometry.scaledOrientation[3] = axis.sign;
    return {second, axis.value}; // 6502: the ellipse's two scaled axes
  }

  void DrawEllipse(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                   const Projection& _centre, EllipseAxes _axes, std::uint8_t _angle, std::uint8_t _target, Picture* _picture) noexcept
  {
    // 6502: PLS22 -- the counter zeroed and the flag set. `CNT` is `BLINE`'s segment counter and
    // `CNT2` the angle this walk is at; both are locals since M2-c-3, and `CNT2` comes in as the
    // start `PLS4` or `PL26` chose while `TGT` comes in as where to stop.
    std::uint8_t atAngle = 0; // 6502: CNT
    std::uint8_t coneWidth = _angle;
    _state.flag = 0xFF;

    for (;;)
    {
      /*
       * 6502: PLL4 -- the same quarter-wave table twice, a quarter-turn apart, and each product
       * scaled by one of the ellipse's two axes. That is what makes it an ellipse rather than a
       * circle: `K2(3 2)` is how far the meridian reaches across and `K2(1 0)` how far it reaches
       * down, and a meridian seen edge-on has one of them at zero.
       */
      // 6502: the first axis against the sine, both halves; `R` and `K` were the scratch they
      // waited in.
      const std::uint8_t sine = SINE_TABLE[coneWidth & 0x1Fu];
      const std::uint8_t firstAcross = MultiplyByLog(_axes.secondX, sine, false).value;
      const std::uint8_t secondAcross = MultiplyByLog(_axes.secondY, sine, false).value;

      // 6502: the sign for this quarter, taken as a bit rotated straight out of the comparison
      // against 33 rather than branched on.
      _geometry.scaledOrientation[5] = (coneWidth >= 33u) ? 0x80u : 0x00u;

      // 6502: the same table a quarter-turn on -- the cosine -- against the second axis. `K+2` and
      // `P` were the scratch these waited in.
      const AddResult quarter = AddWithCarry(coneWidth, 16u, false);
      const std::uint8_t cosine = SINE_TABLE[quarter.value & 0x1Fu];
      const std::uint8_t secondDown = MultiplyByLog(_axes.firstY, cosine, false).value;
      const LogProduct second = MultiplyByLog(_axes.firstX, cosine, false);

      /*
       * 6502: the same sign again for the cosine's quarter -- the angle stepped fifteen, masked
       * back into a turn, and compared against 33.
       *
       * The step runs on FMLTU's exit carry exactly as `CIRCLE2`'s does (§6.50), and the rotate
       * turns the comparison straight into a sign bit: 128 when the comparison SET the carry, which
       * is when the value reached 33. Getting that round the wrong way puts every meridian's second
       * axis on the wrong side of the planet.
       */
      const AddResult stepped = AddWithCarry(coneWidth, 15u, second.carry);
      _geometry.scaledOrientation[4] = (static_cast<std::uint8_t>(stepped.value & 0x3Fu) >= 33u) ? 0x80u : 0x00u;

      // 6502: the two `ADD`s, each combining a product with the axis sign it belongs to: (A P) is
      // the first-axis sign over the cosine product, (S R) the second-axis sign over the sine's.
      AddSignedResult sum = AddSigned(SignMag16{second.value, static_cast<std::uint8_t>(_geometry.scaledOrientation[4] ^ _geometry.scaledOrientation[0])},
                                      SignMag16{firstAcross, static_cast<std::uint8_t>(_geometry.scaledOrientation[5] ^ _geometry.scaledOrientation[2])});
      std::uint8_t offsetHigh = sum.high; // 6502: T -- this loop's own since M2-c-3
      std::uint8_t low = sum.low;
      bool carry = sum.carry; // 6502: the store and the branch touch no flag, so ADD's carry survives

      // 6502: a positive total branches straight past this; a negative one is negated into a
      // sixteen-bit value first, the same block `CIRCLE2` has twice.
      if ((sum.high & 0x80u) != 0u)
      {
        const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(low ^ 0xFFu), 1u, false);
        low = negated.value;
        const AddResult raised = AddWithCarry(static_cast<std::uint8_t>(sum.high ^ 0x7Fu), 0u, negated.carry);
        offsetHigh = raised.value;
        carry = raised.carry;
      }

      // 6502: PL42 -- the centre added on.
      const AddResult xLow = AddWithCarry(low, _centre.x, carry);
      _state.segmentEnd[0] = xLow.value;
      _state.segmentEnd[1] = AddWithCarry(offsetHigh, _centre.x1, xLow.carry).value;

      // 6502: the other pair of products, each half parked in the scratch `ADD` reads.
      sum = AddSigned(SignMag16{secondDown, static_cast<std::uint8_t>(_geometry.scaledOrientation[4] ^ _geometry.scaledOrientation[1])},
                      SignMag16{secondAcross, static_cast<std::uint8_t>(_geometry.scaledOrientation[5] ^ _geometry.scaledOrientation[3])});
      offsetHigh = static_cast<std::uint8_t>(sum.high ^ 0x80u);
      low = sum.low;
      carry = sum.carry; // 6502: the flip, the store and the branch again touch no flag

      if ((offsetHigh & 0x80u) != 0u)
      {
        const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(low ^ 0xFFu), 1u, false);
        low = negated.value;
        const AddResult raised = AddWithCarry(static_cast<std::uint8_t>(offsetHigh ^ 0x7Fu), 0u, negated.carry);
        offsetHigh = raised.value;
        carry = raised.carry;
      }

      // 6502: PL43 -- and the segment, with the y offset in X.
      const std::uint8_t reached =
        DrawBallLine(_canvas, _state, _geometry, _math, _clip, _centre, SignMag16{low, offsetHigh}, atAngle, carry, _picture);
      atAngle = reached;

      // 6502: the equality test in front of the greater-or-equal one is what makes the last step
      // INCLUSIVE, so a meridian reaching exactly 31 draws its final segment and a crater reaching
      // 64 draws its.
      if (reached != _target && reached >= _target)
      {
        return;
      }

      coneWidth = static_cast<std::uint8_t>((coneWidth + _state.circleStep) & 0x3Fu);
    }
  }

  void DrawHalfEllipse(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                       const Projection& _centre, EllipseAxes _axes, std::uint8_t _angle, Picture* _picture) noexcept
  {
    // 6502: PLS2 -- a target of 31, then straight into PLS22. Half a turn, because a meridian seen
    // from outside is a semicircle and the other half is behind the planet.
    DrawEllipse(_canvas, _state, _geometry, _math, _clip, _centre, _axes, _angle, 31, _picture);
  }

  void DrawPlanetDetail(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                        const Ship& _ship, Projection& _centre, KBlock _radius, ShipType _type, Picture* _picture) noexcept
  {
    // 6502: PL9 -- rub out last frame's planet, draw this frame's outline, and only then think
    // about the markings.
    EraseBall(_canvas, _state, _picture);

    if (DrawCircle(_canvas, _state, _geometry, _math, _clip, _centre, _radius.low, _picture))
    {
      return; // 6502: PL20 -- CHKON refused it
    }

    // 6502: a radius whose middle byte is set needed two bytes, which is a planet filling the
    // screen, and its markings would be off it. `K` is a `KBlock` value since M2-c-3, so the `PLS1`
    // divides below no longer step on the byte this test reads -- in the original they do, and this
    // test comes first for that reason.
    if (_radius.mid != 0u)
    {
      return;
    }

    // 6502: a zero in `PLTOG`, the detail switch, skips the markings -- and nothing in this build
    // ever writes it.
    if (_state.planetDetail == 0u)
    {
      return;
    }

    if (_type == ShipType::Planet)
    {
      /*
       * 6502: part 2 -- MERIDIANS. Two great circles at right angles, each drawn as a half
       * ellipse whose axes are the planet's own orientation vectors projected onto the screen.
       *
       * Under six pixels across there is nothing to draw them on, so the radius is tested first.
       */
      if (_radius.low < 6u)
      {
        return;
      }

      // 6502: PLS4 -- where the first meridian starts: the nose's z with its sign flipped, over
      // the roof's.
      std::uint8_t meridian = SetMeridianAngle(_ship, static_cast<std::uint8_t>(_ship.nose.z.hi ^ 0x80u), _ship.roof.z.hi);

      EllipseAxes axes;
      AxisResult axis = DivideAxisByZ(_ship, _math, 9);
      axes.firstX = axis.value;
      _math.k2Low = axis.value; // 6502: into K2, and `MV40` reads this byte a frame later (§8)
      _geometry.scaledOrientation[0] = axis.sign;

      axis = DivideAxisByZ(_ship, _math, axis.at);
      axes.firstY = axis.value;
      _geometry.scaledOrientation[1] = axis.sign;

      std::tie(axes.secondX, axes.secondY) = LoadTwoAxes(_ship, _math, _geometry, 15);
      DrawHalfEllipse(_canvas, _state, _geometry, _math, _clip, _centre, axes, meridian);

      // And the second meridian, which shares the first pair of axes and takes a new second pair.
      meridian = SetMeridianAngle(_ship, static_cast<std::uint8_t>(_ship.nose.z.hi ^ 0x80u), _ship.side.z.hi);

      std::tie(axes.secondX, axes.secondY) = LoadTwoAxes(_ship, _math, _geometry, 21);
      DrawHalfEllipse(_canvas, _state, _geometry, _math, _clip, _centre, axes, meridian);
      return;
    }

    /*
     * 6502: PL26 -- a CRATER. One whole ellipse, offset from the planet's centre along its own
     * ROOF vector, so it slides round the disc as the planet turns and disappears over the edge.
     *
     * The roof's z decides whether it is drawn at all: pointing away means the crater is on the far
     * side. Both that test and the offset below come off index 15, which `Ship.h` pins to `roofv`
     * -- the prose here read "nose" until M6-d-17.
     */
    if ((_ship.roof.z.hi & 0x80u) != 0u)
    {
      return;
    }

    // 6502: PLS3 from the roof's x -- the offset, one axis at a time, ADDED to x and SUBTRACTED from y.
    AxisResult offset = ScaleAxisByZ(_ship, _math, 15);
    const AddResult acrossLow = AddWithCarry(offset.value, _centre.x, false);
    _centre.x = acrossLow.value;
    _centre.x1 = AddWithCarry(offset.sign, _centre.x1, acrossLow.carry).value;

    // 6502: `P` parks each half of the offset for the one instruction that takes it back out of
    // `K4`. `PL26`'s own since M2-c-3, and the last of `P`'s users.
    offset = ScaleAxisByZ(_ship, _math, offset.at);
    const SubResult downLow = SubtractWithCarry(_centre.y, offset.value, true);
    _centre.y = downLow.value;
    _centre.y1 = SubtractWithCarry(_centre.y1, offset.sign, downLow.carry).value;

    /*
     * 6502: four PLS1s, each HALVED before it is stored.
     *
     * The halving is what makes the crater smaller than the planet, and it is a shift of the
     * magnitude alone -- the sign in Y is untouched, so a negative axis halves towards zero rather
     * than away from it.
     */
    EllipseAxes axes;
    AxisResult axis = DivideAxisByZ(_ship, _math, 9);
    axes.firstX = static_cast<std::uint8_t>(axis.value >> 1);
    _math.k2Low = axes.firstX; // 6502: into K2 again -- `MV40`'s byte (§8)
    _geometry.scaledOrientation[0] = axis.sign;

    axis = DivideAxisByZ(_ship, _math, axis.at);
    axes.firstY = static_cast<std::uint8_t>(axis.value >> 1);
    _geometry.scaledOrientation[1] = axis.sign;

    axis = DivideAxisByZ(_ship, _math, 21);
    axes.secondX = static_cast<std::uint8_t>(axis.value >> 1);
    _geometry.scaledOrientation[2] = axis.sign;

    axis = DivideAxisByZ(_ship, _math, axis.at);
    axes.secondY = static_cast<std::uint8_t>(axis.value >> 1);
    _geometry.scaledOrientation[3] = axis.sign;

    // 6502: into PLS22 with a target of 64 from an angle of zero -- a whole turn.
    DrawEllipse(_canvas, _state, _geometry, _math, _clip, _centre, axes, 0, 64);
  }

  void DrawPlanetOrSun(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                       Rng& _rng, const Ship& _ship, Projection& _centre, ShipType _type, Picture* _picture) noexcept
  {
    /*
     * 6502: PLANET -- three rejections before any arithmetic.
     *
     * A z sign byte of 48 or more is "further away than the sign byte can usefully say", and a
     * sign byte and high byte that are both zero is "no distance at all", which is the case
     * `DVID3B2`'s divide cannot take. Both go to `PL2`, so a rejected planet is ERASED rather than
     * merely skipped -- which is why flying away from one leaves no outline behind.
     */
    if (_ship.z.sgn >= 48u || (_ship.z.sgn | _ship.z.hi) == 0u)
    {
      ErasePlanetOrSun(_canvas, _state, _type, _picture);
      return;
    }

    if (Project(_ship, _math, _centre).offScreen)
    {
      ErasePlanetOrSun(_canvas, _state, _type, _picture);
      return;
    }

    // 6502: the radius is 96 * 256 / z, and 96 is the planet's size in the same units everything
    // else in the geometry uses. `K` is the `KBlock` the divide returns since M2-c-3.
    KBlock radius = DivideByShipZ(_ship, _math, SignMag24{0, 96, 0});

    // 6502: a radius that overflowed a byte is clamped to 248, and the middle byte is LEFT AS IT
    // WAS, which is what `PL9` reads to skip the markings.
    if (radius.mid != 0u)
    {
      radius.low = 248;
    }

    // 6502: an ODD type is the sun and an even one the planet -- bit 0 shifted out into the carry
    // and branched on. The two share this routine down to here and nothing below it.
    if ((Byte(_type) & 0x01u) != 0u)
    {
      DrawSun(_canvas, _state, _math, _rng, _centre, radius.low, _picture);
      return;
    }

    DrawPlanetDetail(_canvas, _state, _geometry, _math, _clip, _ship, _centre, radius, _type, _picture);
  }

  void DrawSun(Canvas& _canvas, PlanetSunState& _state, MathWorkspace& _math, Rng& _rng, const Projection& _centre,
               std::uint8_t _radius, Picture* _picture) noexcept
  {
    // 6502: entry 0 stops being the "nothing there" flag the moment the routine commits to
    // drawing, so a `WPLS` interrupted halfway still has something to erase.
    _state.sun[0] = 1;

    const CircleExtent extent = CircleOffScreen(_state, _radius, _centre);
    if (extent.offScreen)
    {
      // 6502: nothing of it is on screen, so only rub out the old one.
      EraseSun(_canvas, _state, _picture);
      return;
    }

    /*
     * 6502: `CNT`, the roughness mask, built out of the radius rather than looked up.
     *
     * Three comparisons rolled into three bits, so `CNT` is 0, 1, 3 or 7 -- the mask the random
     * byte is ANDed with before it is added to each row's half-width. A distant sun is a smooth
     * disc and a near one has a ragged edge, and this is the whole of that effect.
     */
    std::uint8_t roughness = 0; // 6502: CNT -- `SUN`'s own since M2-c-3
    roughness = static_cast<std::uint8_t>((roughness << 1) | ((_radius >= 96u) ? 1u : 0u));
    roughness = static_cast<std::uint8_t>((roughness << 1) | ((_radius >= 40u) ? 1u : 0u));
    roughness = static_cast<std::uint8_t>((roughness << 1) | ((_radius >= 16u) ? 1u : 0u));

    /*
     * 6502: PLF18 -- where to stop. `CHKON` left the circle's top and bottom in `P+1` and `P+2`,
     * and the bottom row of the sun is whichever of that and the screen's own bottom comes first.
     * A sun whose top is at row 0 is given a `TGT` of 1 rather than 0, because row 0 is the flag.
     */
    std::uint8_t stopAt = _state.lowestVisibleRow; // 6502: TGT -- `SUN`'s own too
    if (extent.bottomHigh == 0u && _state.lowestVisibleRow >= extent.bottom)
    {
      stopAt = (extent.bottom != 0u) ? extent.bottom : std::uint8_t{1};
    }

    /*
     * 6502: how far the bottom visible row is from the sun's centre, as a sixteen-bit subtraction.
     * That is where the walk starts.
     */
    const SubResult offsetLow = SubtractWithCarry(_state.lowestVisibleRow, _centre.y, true);
    std::uint8_t at = offsetLow.value;
    const SubResult offsetHigh = SubtractWithCarry(0u, _centre.y1, offsetLow.carry);

    std::uint8_t sign = 0;
    if ((offsetHigh.value & 0x80u) != 0u)
    {
      // 6502: PLF3 -- the centre is BELOW the bottom row, so the walk starts above it and the
      // offset is negated. `V+1` becomes 255, which is what `PLF10` reads as "still climbing".
      at = AddWithCarry(static_cast<std::uint8_t>(at ^ 0xFFu), 1u, false).value;
      sign = 0xFF;
    }
    else if (offsetHigh.value != 0u)
    {
      // 6502: PLF4 -- further than a byte, so start at the sun's own edge.
      at = _radius;
      sign = 0;
    }
    else if (at == 0u)
    {
      sign = 0xFF; // 6502: PLF17 -- exactly on the centre
    }
    else if (at >= _radius)
    {
      at = _radius; // 6502: the branch to PLF5 not taken, so PLF4
      sign = 0;
    }

    // 6502: PLF5 -- the distance from the centre and its sign, stored where `PLF6` reads them.
    _state.v = at;
    _state.vNext = sign;

    // 6502: the radius squared, which every row's half-width is a square root of. `SUN`'s own
    // since M2-c-3 except for the store into `K2`, which is there for `MV40`'s read a frame later
    // (§8).
    const Product radiusSquared = SquareUnsigned(_radius);
    _math.k2Low = radiusSquared.low;

    // 6502: part 2 -- rub out the rows BELOW the sun, with last frame's centre, before any of
    // this frame's arithmetic touches `YY`.
    std::uint8_t row = _state.lowestVisibleRow;
    const SignMag16 wasAt{_state.sunX, _state.sunXNext}; // 6502: YY(1 0) -- where the sun was
    const SignMag16 isAt{_centre.x, _centre.x1};         // 6502: YY(1 0) again -- and where it is

    while (row != stopAt && row != 0u)
    {
      if (_state.sun[row] != 0u)
      {
        EraseSunRow(_canvas, _state, wasAt, _state.sun[row], row, _picture);
      }
      --row;
    }

    /*
     * 6502: PLFL -- the body, one screen row at a time.
     *
     * `PLF6` is the loop's tail and it does three things at once: step the row, step the distance
     * from the centre, and notice when the walk has gone past the sun's other edge. The distance
     * counts DOWN while the row is below the centre and UP once it is above, which is what the
     * sign byte in `V+1` is for.
     */
    /*
     * 6502: `PLF6`'s row-zero exit leaves through the ROUTINE'S TAIL and not through part 4, so a
     * sun that reaches the top of the screen does not get the rows above it erased -- there are
     * none. The other exit, `PLF10`'s test against the radius, falls into part 4 because there are.
     */
    bool eraseAbove = false;

    for (;;)
    {
      // 6502: the half-width, as sqrt(K^2 - v^2). `T` held the square's high byte and (R Q) the
      // difference, which is the radicand `LL5` takes.
      const Product vSquared = SquareUnsigned(_state.v);
      const SubResult widthLow = SubtractWithCarry(radiusSquared.low, vSquared.low, true);
      const std::uint8_t widthHigh = SubtractWithCarry(radiusSquared.high, vSquared.high, widthLow.carry).value;

      const Root root = SquareRoot(widthHigh, widthLow.value);
      _math.lastDivisor = root.value; // 6502: what LL5 leaves in Q -- and the last row's root is the frame's Q when the sun is the last slot drawn

      // 6502: the ragged edge -- a random byte masked by `CNT` and added to the half-width -- and
      // it saturates at 255 rather than wrapping round to nothing. The generator runs on the carry
      // `LL5` left, which is the last bit out of the square root (§6.55).
      const RngResult roll = _rng.Next(root.carry);
      const AddResult ragged = AddWithCarry(static_cast<std::uint8_t>(roll.value & roughness), root.value, false);
      std::uint8_t width = ragged.value;
      if (ragged.carry)
      {
        width = 255;
      }

      // 6502: PLF44 -- the old width is read out of the heap and the new one stored in its place,
      // and what gets drawn is the DIFFERENCE between the two lines rather than both of them.
      const std::uint8_t was = _state.sun[row];
      _state.sun[row] = width;

      if (was != 0u)
      {
        // The old line, clipped against LAST frame's centre. Its two ends go in `XX(1 0)`, which
        // `PLF23` reads back -- two locals of this loop since M2-c-3.
        const SunRow old = ClipSunRow(_state, wasAt, was, row);
        std::uint8_t sliverFrom = old.x1;     // 6502: XX
        const std::uint8_t sliverTo = old.x2; // 6502: XX+1

        // And the new one, against this frame's.
        const SunRow fresh = ClipSunRow(_state, isAt, _state.sun[row], row);

        if (!fresh.offScreen)
        {
          // 6502: the two ends CROSSED OVER, so what is drawn is one end of the old line to the
          // matching end of the new one -- the sliver that has appeared or gone.
          const std::uint8_t held = fresh.x2;
          DrawHorizontalLine(_canvas, fresh.x1, sliverFrom, row);
          if (_picture != nullptr)
          {
            DrawCanvasRow2x(*_picture, fresh.x1, sliverFrom, row);
          }
          sliverFrom = held;
        }

        // 6502: PLF23 -- and the other sliver.
        DrawHorizontalLine(_canvas, sliverFrom, sliverTo, row);
        if (_picture != nullptr)
        {
          DrawCanvasRow2x(*_picture, sliverFrom, sliverTo, row);
        }
      }
      else
      {
        // 6502: PLF11 -- nothing was there last frame, so the whole of the new line is drawn.
        const SunRow fresh = ClipSunRow(_state, isAt, _state.sun[row], row);
        if (fresh.offScreen)
        {
          _state.sun[row] = 0;
        }
        else
        {
          DrawHorizontalLine(_canvas, fresh.x1, fresh.x2, row);
          if (_picture != nullptr)
          {
            DrawCanvasRow2x(*_picture, fresh.x1, fresh.x2, row);
          }
        }
      }

      // 6502: PLF6 -- the row steps up, and row zero ends the walk through the routine's tail.
      --row;
      if (row == 0u)
      {
        break;
      }

      if (_state.vNext != 0u)
      {
        // 6502: PLF10 -- above the centre, so the distance GROWS, and once it passes the radius
        // there is no more sun below and part 4 takes over.
        const std::uint8_t next = static_cast<std::uint8_t>(_state.v + 1u);
        _state.v = next;
        if (next > _radius)
        {
          eraseAbove = true;
          break;
        }
      }
      else
      {
        /*
         * 6502: below the centre, so the distance SHRINKS as the walk climbs.
         *
         * The decrement is UNCONDITIONAL and the branch only decides whether the high byte follows
         * it down. So V reaching zero is what flips the walk from "coming in towards the centre"
         * to "going out the other side", and it does it by making V+1 negative rather than by
         * testing anything.
         */
        --_state.v;
        if (_state.v == 0u)
        {
          --_state.vNext;
        }
      }
    }

    // 6502: part 4 -- rub out whatever is left above the sun, again with last frame's centre.
    if (eraseAbove)
    {
      while (row != 0u)
      {
        if (_state.sun[row] != 0u)
        {
          EraseSunRow(_canvas, _state, wasAt, _state.sun[row], row, _picture);
        }
        --row;
      }
    }

    // 6502: PLF8 -- and this frame's centre becomes next frame's.
    _state.sunX = _centre.x;
    _state.sunXNext = _centre.x1;
  }

  void ClearShip(Ship& _work) noexcept
  {
    // 6502: ZINF -- every byte of the block zeroed, top down.
    _work = Ship{};

    /*
     * 6502: and then three bytes go back in, 96 in each and the third with its top bit set.
     *
     * The three high bytes of `roofv_y`, `sidev_x` and `nosev_z`, so the ship comes out square to
     * the axes -- and the sign on the nose is what makes it face TOWARDS the player. 96 rather than
     * 127 because the orientation vectors are unit vectors at a scale of 96, which is the same 96
     * `PLANET` divides by for its radius.
     */
    _work.roof.y.hi = 96;
    _work.side.x.hi = 96;
    _work.nose.z.hi = static_cast<std::uint8_t>(96u | 0x80u);
  }

  void SeedStardustField(Canvas& _canvas, Stardust& _dust, Rng& _rng, bool _carryIn, Picture* _picture) noexcept
  {
    /*
     * 6502: nWq -- three random bytes per speck, and the generator is threaded straight through.
     *
     * The plot comes before the next call to the generator, so each speck's first random byte runs
     * on the carry the PREVIOUS speck's plot left -- which is `ZZ >= 80`, the distance test inside
     * `PIXEL` (§6.57). The field a fresh view is filled with therefore depends on where the last
     * speck was drawn.
     *
     * Forcing bit 3 of the distance keeps every speck at least eight units away, so none of them starts
     * on the player's face.
     */
    bool carry = _carryIn;

    for (std::uint8_t at = _dust.count; at != 0u; --at)
    {
      RngResult roll = _rng.Next(carry);
      const std::uint8_t distance = static_cast<std::uint8_t>(roll.value | 8u); // 6502: ZZ
      _dust.z[at] = distance;

      roll = _rng.Next(roll.carry);
      _dust.x[at] = roll.value; // 6502: SX,Y and X1, both
      const std::uint8_t x1 = roll.value;

      roll = _rng.Next(roll.carry);
      _dust.y[at] = roll.value; // 6502: SY,Y and Y1, both
      const std::uint8_t y1 = roll.value;

      carry = PlotRelativePixel(_canvas, x1, y1, distance);
      if (_picture != nullptr)
      {
        // A fresh field has no fractions of its own -- `nWq` writes `SX` and `SY` and leaves `SXL`
        // and `SYL` as they were -- so the wide mark takes the two bytes as they stand, which is
        // what the first mover frame's erase will read back (Resolution.md section 4.3).
        PlotRelativePixel2x(*_picture, x1, y1, _dust.xLow[at], _dust.yLow[at], distance);
      }
    }
  }

  void ClearAllShips(Canvas& _canvas, PlanetSunState& _state, Bubble& _bubble, Ship& _work, FlightState& _flight, std::uint8_t _view,
                     Picture* _picture) noexcept
  {
    // 6502: WPSHPS -- the slot list walked from the start, with the two cases the body handles.
    for (std::size_t slot = 0; slot < _bubble.slots.size(); ++slot)
    {
      const ShipType type = TypeOf(_bubble.slots[slot]);
      if (type == ShipType::None)
      {
        break; // 6502: WS2 -- the first empty slot ends the list
      }
      if (IsBody(type))
      {
        continue; // 6502: WS1 -- the planet and the sun have no blip and no line heap
      }

      // 6502: thirty-two bytes copied out of the slot, not the whole block: the AI byte, the heap
      // pointer, the energy and NEWB keep what `INWK` held.
      std::array<std::uint8_t, SHIP_BLOCK_SIZE> bytes = _work.ToBytes();
      const std::array<std::uint8_t, SHIP_BLOCK_SIZE> from = _bubble.blocks[slot].ToBytes();
      std::copy_n(from.begin(), 32u, bytes.begin());
      _work = Ship::FromBytes(bytes);

      // 6502: both stores are the routine's own and were invisible while the scanner was a seam:
      // `SCAN` reads `TYPE` as a global, and `XSAV` is how the loop index survives the call.
      _flight.type = type;
      _flight.slot = static_cast<std::uint8_t>(slot);
      DrawScannerBlip(_canvas, _work, type, _view, _picture);

      /*
       * 6502: the state byte read back through the slot pointer, masked, and put where it came from.
       *
       * It masks the byte in the SLOT and not the copy in `INWK`, so the two disagree the moment
       * this returns -- which is correct, because the caller is about to redraw everything from
       * the slots. The mask clears bits 3, 4 and 6: "drawn on screen", "firing a laser", and the
       * one in between.
       */
      _bubble.blocks[slot].state =
        Without(_bubble.blocks[slot].state, ShipStateBit::OnScreen, ShipStateBit::OnScanner, ShipStateBit::Firing);
    }

    // 6502: WS2 -- the heap pointer to zero and 255 into entry 0 of both halves. Note `LSP` goes
    // to ZERO here and to one in `WP1`; the two are not the same reset.
    _state.ballHeapTop = 0;
    _state.SetBallX(0, 0xFF);
    _state.SetBallY(0, 0xFF);

    // 6502: and the fall-through into FLFLLS.
    ClearSunHeap(_state);
  }

  void SeedStardustAndClearShips(Canvas& _canvas, Stardust& _dust, Rng& _rng, PlanetSunState& _state, Bubble& _bubble, Ship& _work,
                                 FlightState& _flight, std::uint8_t _view, bool _carryIn, Picture* _picture) noexcept
  {
    // 6502: NWSTARS -- `QQ11` is the view, zero for the space view, and a menu has no stardust to
    // fill, so any other view skips straight to the ships. The same byte then decides whether
    // `SCAN` draws anything.
    if (_view == 0u)
    {
      SeedStardustField(_canvas, _dust, _rng, _carryIn, _picture);
    }

    ClearAllShips(_canvas, _state, _bubble, _work, _flight, _view, _picture);
  }

  void DrawHyperspaceRing(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                          const Projection& _centre, std::uint8_t _index, Presenter& _present, Picture* _picture) noexcept
  {
    // 6502: HFL1 -- the ring's starting radius, the low three bits of the index plus eight, and
    // this routine's own since M2-c-3: it fills `K` and nothing else reads the block while it runs.
    std::uint8_t radius = static_cast<std::uint8_t>((_index & 7u) + 8u);

    for (;;)
    {
      /*
       * 6502: HFL2 -- the ring loop's top.
       *
       * The heap is rewound to one before every circle, so each ring is drawn over the last one's
       * run rather than after it -- and because `BLINE` EORs, drawing the next size erases the
       * previous. The whole effect is one heap entry deep.
       */
      _state.ballHeapTop = 1u;
      DrawBall(_canvas, _state, _geometry, _math, _clip, _centre, radius, false, _picture);

      /*
       * Not in the 6502, and it is the display's absence rather than an addition to the routine.
       * The VIC-II was showing this circle while the next one was being computed; here nothing is
       * showing anything until somebody presents, so the pacing goes where the machine's own
       * pause was -- between one circle and the one that erases it.
       */
      _present.Present();

      // 6502: a radius past 128 doubles out of the byte, and the carry that falls out ends the ring.
      const ShiftResult doubled = RotateLeftValue(radius, false);
      radius = doubled.value;

      if (doubled.carry)
      {
        return;
      }

      // 6502: 160 is half the screen's width, so a ring is abandoned once it is wider than the
      // view rather than once it is off it.
      if (radius >= 160u)
      {
        return;
      }
    }
  }

  void DrawHyperspaceRings(Canvas& _canvas, PlanetSunState& _state, GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                           Presenter& _present, Picture* _picture) noexcept
  {
    // 6502: the centre of the space view as a sixteen-bit pair, and the ring counter zeroed.
    Projection centre{};
    centre.x = SPACE_VIEW_CENTRE_X;
    centre.y = SPACE_VIEW_CENTRE_Y;
    centre.x1 = 0;
    centre.y1 = 0;

    // 6502: HFL5 -- eight rings, and `XX4` is this loop's counter. Nothing reads it afterwards:
    // `LL9` part 1 writes 31 into it before its first read, so the eight the loop leaves is dead.
    // A local since M2-c-3, with `LL9`'s own four.
    for (std::uint8_t index = 0; index < 8u; ++index)
    {
      DrawHyperspaceRing(_canvas, _state, _geometry, _math, _clip, centre, index, _present, _picture);
    }
  }

} // namespace Elite
