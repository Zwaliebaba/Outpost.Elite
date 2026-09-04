#include "pch.h"

#include "PlanetDraw.h"

#include "EliteTypes.h"
#include "LookupTables.h"

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


AxisResult DivideAxisByZ(const ShipBlock& _ship, MathWorkspace& _math, std::uint8_t _at) noexcept
{
  // 6502: PLS1 -- LDA INWK,X / STA P / LDA INWK+1,X / AND #%01111111 / STA P+1, then the sign.
  _math.p = _ship[_at];
  _math.p1 = static_cast<std::uint8_t>(_ship[_at + 1u] & 0x7Fu);

  DivideByShipZ(_ship, _math, static_cast<std::uint8_t>(_ship[_at + 1u] & 0x80u));

  /*
   * 6502: LDA K / LDY K+1 / BEQ P%+4 / LDA #254.
   *
   * The branch skips the `LDA #254`, so a result that fits in a byte comes back as itself and
   * anything larger SATURATES rather than wrapping. A planet close enough for the division to
   * overflow is one whose markings run off the disc, and 254 is what keeps them there.
   */
  std::uint8_t value = _math.k[0];
  if (_math.k[1] != 0u)
  {
    value = 254;
  }

  return { value, _math.k[3], static_cast<std::uint8_t>(_at + 2u) };
}


AxisResult ScaleAxisByZ(const ShipBlock& _ship, MathWorkspace& _math, std::uint8_t _at) noexcept
{
  // 6502: PLS3 -- PLS1, then * 222/256, and X is SAVED rather than stepped because the caller
  // wants to divide the same axis twice.
  const AxisResult axis = DivideAxisByZ(_ship, _math, _at);
  _math.p = axis.value;
  _math.q = 222;

  // 6502: `STX U` comes AFTER the `JSR PLS1`, and `PLS1` ends with two `INX`s -- so what is
  // saved and handed back is the STEPPED index, not the one this call was given. `PL26` calls
  // this twice in a row without touching X in between and gets two different axes (§6.53).
  _math.u = axis.at;

  const std::uint8_t scaled = MultiplyUnsigned(_math).high;

  // 6502: LDY K+3 / BPL PL12 -- a positive axis returns as it is with a zero high byte.
  if ((_math.k[3] & 0x80u) == 0u)
  {
    return { scaled, 0, _math.u };
  }

  // 6502: EOR #&FF / CLC / ADC #1 / BEQ PL12 -- and a negative one is negated into a sixteen-bit
  // value whose high half is 255. Negating zero gives zero, which needs a high half of ZERO, and
  // the `BEQ` is what catches it.
  const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(scaled ^ 0xFFu), 1u, false);
  if (negated.value == 0u)
  {
    return { negated.value, 0, _math.u };
  }

  return { negated.value, 0xFF, _math.u };
}


void SetMeridianAngle(const ShipBlock& _ship, MathWorkspace& _math, std::uint8_t _a) noexcept
{
  // 6502: PLS4 -- STA Q / JSR ARCTAN, then the roof vector's sign decides which way round.
  _math.q = _a;
  std::uint8_t angle = Arctan(_math);

  // 6502: LDX INWK+14 / BMI P%+4 / EOR #%10000000 -- the branch SKIPS the flip, so it is the
  // POSITIVE roof vector that gets it.
  if ((_ship[14] & 0x80u) == 0u)
  {
    angle = static_cast<std::uint8_t>(angle ^ 0x80u);
  }

  // Two shifts: a byte turn becomes a sixty-fourth, which is what the ellipse walk counts in.
  _math.cnt2 = static_cast<std::uint8_t>(angle >> 2);
}


void LoadTwoAxes(const ShipBlock& _ship, MathWorkspace& _math, GeometryWorkspace& _geometry,
                 std::uint8_t _at) noexcept
{
  // 6502: PLS5 -- two of PLS1 into the second half of the ellipse's axes.
  AxisResult axis = DivideAxisByZ(_ship, _math, _at);
  _math.k2[2] = axis.value;
  _geometry.xx16[2] = axis.sign;

  axis = DivideAxisByZ(_ship, _math, axis.at);
  _math.k2[3] = axis.value;
  _geometry.xx16[3] = axis.sign;
}


void DrawEllipse(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                 GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                 const Projection& _centre) noexcept
{
  // 6502: PLS22 -- LDX #0 / STX CNT / DEX / STX FLAG.
  _math.cnt = 0;
  _state.flag = 0xFF;

  for (;;)
  {
    /*
     * 6502: PLL4 -- the same quarter-wave table twice, a quarter-turn apart, and each product
     * scaled by one of the ellipse's two axes. That is what makes it an ellipse rather than a
     * circle: `K2(3 2)` is how far the meridian reaches across and `K2(1 0)` how far it reaches
     * down, and a meridian seen edge-on has one of them at zero.
     */
    _math.q = SINE_TABLE[_math.cnt2 & 0x1Fu];
    _math.r = MultiplyByLog(_math, _math.k2[2], false).high;
    _math.k[0] = MultiplyByLog(_math, _math.k2[3], false).high;

    // 6502: LDX CNT2 / CPX #33 / LDA #0 / ROR A / STA XX16+5 -- the sign for this quarter, as a
    // bit rotated straight out of the comparison.
    _geometry.xx16[5] = (_math.cnt2 >= 33u) ? 0x80u : 0x00u;
    bool carry = _math.cnt2 >= 33u;

    const AddResult quarter = AddWithCarry(_math.cnt2, 16u, false);
    _math.q = SINE_TABLE[quarter.value & 0x1Fu];
    _math.k[2] = MultiplyByLog(_math, _math.k2[1], false).high;
    const WideResult second = MultiplyByLog(_math, _math.k2[0], false);
    _math.p = second.high;

    /*
     * 6502: LDA CNT2 / ADC #15 / AND #63 / CMP #33 / LDA #0 / ROR A / STA XX16+4.
     *
     * The `ADC` runs on FMLTU's exit carry exactly as `CIRCLE2`'s does (§6.50), and the `ROR`
     * turns the comparison straight into a sign bit: 128 when the comparison SET the carry, which
     * is when the value reached 33. Getting that round the wrong way puts every meridian's second
     * axis on the wrong side of the planet.
     */
    const AddResult stepped = AddWithCarry(_math.cnt2, 15u, second.carry);
    _geometry.xx16[4] =
      (static_cast<std::uint8_t>(stepped.value & 0x3Fu) >= 33u) ? 0x80u : 0x00u;

    // 6502: the two `ADD`s, each combining a product with the axis sign it belongs to.
    _math.s = static_cast<std::uint8_t>(_geometry.xx16[5] ^ _geometry.xx16[2]);
    AddSignedResult sum =
      AddSigned(_math, static_cast<std::uint8_t>(_geometry.xx16[4] ^ _geometry.xx16[0]));
    _math.t = sum.high;
    std::uint8_t low = sum.low;
    carry = sum.carry; // 6502: `STA T / BPL PL42` touches no flag, so `ADC K3` reads ADD's

    // 6502: BPL PL42 -- a negative total is negated into a sixteen-bit value, the same block
    // `CIRCLE2` has twice.
    if ((sum.high & 0x80u) != 0u)
    {
      const AddResult negated =
        AddWithCarry(static_cast<std::uint8_t>(low ^ 0xFFu), 1u, false);
      low = negated.value;
      const AddResult high =
        AddWithCarry(static_cast<std::uint8_t>(sum.high ^ 0x7Fu), 0u, negated.carry);
      _math.t = high.value;
      carry = high.carry;
    }

    // 6502: PL42 -- the centre added on.
    const AddResult xLow = AddWithCarry(low, _centre.x, carry);
    _state.k6[0] = xLow.value;
    _state.k6[1] = AddWithCarry(_math.t, _centre.x1, xLow.carry).value;

    _math.r = _math.k[0];
    _math.s = static_cast<std::uint8_t>(_geometry.xx16[5] ^ _geometry.xx16[3]);
    _math.p = _math.k[2];
    sum = AddSigned(_math, static_cast<std::uint8_t>(_geometry.xx16[4] ^ _geometry.xx16[1]));
    _math.t = static_cast<std::uint8_t>(sum.high ^ 0x80u);
    low = sum.low;
    carry = sum.carry; // 6502: `EOR #%10000000 / STA T / BPL PL43` -- again no flag is touched

    if ((_math.t & 0x80u) != 0u)
    {
      const AddResult negated =
        AddWithCarry(static_cast<std::uint8_t>(low ^ 0xFFu), 1u, false);
      low = negated.value;
      const AddResult high =
        AddWithCarry(static_cast<std::uint8_t>(_math.t ^ 0x7Fu), 0u, negated.carry);
      _math.t = high.value;
      carry = high.carry;
    }

    // 6502: PL43 -- and the segment, with the y offset in X.
    const std::uint8_t reached =
      DrawBallLine(_canvas, _state, _draw, _geometry, _math, _clip, _centre, low, carry);

    // 6502: CMP TGT / BEQ P%+4 / BCS PL40 -- the `BEQ` is what makes the last step INCLUSIVE, so
    // a meridian reaching exactly 31 draws its final segment and a crater reaching 64 draws its.
    if (reached != _math.tgt && reached >= _math.tgt)
    {
      return;
    }

    _math.cnt2 = static_cast<std::uint8_t>((_math.cnt2 + _state.stp) & 0x3Fu);
  }
}


void DrawHalfEllipse(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                     GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                     const Projection& _centre) noexcept
{
  // 6502: PLS2 -- LDA #31 / STA TGT, then straight into PLS22. Half a turn, because a meridian
  // seen from outside is a semicircle and the other half is behind the planet.
  _math.tgt = 31;
  DrawEllipse(_canvas, _state, _draw, _geometry, _math, _clip, _centre);
}


void DrawPlanetDetail(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                      GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                      const ShipBlock& _ship, Projection& _centre, std::uint8_t _type) noexcept
{
  // 6502: PL9 -- rub out last frame's planet, draw this frame's outline, and only then think
  // about the markings.
  EraseBall(_canvas, _state, _draw);

  if (DrawCircle(_canvas, _state, _draw, _geometry, _math, _clip, _centre))
  {
    return; // 6502: BCS PL20 -- CHKON refused it
  }

  // 6502: LDA K+1 / BEQ PL25 -- a radius that needed two bytes is a planet filling the screen,
  // and its markings would be off it.
  if (_math.k[1] != 0u)
  {
    return;
  }

  // 6502: LDA PLTOG / BEQ PL20 -- the detail switch, which nothing in this build ever writes.
  if (_state.pltog == 0u)
  {
    return;
  }

  if (_type == 128u)
  {
    /*
     * 6502: part 2 -- MERIDIANS. Two great circles at right angles, each drawn as a half
     * ellipse whose axes are the planet's own orientation vectors projected onto the screen.
     *
     * `LDA K / CMP #6 / BCC PL20` -- under six pixels across there is nothing to draw them on.
     */
    if (_math.k[0] < 6u)
    {
      return;
    }

    // 6502: LDA INWK+14 / EOR #%10000000 / STA P / LDA INWK+20 / JSR PLS4 -- where the first
    // meridian starts, from the roof vector against the nose.
    _math.p = static_cast<std::uint8_t>(_ship[14] ^ 0x80u);
    SetMeridianAngle(_ship, _math, _ship[20]);

    AxisResult axis = DivideAxisByZ(_ship, _math, 9);
    _math.k2[0] = axis.value;
    _geometry.xx16[0] = axis.sign;

    axis = DivideAxisByZ(_ship, _math, axis.at);
    _math.k2[1] = axis.value;
    _geometry.xx16[1] = axis.sign;

    LoadTwoAxes(_ship, _math, _geometry, 15);
    DrawHalfEllipse(_canvas, _state, _draw, _geometry, _math, _clip, _centre);

    // And the second meridian, which shares the first pair of axes and takes a new second pair.
    _math.p = static_cast<std::uint8_t>(_ship[14] ^ 0x80u);
    SetMeridianAngle(_ship, _math, _ship[26]);

    LoadTwoAxes(_ship, _math, _geometry, 21);
    DrawHalfEllipse(_canvas, _state, _draw, _geometry, _math, _clip, _centre);
    return;
  }

  /*
   * 6502: PL26 -- a CRATER. One whole ellipse, offset from the planet's centre along its own
   * nose vector, so it slides round the disc as the planet turns and disappears over the edge.
   *
   * `LDA INWK+20 / BMI PL20` -- the nose pointing away means the crater is on the far side.
   */
  if ((_ship[20] & 0x80u) != 0u)
  {
    return;
  }

  // 6502: LDX #15 / JSR PLS3 -- the offset, one axis at a time, ADDED to x and SUBTRACTED from y.
  AxisResult offset = ScaleAxisByZ(_ship, _math, 15);
  const AddResult acrossLow = AddWithCarry(offset.value, _centre.x, false);
  _centre.x = acrossLow.value;
  _centre.x1 = AddWithCarry(offset.sign, _centre.x1, acrossLow.carry).value;

  offset = ScaleAxisByZ(_ship, _math, offset.at);
  _math.p = offset.value;
  const SubResult downLow = SubtractWithCarry(_centre.y, _math.p, true);
  _centre.y = downLow.value;
  _math.p = offset.sign;
  _centre.y1 = SubtractWithCarry(_centre.y1, _math.p, downLow.carry).value;

  /*
   * 6502: four PLS1s, each HALVED before it is stored.
   *
   * The halving is what makes the crater smaller than the planet, and it is an `LSR A` on the
   * magnitude alone -- the sign in Y is untouched, so a negative axis halves towards zero rather
   * than away from it.
   */
  AxisResult axis = DivideAxisByZ(_ship, _math, 9);
  _math.k2[0] = static_cast<std::uint8_t>(axis.value >> 1);
  _geometry.xx16[0] = axis.sign;

  axis = DivideAxisByZ(_ship, _math, axis.at);
  _math.k2[1] = static_cast<std::uint8_t>(axis.value >> 1);
  _geometry.xx16[1] = axis.sign;

  axis = DivideAxisByZ(_ship, _math, 21);
  _math.k2[2] = static_cast<std::uint8_t>(axis.value >> 1);
  _geometry.xx16[2] = axis.sign;

  axis = DivideAxisByZ(_ship, _math, axis.at);
  _math.k2[3] = static_cast<std::uint8_t>(axis.value >> 1);
  _geometry.xx16[3] = axis.sign;

  // 6502: LDA #64 / STA TGT / LDA #0 / STA CNT2 / JMP PLS22 -- a whole turn, from zero.
  _math.tgt = 64;
  _math.cnt2 = 0;
  DrawEllipse(_canvas, _state, _draw, _geometry, _math, _clip, _centre);
}


void DrawPlanetOrSun(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw,
                     GeometryWorkspace& _geometry, MathWorkspace& _math, ClipState& _clip,
                     Rng& _rng, const ShipBlock& _ship, Projection& _centre,
                     std::uint8_t _type) noexcept
{
  /*
   * 6502: PLANET -- three rejections before any arithmetic.
   *
   * `LDA INWK+8 / CMP #48 / BCS PL2` is "further away than the sign byte can usefully say", and
   * `ORA INWK+7 / BEQ PL2` is "no distance at all", which is the case `DVID3B2`'s divide cannot
   * take. Both go to `PL2`, so a rejected planet is ERASED rather than merely skipped -- which is
   * why flying away from one leaves no outline behind.
   */
  if (_ship[8] >= 48u || (_ship[8] | _ship[7]) == 0u)
  {
    ErasePlanetOrSun(_canvas, _state, _math, _draw, _type);
    return;
  }

  if (Project(_ship, _math, _centre).offScreen)
  {
    ErasePlanetOrSun(_canvas, _state, _math, _draw, _type);
    return;
  }

  // 6502: LDA #96 / STA P+1 / LDA #0 / STA P / JSR DVID3B2 -- the radius is 96 * 256 / z, and
  // 96 is the planet's size in the same units everything else in the geometry uses.
  _math.p1 = 96;
  _math.p = 0;
  DivideByShipZ(_ship, _math, 0);

  // 6502: LDA K+1 / BEQ PL82 / LDA #248 / STA K -- a radius that overflowed a byte is clamped,
  // and K+1 is LEFT SET, which is what `PL9` reads to skip the markings.
  if (_math.k[1] != 0u)
  {
    _math.k[0] = 248;
  }

  // 6502: LDA TYPE / LSR A / BCC PL9 / JMP SUN.
  if ((_type & 0x01u) != 0u)
  {
    DrawSun(_canvas, _state, _draw, _math, _rng, _centre);
    return;
  }

  DrawPlanetDetail(_canvas, _state, _draw, _geometry, _math, _clip, _ship, _centre, _type);
}


void DrawSun(Canvas& _canvas, PlanetSunState& _state, DrawWorkspace& _draw, MathWorkspace& _math,
             Rng& _rng, const Projection& _centre) noexcept
{
  // 6502: LDA #1 / STA LSX -- entry 0 stops being the "nothing there" flag the moment the
  // routine commits to drawing, so a `WPLS` interrupted halfway still has something to erase.
  _state.sun[0] = 1;

  if (CircleOffScreen(_state, _math, _centre))
  {
    // 6502: BCS PLF3M3 / JMP WPLS -- nothing of it is on screen, so only rub out the old one.
    EraseSun(_canvas, _state, _math, _draw);
    return;
  }

  /*
   * 6502: LDA #0 / LDX K / CPX #96 / ROL A / CPX #40 / ROL A / CPX #16 / ROL A.
   *
   * Three comparisons rolled into three bits, so `CNT` is 0, 1, 3 or 7 -- the mask the random
   * byte is ANDed with before it is added to each row's half-width. A distant sun is a smooth
   * disc and a near one has a ragged edge, and this is the whole of that effect.
   */
  std::uint8_t rough = 0;
  rough = static_cast<std::uint8_t>((rough << 1) | ((_math.k[0] >= 96u) ? 1u : 0u));
  rough = static_cast<std::uint8_t>((rough << 1) | ((_math.k[0] >= 40u) ? 1u : 0u));
  rough = static_cast<std::uint8_t>((rough << 1) | ((_math.k[0] >= 16u) ? 1u : 0u));
  _math.cnt = rough;

  /*
   * 6502: PLF18 -- where to stop. `CHKON` left the circle's top and bottom in `P+1` and `P+2`,
   * and the bottom row of the sun is whichever of that and the screen's own bottom comes first.
   * A sun whose top is at row 0 is given a `TGT` of 1 rather than 0, because row 0 is the flag.
   */
  std::uint8_t bottom = _state.yx2M1;
  if (_math.p2 == 0u && _state.yx2M1 >= _math.p1)
  {
    bottom = (_math.p1 != 0u) ? _math.p1 : std::uint8_t{ 1 };
  }
  _math.tgt = bottom;

  /*
   * 6502: LDA Yx2M1 / SEC / SBC K4 / TAX / LDA #0 / SBC K4+1 -- how far the bottom row is from
   * the sun's centre, which is where the walk starts.
   */
  const SubResult offsetLow = SubtractWithCarry(_state.yx2M1, _centre.y, true);
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
    at = _math.k[0];
    sign = 0;
  }
  else if (at == 0u)
  {
    sign = 0xFF; // 6502: PLF17 -- exactly on the centre
  }
  else if (at >= _math.k[0])
  {
    at = _math.k[0]; // 6502: BCC PLF5 not taken, so PLF4
    sign = 0;
  }

  // 6502: PLF5 -- STX V / STA V+1, and K2(1 0) = K * K, which every row's width is measured
  // against.
  _state.v = at;
  _state.vNext = sign;

  _math.k2[1] = SquareUnsigned(_math, _math.k[0]).high;
  _math.k2[0] = _math.p;

  // 6502: part 2 -- rub out the rows BELOW the sun, with last frame's centre, before any of
  // this frame's arithmetic touches `YY`.
  std::uint8_t row = _state.yx2M1;
  _math.yy = _state.sunX;
  _math.yyNext = _state.sunXNext;

  while (row != _math.tgt && row != 0u)
  {
    if (_state.sun[row] != 0u)
    {
      EraseSunRow(_canvas, _state, _math, _draw, _state.sun[row], row);
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
   * 6502: `PLF6`'s `DEY / BEQ PLF8` leaves through the ROUTINE'S TAIL and not through part 4, so
   * a sun that reaches the top of the screen does not get the rows above it erased -- there are
   * none. The other exit, `PLF10`'s `CPX K`, falls into part 4 because there are.
   */
  bool eraseAbove = false;

  for (;;)
  {
    // 6502: the half-width, as sqrt(K^2 - v^2).
    _math.t = SquareUnsigned(_math, _state.v).high;
    const SubResult widthLow = SubtractWithCarry(_math.k2[0], _math.p, true);
    _math.q = widthLow.value;
    _math.r = SubtractWithCarry(_math.k2[1], _math.t, widthLow.carry).value;

    _draw.y1 = row;
    const bool rootCarry = SquareRoot(_math);

    // 6502: JSR DORND / AND CNT / CLC / ADC Q / BCC PLF44 / LDA #255 -- the ragged edge, and it
    // saturates rather than wrapping round to nothing. The generator runs on the carry `LL5`
    // left, which is the last bit out of the square root (§6.55).
    const RngResult roll = _rng.Next(rootCarry);
    const AddResult ragged =
      AddWithCarry(static_cast<std::uint8_t>(roll.value & _math.cnt), _math.q, false);
    std::uint8_t width = ragged.value;
    if (ragged.carry)
    {
      width = 255;
    }

    // 6502: PLF44 -- LDX LSO,Y / STA LSO,Y. The old width is kept and the new one stored, and
    // what gets drawn is the DIFFERENCE between the two lines rather than both of them.
    const std::uint8_t was = _state.sun[row];
    _state.sun[row] = width;

    if (was != 0u)
    {
      // The old line, clipped against LAST frame's centre.
      _math.yy = _state.sunX;
      _math.yyNext = _state.sunXNext;
      (void)ClipSunRow(_state, _math, _draw, was, row);
      _math.xx = _draw.x1;
      _math.xxNext = _draw.x2;

      // And the new one, against this frame's.
      _math.yy = _centre.x;
      _math.yyNext = _centre.x1;
      const bool offScreen = ClipSunRow(_state, _math, _draw, _state.sun[row], row);

      if (!offScreen)
      {
        // 6502: the two ends CROSSED OVER, so what is drawn is one end of the old line to the
        // matching end of the new one -- the sliver that has appeared or gone.
        const std::uint8_t held = _draw.x2;
        _draw.x2 = _math.xx;
        _math.xx = held;
        DrawHorizontalLine(_canvas, _draw);
      }

      // 6502: PLF23 -- and the other sliver.
      _draw.x1 = _math.xx;
      _draw.x2 = _math.xxNext;
      DrawHorizontalLine(_canvas, _draw);
    }
    else
    {
      // 6502: PLF11 -- nothing was there last frame, so the whole of the new line is drawn.
      _math.yy = _centre.x;
      _math.yyNext = _centre.x1;
      if (ClipSunRow(_state, _math, _draw, _state.sun[row], row))
      {
        _state.sun[row] = 0;
      }
      else
      {
        DrawHorizontalLine(_canvas, _draw);
      }
    }

    // 6502: PLF6 -- DEY / BEQ PLF8.
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
      if (next > _math.k[0])
      {
        eraseAbove = true;
        break;
      }
    }
    else
    {
      /*
       * 6502: DEC V / BNE PLFL / DEC V+1.
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
    _math.yy = _state.sunX;
    _math.yyNext = _state.sunXNext;
    while (row != 0u)
    {
      if (_state.sun[row] != 0u)
      {
        EraseSunRow(_canvas, _state, _math, _draw, _state.sun[row], row);
      }
      --row;
    }
  }

  // 6502: PLF8 -- and this frame's centre becomes next frame's.
  _state.sunX = _centre.x;
  _state.sunXNext = _centre.x1;
}


void ClearShipBlock(ShipBlock& _work) noexcept
{
  // 6502: ZINF -- LDY #NI%-1 / LDA #0 / .ZI1 STA INWK,Y / DEY / BPL ZI1.
  for (std::size_t byte = 0; byte < SHIP_BLOCK_SIZE; ++byte)
  {
    _work[byte] = 0;
  }

  /*
   * 6502: LDA #96 / STA INWK+18 / STA INWK+22 / ORA #%10000000 / STA INWK+14.
   *
   * The three high bytes of `roofv_y`, `sidev_x` and `nosev_z`, so the ship comes out square to
   * the axes -- and the sign on the nose is what makes it face TOWARDS the player. 96 rather than
   * 127 because the orientation vectors are unit vectors at a scale of 96, which is the same 96
   * `PLANET` divides by for its radius.
   */
  _work[18] = 96;
  _work[22] = 96;
  _work[14] = static_cast<std::uint8_t>(96u | 0x80u);
}


void SeedStardustField(Canvas& _canvas, DrawWorkspace& _draw, Stardust& _dust, Rng& _rng,
                       bool _carryIn) noexcept
{
  /*
   * 6502: nWq -- three random bytes per speck, and the generator is threaded straight through.
   *
   * `JSR PIXEL2 / DEY / BNE SAL4` and then `JSR DORND`, so each speck's first random byte runs
   * on the carry the PREVIOUS speck's plot left -- which is `ZZ >= 80`, the distance test inside
   * `PIXEL` (§6.57). The field a fresh view is filled with therefore depends on where the last
   * speck was drawn.
   *
   * `ORA #8` on the distance keeps every speck at least eight units away, so none of them starts
   * on the player's face.
   */
  bool carry = _carryIn;

  for (std::uint8_t at = _dust.count; at != 0u; --at)
  {
    RngResult roll = _rng.Next(carry);
    _draw.zz = static_cast<std::uint8_t>(roll.value | 8u);
    _dust.z[at] = _draw.zz;

    roll = _rng.Next(roll.carry);
    _dust.x[at] = roll.value;
    _draw.x1 = roll.value;

    roll = _rng.Next(roll.carry);
    _dust.y[at] = roll.value;
    _draw.y1 = roll.value;

    carry = PlotRelativePixel(_canvas, _draw);
  }
}


void ClearAllShips(Canvas& _canvas, DrawWorkspace& _draw, PlanetSunState& _state, Bubble& _bubble,
                   ShipBlock& _work, FlightState& _flight, std::uint8_t _view) noexcept
{
  // 6502: WPSHPS -- LDX #0 / .WSL1 LDA FRIN,X / BEQ WS2 / BMI WS1.
  for (std::size_t slot = 0; slot < _bubble.slots.size(); ++slot)
  {
    const std::uint8_t type = _bubble.slots[slot];
    if (type == 0u)
    {
      break; // 6502: BEQ WS2 -- the first empty slot ends the list
    }
    if ((type & 0x80u) != 0u)
    {
      continue; // 6502: BMI WS1 -- the planet and the sun have no blip and no line heap
    }

    // 6502: JSR GINF / LDY #31 / .WSL2 -- thirty-two bytes, not the whole block.
    for (std::size_t byte = 0; byte < 32u; ++byte)
    {
      _work[byte] = _bubble.blocks[slot][byte];
    }

    // 6502: STA TYPE / ... / STX XSAV / JSR SCAN / LDX XSAV. Both stores are the routine's own
    // and were invisible while the scanner was a seam: `SCAN` reads `TYPE` as a global, and
    // `XSAV` is how the loop index survives the call.
    _flight.type = type;
    _flight.slot = static_cast<std::uint8_t>(slot);
    DrawScannerBlip(_canvas, _draw, _work, type, _view);

    /*
     * 6502: LDY #31 / LDA (INF),Y / AND #%10100111 / STA (INF),Y.
     *
     * It masks the byte in the SLOT and not the copy in `INWK`, so the two disagree the moment
     * this returns -- which is correct, because the caller is about to redraw everything from
     * the slots. The mask clears bits 3, 4 and 6: "drawn on screen", "firing a laser", and the
     * one in between.
     */
    _bubble.blocks[slot][SHIP_STATE_OFFSET] =
      static_cast<std::uint8_t>(_bubble.blocks[slot][SHIP_STATE_OFFSET] & 0xA7u);
  }

  // 6502: WS2 -- LDX #0 / STX LSP / DEX / STX LSX2 / STX LSY2. Note `LSP` goes to ZERO here and
  // to one in `WP1`; the two are not the same reset.
  _state.lsp = 0;
  _state.SetBallX(0, 0xFF);
  _state.SetBallY(0, 0xFF);

  // 6502: and the fall-through into FLFLLS.
  ClearSunHeap(_state);
}


void SeedStardustAndClearShips(Canvas& _canvas, DrawWorkspace& _draw, Stardust& _dust, Rng& _rng,
                               PlanetSunState& _state, Bubble& _bubble, ShipBlock& _work,
                               FlightState& _flight, std::uint8_t _view, bool _carryIn) noexcept
{
  // 6502: NWSTARS -- LDA QQ11 / BNE WPSHPS. `QQ11` is the view, zero for the space view, and a
  // menu has no stardust to fill. The same byte then decides whether `SCAN` draws anything.
  if (_view == 0u)
  {
    SeedStardustField(_canvas, _draw, _dust, _rng, _carryIn);
  }

  ClearAllShips(_canvas, _draw, _state, _bubble, _work, _flight, _view);
}

} // namespace Elite
