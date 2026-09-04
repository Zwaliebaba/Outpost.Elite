#include "pch.h"

#include "PlanetDraw.h"

#include "EliteTypes.h"

namespace Elite
{

bool ClipSunRow(PlanetSunState& _state, MathWorkspace& _math, DrawWorkspace& _draw,
                std::uint8_t _a, std::uint8_t _row) noexcept
{
  // 6502: EDGES -- STA T / CLC / ADC YY / STA X2, the right-hand end first.
  _math.t = _a;

  const AddResult right = AddWithCarry(_a, _math.yy, false);
  _draw.x2 = right.value;
  const AddResult rightHigh = AddWithCarry(_math.yyNext, 0u, right.carry);

  if ((rightHigh.value & 0x80u) != 0u)
  {
    // 6502: ED1 -- even the right-hand end is off the left of the screen.
    if (_row < _state.sun.size())
    {
      _state.sun[_row] = 0;
    }
    return true;
  }

  // 6502: BEQ P%+6 -- a high byte of zero means X2 is already a screen coordinate. Anything
  // else positive means the line runs off the right, so it is clamped.
  if (rightHigh.value != 0u)
  {
    _draw.x2 = 255;
  }

  const SubResult left = SubtractWithCarry(_math.yy, _math.t, true);
  _draw.x1 = left.value;
  const SubResult leftHigh = SubtractWithCarry(_math.yyNext, 0u, left.carry);

  if (leftHigh.value == 0u)
  {
    return false; // 6502: CLC / RTS -- both ends are on screen as they stand.
  }

  // 6502: ED3.
  if ((leftHigh.value & 0x80u) == 0u)
  {
    // Positive and non-zero: the whole line is off the right.
    if (_row < _state.sun.size())
    {
      _state.sun[_row] = 0;
    }
    return true;
  }

  _draw.x1 = 0;
  return false;
}


void EraseSunRow(Canvas& _canvas, PlanetSunState& _state, MathWorkspace& _math,
                 DrawWorkspace& _draw, std::uint8_t _a, std::uint8_t _row) noexcept
{
  // 6502: HLOIN2 -- JSR EDGES / STY Y1 / LDA #0 / STA LSO,Y / JMP HLOIN. The carry is dropped.
  (void)ClipSunRow(_state, _math, _draw, _a, _row);

  _draw.y1 = _row;
  if (_row < _state.sun.size())
  {
    _state.sun[_row] = 0;
  }

  DrawHorizontalLine(_canvas, _draw);
}


void ClearSunHeap(PlanetSunState& _state) noexcept
{
  // 6502: FLFLLS -- LDY #199 / LDA #0 / .SAL6 STA LSO,Y / DEY / BNE SAL6 / DEY / STY LSX / RTS.
  //
  // The loop stops at entry 1, so entry 0 is never zeroed -- and then `DEY` takes Y to 255 and
  // `STY LSX` puts that in it. One byte, two meanings, and the loop bound is what keeps them
  // apart.
  for (std::size_t row = _state.sun.size() - 1u; row != 0u; --row)
  {
    _state.sun[row] = 0;
  }
  _state.sun[0] = 0xFF;
}


void ClearBallHeap(PlanetSunState& _state) noexcept
{
  // 6502: WP1 -- LDA #1 / STA LSP / LDA #&FF / STA LSX2 / RTS.
  _state.lsp = 1;
  _state.SetBallX(0, 0xFF);
}


void EraseSun(Canvas& _canvas, PlanetSunState& _state, MathWorkspace& _math,
              DrawWorkspace& _draw) noexcept
{
  // 6502: WPLS -- LDA LSX / BMI WPLS-1, and that byte is `WP1`'s own `RTS`. One of the six
  // backward label-with-offset targets §6.35 counted that land in the file BEFORE the one
  // naming them, and the second of them to be confirmed by building it.
  if ((_state.sun[0] & 0x80u) != 0u)
  {
    return;
  }

  _math.yy = _state.sunX;
  _math.yyNext = _state.sunXNext;

  // 6502: LDY #2*Y-1 -- the literal 143, in the same build where `CHKON` reads `Yx2M1`.
  for (std::uint8_t row = SPACE_VIEW_BOTTOM - 1u; row != 0u; --row)
  {
    const std::uint8_t width = _state.sun[row];
    if (width != 0u)
    {
      EraseSunRow(_canvas, _state, _math, _draw, width, row);
    }
  }

  _state.sun[0] = 0xFF;
}


void EraseBall(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw) noexcept
{
  // 6502: WPLS2 -- LDY LSX2 / BNE WP1. Entry 0 of the x heap is the flag: `CIRCLE` clears it
  // when it starts filling, so anything else means there is nothing to rub out.
  std::uint8_t at = _state.BallX(0);
  if (at != 0u)
  {
    ClearBallHeap(_state);
    return;
  }

  while (true)
  {
    // 6502: WPL1 -- CPY LSP / BCS WP1.
    if (at >= _state.lsp)
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
      _draw.x1 = _state.BallX(at);
      _draw.y1 = _state.BallY(at);
      ++at;
      continue;
    }

    _draw.y2 = y;
    _draw.x2 = _state.BallX(at);
    DrawLine(_canvas, _draw);
    ++at;

    /*
     * 6502: LDA SWAP / BNE WPL1 -- `LOIN` swaps its endpoints when it draws right-to-left, and
     * when it has, the coordinates in X2/Y2 are no longer this segment's end. So the hand-off to
     * the next segment is SKIPPED, and the next `LOIN` starts from whatever X1/Y1 now hold.
     */
    if (_draw.swap == 0u)
    {
      _draw.x1 = _draw.x2;
      _draw.y1 = _draw.y2;
    }
  }
}


void ErasePlanetOrSun(Canvas& _canvas, PlanetSunState& _state, MathWorkspace& _math,
                      DrawWorkspace& _draw, std::uint8_t _type) noexcept
{
  // 6502: PL2 -- LDA TYPE / LSR A / BCS P%+5 / JMP WPLS2 / JMP WPLS. The planet is 128 and the
  // sun 129, so the bottom bit is the whole of the test and no comparison is needed.
  if ((_type & 0x01u) != 0u)
  {
    EraseSun(_canvas, _state, _math, _draw);
  }
  else
  {
    EraseBall(_canvas, _state, _draw);
  }
}


bool CircleOffScreen(const PlanetSunState& _state, MathWorkspace& _math,
                     const Projection& _centre) noexcept
{
  // 6502: CHKON. Four sixteen-bit comparisons, and each one's high byte is all that is looked at.
  const AddResult rightLow = AddWithCarry(_centre.x, _math.k[0], false);
  (void)rightLow;
  const AddResult right = AddWithCarry(_centre.x1, 0u, rightLow.carry);
  if ((right.value & 0x80u) != 0u)
  {
    return true; // 6502: PL21 -- SEC / RTS.
  }

  const SubResult leftLow = SubtractWithCarry(_centre.x, _math.k[0], true);
  const SubResult left = SubtractWithCarry(_centre.x1, 0u, leftLow.carry);
  if ((left.value & 0x80u) == 0u && left.value != 0u)
  {
    return true;
  }

  // 6502: PL31 -- and the y half also STORES, so the answer is three values rather than a flag.
  const AddResult bottomLow = AddWithCarry(_centre.y, _math.k[0], false);
  _math.p1 = bottomLow.value;
  const AddResult bottom = AddWithCarry(_centre.y1, 0u, bottomLow.carry);
  if ((bottom.value & 0x80u) != 0u)
  {
    return true;
  }
  _math.p2 = bottom.value;

  const SubResult topLow = SubtractWithCarry(_centre.y, _math.k[0], true);
  const SubResult top = SubtractWithCarry(_centre.y1, 0u, topLow.carry);
  if ((top.value & 0x80u) != 0u)
  {
    return false; // 6502: BMI PL44 -- which is `PLS6`'s `CLC`, not `EDGES`'s (§6.45).
  }
  if (top.value != 0u)
  {
    return true;
  }

  // 6502: CPX Yx2M1 / RTS -- the carry from the comparison IS the return value.
  return topLow.value >= _state.yx2M1;
}


std::uint8_t DrawBallLine(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                          GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                          const Projection& _centre, std::uint8_t _x, bool _carryIn) noexcept
{
  // 6502: TXA / ADC K4 / STA K6+2 / LDA K4+1 / ADC T / STA K6+3 -- the segment's far end, as an
  // offset from the circle's centre, and both halves run on the caller's carry.
  const AddResult low = AddWithCarry(_x, _centre.y, _carryIn);
  _state.k6[2] = low.value;
  _state.k6[3] = AddWithCarry(_centre.y1, _math.t, low.carry).value;

  // 6502: LDA FLAG / BEQ BL1 / INC FLAG. The first segment of a circle has a start and no end
  // yet, so it goes straight to the break rather than being drawn.
  bool endTheRun = _state.flag != 0u;
  if (endTheRun)
  {
    ++_state.flag;
  }
  else
  {
    // 6502: BL1 -- the eight bytes of the two endpoints into the clipper's own workspace.
    _draw.x1 = _state.k5[0];
    _draw.y1 = _state.k5[1];
    _draw.x2 = _state.k5[2];
    _draw.y2 = _state.k5[3];
    _draw.xx15Plus4 = _state.k6[0];
    _draw.xx15Plus5 = _state.k6[1];
    _geometry.xx12[0] = _state.k6[2];
    _geometry.xx12[1] = _state.k6[3];

    if (ClipLine(_draw, _geometry, _math, _clip))
    {
      endTheRun = true; // 6502: BCS BL5 -- clipped away entirely
    }
    else
    {
      // 6502: LDA SWAP / BEQ BL9 -- the clipper may hand the ends back the other way round, and
      // the heap has to hold them in the order the walk produced them.
      if (_draw.swap != 0u)
      {
        std::uint8_t held = _draw.x1;
        _draw.x1 = _draw.x2;
        _draw.x2 = held;
        held = _draw.y1;
        _draw.y1 = _draw.y2;
        _draw.y2 = held;
      }

      /*
       * 6502: BL9 / BL8 -- the segment onto the heap.
       *
       * The start point is written only when the entry before is a break, which is what makes a
       * run of segments N+1 points rather than 2N.
       */
      std::uint8_t at = _state.lsp;
      if (_state.BallYBefore(at) == 0xFFu)
      {
        _state.SetBallX(at, _draw.x1);
        _state.SetBallY(at, _draw.y1);
        ++at;
      }
      _state.SetBallX(at, _draw.x2);
      _state.SetBallY(at, _draw.y2);
      ++at;
      _state.lsp = at;

      DrawLine(_canvas, _draw);

      // 6502: LDA XX13 / BNE BL5 -- an end that had to be moved ends the run too, because the
      // next segment does not start where this one was drawn to.
      endTheRun = _clip.xx13 != 0u;
    }
  }

  if (endTheRun)
  {
    // 6502: BL5 -- a break, unless the last thing written was already one.
    const std::uint8_t at = _state.lsp;
    if (_state.BallYBefore(at) != 0xFFu)
    {
      _state.SetBallY(at, 0xFF);
      ++_state.lsp;
    }
  }

  // 6502: BL7 -- this segment's end is the next one's start, and the angle moves on.
  _state.k5 = _state.k6;
  const AddResult next = AddWithCarry(_math.cnt, _state.stp, false);
  _math.cnt = next.value;
  return next.value;
}


namespace
{

/*
 * 6502: the `EOR #%11111111 / ADC #0 / TAX / LDA #&FF / ADC #0 / STA T` block, which `CIRCLE2`
 * has twice and `PLS22` twice more.
 *
 * It negates a byte into a sixteen-bit value: the low half two's-complemented and the high half
 * either 255 or 0 depending on whether the negation carried. The `ADC #0`s run on the carry the
 * comparison above left, which is SET -- that set bit is the "+1" of the two's complement, and
 * the block would be wrong without it.
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
  return { low.value, high.value };
}

} // namespace

void DrawBall(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
              GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
              const Projection& _centre, bool _carryIn) noexcept
{
  // 6502: LDX #&FF / STX FLAG / INX / STX CNT.
  _state.flag = 0xFF;
  _math.cnt = 0;

  bool carry = _carryIn;

  for (;;)
  {
    /*
     * 6502: PLL3 -- the two coordinates, a quarter-turn apart in the same table.
     *
     * `FMLTU2` masks to five bits, so the table is a quarter-wave and the sign has to be put
     * back by hand; that is what the two `CMP #33` tests do. 33 rather than 32 because what is
     * being compared is a count the loop has already stepped.
     */
    const WideResult sine = MultiplyKBySine(_math, _math.cnt, carry);
    std::uint8_t across = sine.high;
    _math.t = 0;

    carry = _math.cnt >= 33u; // 6502: LDX CNT / CPX #33
    if (carry)
    {
      const Negated negated = NegateWide(across, carry);
      across = negated.low;
      _math.t = negated.high;
      carry = false; // 6502: CLC
    }

    // 6502: PL37 -- and the centre added on, sixteen bits at a time.
    const AddResult xLow = AddWithCarry(across, _centre.x, carry);
    _state.k6[0] = xLow.value;
    _state.k6[1] = AddWithCarry(_centre.x1, _math.t, xLow.carry).value;

    // 6502: LDA CNT / CLC / ADC #16 / JSR FMLTU2 -- the same table a quarter-turn on, which is
    // the cosine.
    const AddResult quarter = AddWithCarry(_math.cnt, 16u, false);
    const WideResult cosine = MultiplyKBySine(_math, quarter.value, false);
    std::uint8_t down = cosine.high;
    _math.t = 0;

    /*
     * 6502: LDA CNT / ADC #15 / AND #63 / CMP #33.
     *
     * The `ADC #15` has no `CLC` in front of it and runs on FMLTU2's exit carry, which is set on
     * both of its antilog exits and clear on the one that returns zero. Fifteen plus that carry
     * is the sixteen above -- so the quarter-turn is only a quarter-turn when the multiply
     * produced something (§6.50).
     */
    const AddResult stepped = AddWithCarry(_math.cnt, 15u, cosine.carry);
    carry = static_cast<std::uint8_t>(stepped.value & 0x3Fu) >= 33u;
    if (carry)
    {
      const Negated negated = NegateWide(down, carry);
      down = negated.low;
      _math.t = negated.high;
      carry = false; // 6502: CLC
    }

    // 6502: PL38 -- and the segment is drawn, with the y offset still in X.
    const std::uint8_t reached =
      DrawBallLine(_canvas, _state, _draw, _geometry, _math, _clip, _centre, down, carry);

    // 6502: CMP #65 / BCS P%+5 / JMP PLL3 -- sixty-four steps of one, or eight of eight.
    if (reached >= 65u)
    {
      return; // 6502: CLC / RTS
    }
    carry = false; // the branch was not taken, so the comparison left it clear
  }
}


bool DrawCircle(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                const Projection& _centre) noexcept
{
  // 6502: JSR CHKON / BCS RTS2.
  if (CircleOffScreen(_state, _math, _centre))
  {
    return true;
  }

  // 6502: LDA #0 / STA LSX2 -- the flag that tells `WPLS2` there is something to rub out.
  _state.SetBallX(0, 0);

  /*
   * 6502: LDX K / LDA #8 / CPX #8 / BCC PL89 / LSR A / CPX #60 / BCC PL89 / LSR A.
   *
   * Eight steps for a speck, sixteen for a planet, thirty-two for one you are close to. Two
   * shifts and two comparisons rather than a table, and the carry the second `CPX` leaves is the
   * one `CIRCLE2` starts its first multiply on.
   */
  std::uint8_t step = 8;
  bool carry = _math.k[0] >= 8u;
  if (carry)
  {
    step >>= 1;
    carry = _math.k[0] >= 60u;
    if (carry)
    {
      step >>= 1;
    }
  }
  _state.stp = step;

  DrawBall(_canvas, _state, _draw, _geometry, _math, _clip, _centre, carry);
  return false;
}

} // namespace Elite
