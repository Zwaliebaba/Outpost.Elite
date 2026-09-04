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

} // namespace Elite
