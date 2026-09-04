#include "pch.h"

#include "ShipDraw.h"

#include "EliteTypes.h"

#include <utility>

namespace Elite
{

void DivideByShipZ(const ShipBlock& _ship, MathWorkspace& _math, std::uint8_t _a) noexcept
{
  _math.p2 = _a;

  // The `ORA #1` is what makes the divide below safe, and it is deliberate rather than defensive:
  // a ship exactly on the plane of the screen has z_lo = 0, and the difference between dividing
  // by zero and dividing by one is invisible at this scale.
  _math.q = static_cast<std::uint8_t>(_ship.bytes[SHIP_Z_OFFSET] | 0x01u);
  _math.r = _ship.bytes[SHIP_Z_OFFSET + 1];
  _math.s = _ship.bytes[SHIP_Z_OFFSET + 2];

  DivideSignedToK(_math);
}

ScreenOffset DivideToScreenOffset(const ShipBlock& _ship, MathWorkspace& _math, std::uint8_t _a) noexcept
{
  DivideByShipZ(_ship, _math, _a);

  // The top two bytes of the quotient, sign removed. Anything at all up there is already past
  // 65,536 and there is nothing to say about where it would be on a 256-pixel view.
  const std::uint8_t top = static_cast<std::uint8_t>((_math.k[3] & 0x7Fu) | _math.k[2]);
  if (top != 0u)
  {
    // 6502: PL21 -- SEC and return. X is not touched on this path.
    return ScreenOffset{ _math.k[0], 0, top, true };
  }

  const std::uint8_t high = _math.k[1];
  if (high >= 4u)
  {
    // 6502: the CPX that overflows at 1024, returning through PL6's RTS with the carry the
    // comparison set. A is still the zero the test above left, which is why `SHPPT` -- which
    // reads A and not the carry -- can miss this and does.
    return ScreenOffset{ _math.k[0], high, 0, true };
  }

  if ((_math.k[3] & 0x80u) == 0u)
  {
    // Positive, and the carry is already clear: the comparison above did not set it.
    return ScreenOffset{ _math.k[0], high, 0, false };
  }

  // 6502: the two's complement negation. `ADC #1` runs with the carry the CPX left CLEAR, so it
  // adds exactly one -- the one place in this file where the incoming carry is not part of the
  // sum, and the only one where reading it as `+ 1 + C` would still be right.
  const AddResult low = AddWithCarry(static_cast<std::uint8_t>(_math.k[0] ^ 0xFFu), 1, false);
  _math.k[0] = low.value;

  const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(high ^ 0xFFu), 0, low.carry);

  // 6502: PL44 -- CLC, then PL6's RTS.
  return ScreenOffset{ _math.k[0], negated.value, negated.value, false };
}

ProjectResult Project(const ShipBlock& _ship, MathWorkspace& _math, Projection& _screen) noexcept
{
  _math.p = _ship.bytes[SHIP_X_OFFSET];
  _math.p1 = _ship.bytes[SHIP_X_OFFSET + 1];

  const ScreenOffset across = DivideToScreenOffset(_ship, _math, _ship.bytes[SHIP_X_OFFSET + 2]);
  if (across.overflow)
  {
    // 6502: BCS PL2-1, which is PROJ's own RTS one byte before the next routine begins.
    return ProjectResult{ true, across.a };
  }

  // The carry is clear here, so the addition is the plain one it looks like.
  const AddResult x = AddWithCarry(across.low, SPACE_VIEW_CENTRE_X, false);
  _screen.x = x.value;
  const AddResult x1 = AddWithCarry(across.high, 0, x.carry);
  _screen.x1 = x1.value;

  _math.p = _ship.bytes[SHIP_Y_OFFSET];
  _math.p1 = _ship.bytes[SHIP_Y_OFFSET + 1];

  const std::uint8_t upwards = static_cast<std::uint8_t>(_ship.bytes[SHIP_Y_OFFSET + 2] ^ 0x80u);
  const ScreenOffset down = DivideToScreenOffset(_ship, _math, upwards);
  if (down.overflow)
  {
    return ProjectResult{ true, down.a };
  }

  const AddResult y = AddWithCarry(down.low, SPACE_VIEW_CENTRE_Y, false);
  _screen.y = y.value;
  const AddResult y1 = AddWithCarry(down.high, 0, y.carry);
  _screen.y1 = y1.value;

  return ProjectResult{ false, y1.value };
}


namespace
{

/*
 * 6502: Shpt -- one four-pixel horizontal line, written as the line-heap entry that ENDS at
 * offset `_y`. The original walks Y forwards and then back, which is why the entry's four bytes
 * come out at `_y - 1` to `_y + 2` rather than starting where Y is.
 *
 * Returns false where the original does `BCS nono-2`. That target is a `PLA / PLA` above `nono`
 * which throws away this routine's own return address, so the failure does not come back here --
 * it returns to SHPPT's caller with the ship marked as not drawn. A bool and an early return say
 * the same thing without needing a stack.
 */
bool StorePoint(LineHeap& _heap, std::uint16_t _address, const Projection& _screen, std::uint8_t _y,
                std::uint8_t _a) noexcept
{
  _heap.Write(static_cast<std::uint16_t>(_address + _y), _a);
  _heap.Write(static_cast<std::uint16_t>(_address + _y + 2), _a);
  _heap.Write(static_cast<std::uint16_t>(_address + _y + 1), _screen.x);

  // The carry is clear here on both calls: on the first because the `CMP` that let us past the
  // bottom-of-screen test left it clear, and on the second because the first call would have
  // bailed out if its own `ADC` had carried.
  const AddResult right = AddWithCarry(_screen.x, 3, false);
  if (right.carry)
  {
    return false;
  }

  _heap.Write(static_cast<std::uint16_t>(_address + _y - 1), right.value);
  return true;
}

} // namespace

void DrawShipLines(Canvas& _canvas, DrawWorkspace& _draw, const LineHeap& _heap,
                   std::uint16_t _address) noexcept
{
  const std::uint8_t length = _heap.Read(_address);
  if (length < 4u)
  {
    return;
  }

  // A byte, and the comparison is on a byte, because the original's is: `INY / CPY XX20 / BCC`.
  // A heap longer than 253 bytes would wrap Y and loop forever here exactly as it does there;
  // the length comes from byte 5 of a blueprint, and the largest of the thirty-three is 157.
  //
  // The test being at the BOTTOM is the instruction order and not a behaviour: the guard above
  // has already established that the first one would pass. Same shape as `DVL6` in `DVID3B`, and
  // a while-loop here is an equivalent mutation for the same reason.
  std::uint8_t y = 1;
  do
  {
    _draw.x1 = _heap.Read(static_cast<std::uint16_t>(_address + y));
    _draw.y1 = _heap.Read(static_cast<std::uint16_t>(_address + y + 1));
    _draw.x2 = _heap.Read(static_cast<std::uint16_t>(_address + y + 2));
    _draw.y2 = _heap.Read(static_cast<std::uint16_t>(_address + y + 3));

    DrawLine(_canvas, _draw);

    y = static_cast<std::uint8_t>(y + 4);
  } while (y < length);
}

void StoreLineCountAndDraw(Canvas& _canvas, DrawWorkspace& _draw, LineHeap& _heap,
                           std::uint16_t _address, std::uint8_t _count) noexcept
{
  _heap.Write(_address, _count);
  DrawShipLines(_canvas, _draw, _heap, _address);
}

void EraseShip(Canvas& _canvas, DrawWorkspace& _draw, ShipBlock& _ship, const LineHeap& _heap) noexcept
{
  if ((_ship[SHIP_STATE_OFFSET] & SHIP_STATE_DRAWN) == 0u)
  {
    return;
  }

  _ship[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>(_ship[SHIP_STATE_OFFSET] ^ SHIP_STATE_DRAWN);
  DrawShipLines(_canvas, _draw, _heap, ShipHeapAddress(_ship));
}

void DrawShipAsPoint(Canvas& _canvas, DrawWorkspace& _draw, ShipBlock& _ship, LineHeap& _heap,
                     MathWorkspace& _math, Projection& _screen) noexcept
{
  EraseShip(_canvas, _draw, _ship, _heap);

  const ProjectResult projected = Project(_ship, _math, _screen);

  // 6502: ORA K3+1 / BNE nono. See the header -- this is not the carry, and the difference is
  // visible whenever `PLS6` overflows on its second test rather than its first.
  const bool offScreen = (projected.a | _screen.x1) != 0u
                         || _screen.y >= static_cast<std::uint8_t>(SPACE_VIEW_BOTTOM - 2);

  const std::uint16_t heap = ShipHeapAddress(_ship);

  // The two stores write as they go and can fail half way, which is what the original does: the
  // first four bytes of the entry are already on the heap when the second call gives up. Nothing
  // reads them, because the length byte is only written on the path below.
  if (offScreen || !StorePoint(_heap, heap, _screen, 2, _screen.y)
      || !StorePoint(_heap, heap, _screen, 6, AddWithCarry(_screen.y, 1, false).value))
  {
    // 6502: nono -- LDA #%11110111 / AND XX1+31. Reached four ways, and all four leave the ship
    // marked as not on the screen.
    _ship[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>(_ship[SHIP_STATE_OFFSET] & 0xF7u);
    return;
  }

  _ship[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>(_ship[SHIP_STATE_OFFSET] | SHIP_STATE_DRAWN);
  StoreLineCountAndDraw(_canvas, _draw, _heap, heap, 8);
}


void DotProducts(const DrawWorkspace& _draw, GeometryWorkspace& _geometry, MathWorkspace& _math) noexcept
{
  // The six bytes of XX15, as the three sign-magnitude pairs the dot product treats them as.
  const std::uint8_t magnitude[3] = { _draw.x1, _draw.x2, _draw.xx15Plus4 };
  const std::uint8_t sign[3] = { _draw.y1, _draw.y2, _draw.xx15Plus5 };

  // Three vectors of six, and the loop in the original ends on `CMP #17 / BCC`, so it runs for
  // X = 0, 6 and 12 and stops at 18 rather than testing a count.
  for (std::size_t vector = 0; vector < 3u; ++vector)
  {
    const std::size_t base = vector * 6u;

    // The first term sets S, which is the sign the whole sum is accumulated against -- `LL38`
    // FLIPS it when a subtraction goes past zero, so what comes out at the end is the sign of the
    // answer and not of the first product.
    _math.q = magnitude[0];
    const std::uint8_t first = MultiplyByLog(_math, _geometry.xx16[base]);
    _math.t = first;
    _math.s = static_cast<std::uint8_t>(sign[0] ^ _geometry.xx16[base + 1]);

    // Q is set twice on purpose and the first is not dead: `FMLTU` reads Q as one operand and
    // the original stores the product straight back over it. `STA Q / JSR FMLTU / STA Q`.
    _math.q = magnitude[1];
    _math.q = MultiplyByLog(_math, _geometry.xx16[base + 2]);
    _math.r = _math.t;
    _math.t = CombineSigned(_math, static_cast<std::uint8_t>(sign[1] ^ _geometry.xx16[base + 3]));

    _math.q = magnitude[2];
    _math.q = MultiplyByLog(_math, _geometry.xx16[base + 4]);
    _math.r = _math.t;

    _geometry.xx12[vector * 2u] =
      CombineSigned(_math, static_cast<std::uint8_t>(sign[2] ^ _geometry.xx16[base + 5]));
    _geometry.xx12[vector * 2u + 1u] = _math.s;
  }
}


std::uint8_t PrepareSlope(MathWorkspace& _math, const GeometryWorkspace& _geometry) noexcept
{
  _math.q = _geometry.xx12[2];

  const std::uint8_t original = _math.s;
  if ((original & 0x80u) != 0u)
  {
    // (S R) = -(S R). The low byte is `LDA #0 / SEC / SBC R`, and the high byte's `ADC #0` runs
    // on that subtraction's carry, so the two are one sixteen-bit negation and not two eight-bit
    // ones.
    const SubResult low = SubtractWithCarry(0, _math.r, true);
    _math.r = low.value;
    _math.s = AddWithCarry(static_cast<std::uint8_t>(original ^ 0xFFu), 0, low.carry).value;
  }

  return static_cast<std::uint8_t>(original ^ _geometry.xx12[3]);
}

namespace
{

/// 6502: LL122 -- (Y X) = (S R) * Q, shift-and-add, with the first shift happening BEFORE any
/// addition so the product comes out halved. That is not an error to correct: the caller wants
/// the step for half a pixel.
SlopeStep MultiplySlope(MathWorkspace& _math) noexcept
{
  std::uint8_t low = 0;
  std::uint8_t high = 0;

  // `LSR S / ROR R / ASL Q` -- one shift of the multiplicand and one bit off the top of the
  // multiplier.
  const auto step = [&_math]() noexcept
  {
    const bool intoR = (_math.s & 0x01u) != 0u;
    _math.s = static_cast<std::uint8_t>(_math.s >> 1);
    _math.r = RotateRight(_math.r, intoR).value;

    const ShiftResult multiplier = RotateLeftValue(_math.q, false);
    _math.q = multiplier.value;
    return multiplier.carry;
  };

  /*
   * The first shift is OUTSIDE the loop, and that is the whole shape of the routine rather than
   * a detail: the entry does `LSR S / ROR R / ASL Q / BCC LL126`, and `LL126` -- which is where
   * the "have we run out of multiplier" test lives -- shifts again before testing. So a Q of
   * zero still gets TWO shifts, not one, and the port shifted once until the sweep said
   * otherwise.
   */
  bool add = step();
  for (;;)
  {
    if (add)
    {
      const AddResult sum = AddWithCarry(low, _math.r, false);
      low = sum.value;
      high = AddWithCarry(high, _math.s, sum.carry).value;
    }

    add = step();
    if (!add && _math.q == 0u)
    {
      break;
    }
  }

  return SlopeStep{ low, high };
}

/// 6502: LL121 -- (Y X) = (S R) / Q, restoring division, with (Y X) starting at &FFFE as the bit
/// counter in the same trick `LL31` uses: the quotient bits push the set bits out of the top.
SlopeStep DivideSlope(MathWorkspace& _math) noexcept
{
  std::uint8_t low = 0xFE;
  std::uint8_t high = 0xFF;

  for (;;)
  {
    const ShiftResult shifted = RotateLeftValue(_math.r, false);
    _math.r = shifted.value;
    const ShiftResult raised = RotateLeft(_math.s, shifted.carry);
    _math.s = raised.value;

    bool bit = raised.carry;
    if (raised.carry || _math.s >= _math.q)
    {
      const SubResult difference = SubtractWithCarry(_math.s, _math.q, true);
      _math.s = difference.value;
      _math.r = SubtractWithCarry(_math.r, 0, difference.carry).value;
      bit = true;
    }

    const ShiftResult quotientLow = RotateLeft(low, bit);
    low = quotientLow.value;
    const ShiftResult quotientHigh = RotateLeft(high, quotientLow.carry);
    high = quotientHigh.value;

    if (!quotientHigh.carry)
    {
      break;
    }
  }

  return SlopeStep{ low, high };
}

/// 6502: LL133 -- negate (Y X). Both loops exit with the carry clear, so the `ADC #1` adds one.
SlopeStep NegateStep(SlopeStep _step) noexcept
{
  const AddResult low = AddWithCarry(static_cast<std::uint8_t>(_step.low ^ 0xFFu), 1, false);
  const AddResult high = AddWithCarry(static_cast<std::uint8_t>(_step.high ^ 0xFFu), 0, low.carry);
  return SlopeStep{ low.value, high.value };
}

/// The tail both entry points share: run one of the two loops, then take the sign from the byte
/// `LL129` returned -- negating when it is POSITIVE, because the step has to oppose the slope.
SlopeStep FinishStep(SlopeStep _step, std::uint8_t _sign) noexcept
{
  return ((_sign & 0x80u) == 0u) ? NegateStep(_step) : _step;
}

} // namespace

SlopeStep StepAlongX(MathWorkspace& _math, const GeometryWorkspace& _geometry,
                     const DrawWorkspace& _draw) noexcept
{
  _math.r = _draw.x1;

  const std::uint8_t sign = PrepareSlope(_math, _geometry);
  const SlopeStep step = (_math.t != 0u) ? DivideSlope(_math) : MultiplySlope(_math);
  return FinishStep(step, sign);
}

SlopeStep StepAlongY(MathWorkspace& _math, const GeometryWorkspace& _geometry) noexcept
{
  const std::uint8_t sign = PrepareSlope(_math, _geometry);
  const SlopeStep step = (_math.t != 0u) ? MultiplySlope(_math) : DivideSlope(_math);
  return FinishStep(step, sign);
}


namespace
{

/// The move every one of `LL118`'s four clamps ends with: add the sixteen-bit step to the OTHER
/// coordinate. `TXA / CLC / ADC lo / STA lo` then `TYA / ADC hi / STA hi`.
void AddStep(SlopeStep _step, std::uint8_t& _low, std::uint8_t& _high) noexcept
{
  const AddResult sum = AddWithCarry(_step.low, _low, false);
  _low = sum.value;
  _high = AddWithCarry(_step.high, _high, sum.carry).value;
}

} // namespace

void MovePointOnScreen(DrawWorkspace& _draw, const GeometryWorkspace& _geometry,
                       MathWorkspace& _math) noexcept
{
  // The accumulator threads through the first two clamps: the left-edge branch ends `TAX` with A
  // zero, and the right-edge test below is `BEQ` on that same A. So clamping to the left edge is
  // what stops the right-edge clamp running as well.
  std::uint8_t a = _draw.y1;

  if ((a & 0x80u) != 0u)
  {
    // x1_hi is negative, so the point is off the LEFT edge. Step to x = 0.
    _math.s = a;
    AddStep(StepAlongX(_math, _geometry, _draw), _draw.x2, _draw.y2);
    _draw.x1 = 0;
    _draw.y1 = 0;
    a = 0;
  }

  // 6502: LL119 -- x1_hi is non-zero and positive, so the point is off the RIGHT edge. The `DEC S`
  // is what makes the step land on 255 rather than 256.
  if (a != 0u)
  {
    _math.s = static_cast<std::uint8_t>(a - 1u);
    AddStep(StepAlongX(_math, _geometry, _draw), _draw.x2, _draw.y2);
    _draw.x1 = 255;
    _draw.y1 = 0;
  }

  // 6502: LL134 -- y1_hi is negative, so the point is off the TOP. Step to y = 0.
  if ((_draw.y2 & 0x80u) != 0u)
  {
    _math.s = _draw.y2;
    _math.r = _draw.x2;
    AddStep(StepAlongY(_math, _geometry), _draw.x1, _draw.y1);
    _draw.x2 = 0;
    _draw.y2 = 0;
  }

  // 6502: LL135 -- and the bottom, which is a subtraction rather than a sign test because the
  // edge is 144 and not zero. R and S are left holding the difference whether or not the clamp
  // runs, because that difference IS the step's argument.
  const SubResult overshoot = SubtractWithCarry(_draw.x2, SPACE_VIEW_BOTTOM, true);
  _math.r = overshoot.value;
  const SubResult beyond = SubtractWithCarry(_draw.y2, 0, overshoot.carry);
  _math.s = beyond.value;

  if (!beyond.carry)
  {
    return;
  }

  // 6502: LL139 -- and 143 rather than 144, for the same reason the right edge is 255.
  AddStep(StepAlongY(_math, _geometry), _draw.x1, _draw.y1);
  _draw.x2 = static_cast<std::uint8_t>(SPACE_VIEW_BOTTOM - 1);
  _draw.y2 = 0;
}


namespace
{

/// 6502: LL146 -- repack the three sixteen-bit coordinates into the four eight-bit ones the line
/// drawing wants. The order matters: `XX15+2` is read into `XX15+1` before it is overwritten.
void RepackClipped(DrawWorkspace& _draw, const GeometryWorkspace& _geometry) noexcept
{
  _draw.y1 = _draw.x2;
  _draw.x2 = _draw.xx15Plus4;
  _draw.y2 = _geometry.xx12[0];
}

/// 6502: the four swaps at LLX117 -- exchange the two ends of the line.
void SwapEnds(DrawWorkspace& _draw, GeometryWorkspace& _geometry) noexcept
{
  std::swap(_draw.x1, _draw.xx15Plus4);
  std::swap(_draw.y1, _draw.xx15Plus5);
  std::swap(_draw.x2, _geometry.xx12[0]);
  std::swap(_draw.y2, _geometry.xx12[1]);
}

/// True when both ends are so far off the same side that no part of the line can be on screen.
/// 6502: the four `BPL LL109` / `BMI LL109` tests at LL83, which are only reached when neither
/// end is on the screen.
bool BothEndsBeyondTheSameEdge(DrawWorkspace& _draw, GeometryWorkspace& _geometry) noexcept
{
  if (((_draw.y1 & _draw.xx15Plus5) & 0x80u) != 0u)
  {
    return true; // both x high bytes negative -- off the left
  }
  if (((_draw.y2 & _geometry.xx12[1]) & 0x80u) != 0u)
  {
    return true; // both y high bytes negative -- above
  }

  // Both x coordinates past 255: the high bytes minus one are still positive.
  _geometry.xx12[2] = static_cast<std::uint8_t>(_draw.xx15Plus5 - 1u);
  const std::uint8_t left = static_cast<std::uint8_t>(_draw.y1 - 1u);
  if (((left | _geometry.xx12[2]) & 0x80u) == 0u)
  {
    return true;
  }

  // And both below the bottom. The `CMP #Y*2` is only there for its carry -- the byte it
  // produces is thrown away and the `SBC #0` under it is what gets kept.
  const SubResult firstLow = SubtractWithCarry(_draw.x2, SPACE_VIEW_BOTTOM, true);
  _geometry.xx12[2] = SubtractWithCarry(_draw.y2, 0, firstLow.carry).value;

  const SubResult secondLow = SubtractWithCarry(_geometry.xx12[0], SPACE_VIEW_BOTTOM, true);
  const std::uint8_t second = SubtractWithCarry(_geometry.xx12[1], 0, secondLow.carry).value;

  return ((second | _geometry.xx12[2]) & 0x80u) == 0u;
}

/// 6502: LL115 to LL114 -- the line's gradient, scaled so that both differences fit in a byte,
/// with `T` saying which axis it is measured along.
void MeasureSlope(DrawWorkspace& _draw, GeometryWorkspace& _geometry, MathWorkspace& _math) noexcept
{
  const SubResult acrossLow = SubtractWithCarry(_draw.xx15Plus4, _draw.x1, true);
  _geometry.xx12[2] = acrossLow.value;
  const SubResult acrossHigh = SubtractWithCarry(_draw.xx15Plus5, _draw.y1, acrossLow.carry);
  _geometry.xx12[3] = acrossHigh.value;

  const SubResult downLow = SubtractWithCarry(_geometry.xx12[0], _draw.x2, true);
  _geometry.xx12[4] = downLow.value;
  const SubResult downHigh = SubtractWithCarry(_geometry.xx12[1], _draw.y2, downLow.carry);
  _geometry.xx12[5] = downHigh.value;

  // The direction of the slope, which is the two differences' signs EOR'd -- taken now, because
  // both are about to be made positive.
  _math.s = static_cast<std::uint8_t>(downHigh.value ^ _geometry.xx12[3]);

  if ((_geometry.xx12[5] & 0x80u) != 0u)
  {
    const SubResult low = SubtractWithCarry(0, _geometry.xx12[4], true);
    _geometry.xx12[4] = low.value;
    _geometry.xx12[5] = SubtractWithCarry(0, _geometry.xx12[5], low.carry).value;
  }

  // The x difference's high byte is negated into the ACCUMULATOR and never stored back. That
  // looks load-bearing and is not: XX12+3 is not read again between here and `LL116`, which
  // overwrites it with the slope direction from S. Writing the magnitude back is an equivalent
  // mutation and the sweep says so -- which is the only reason this comment is right, because
  // the first version of it claimed the opposite with a plausible argument attached (§6.29).
  std::uint8_t high = _geometry.xx12[3];
  if ((high & 0x80u) != 0u)
  {
    const SubResult low = SubtractWithCarry(0, _geometry.xx12[2], true);
    _geometry.xx12[2] = low.value;
    high = SubtractWithCarry(0, _geometry.xx12[3], low.carry).value;
  }

  // 6502: LL111 / LL112 -- halve both until each fits in one byte.
  while (high != 0u || _geometry.xx12[5] != 0u)
  {
    const bool intoAcross = (high & 0x01u) != 0u;
    high = static_cast<std::uint8_t>(high >> 1);
    _geometry.xx12[2] = RotateRight(_geometry.xx12[2], intoAcross).value;

    const bool intoDown = (_geometry.xx12[5] & 0x01u) != 0u;
    _geometry.xx12[5] = static_cast<std::uint8_t>(_geometry.xx12[5] >> 1);
    _geometry.xx12[4] = RotateRight(_geometry.xx12[4], intoDown).value;
  }

  // 6502: LL113 -- X is the now-zero high byte, so T starts at zero and the steep branch
  // decrements it to 255.
  _math.t = 0;

  if (_geometry.xx12[2] >= _geometry.xx12[4])
  {
    _math.q = _geometry.xx12[2];
    (void)DivideToR(_math, _geometry.xx12[4]);
    return;
  }

  // 6502: LL114 -- steep.
  _math.q = _geometry.xx12[4];
  (void)DivideToR(_math, _geometry.xx12[2]);
  _math.t = static_cast<std::uint8_t>(_math.t - 1u);
}

} // namespace

bool ClipLineKeepingSwap(DrawWorkspace& _draw, GeometryWorkspace& _geometry, MathWorkspace& _math,
                         ClipState& _clip, std::uint8_t _a) noexcept
{
  if ((_clip.dontclip & 0x80u) != 0u)
  {
    RepackClipped(_draw, _geometry);
    return false;
  }

  // 6502: LL107 -- is the FAR end on the screen? Both its high bytes zero and its y under 144.
  constexpr std::uint8_t LAST_ROW = static_cast<std::uint8_t>(SPACE_VIEW_BOTTOM - 1);
  std::uint8_t state = LAST_ROW;
  if ((_a | _geometry.xx12[1]) == 0u && LAST_ROW >= _geometry.xx12[0])
  {
    state = 0;
  }
  _clip.xx13 = state;

  // And the near end, by the same test. If both are on screen there is nothing to do; if only
  // the near one is, the state is halved, which is what turns 143 into 71 and clears bit 7.
  if ((_draw.y1 | _draw.y2) == 0u && LAST_ROW >= _draw.x2)
  {
    if (_clip.xx13 == 0u)
    {
      RepackClipped(_draw, _geometry);
      return false;
    }
    _clip.xx13 = static_cast<std::uint8_t>(_clip.xx13 >> 1);
  }

  // 6502: LL83 -- with neither end on screen, four cheap rejections before any arithmetic.
  if ((_clip.xx13 & 0x80u) != 0u && BothEndsBeyondTheSameEdge(_draw, _geometry))
  {
    return true;
  }

  MeasureSlope(_draw, _geometry, _math);

  // 6502: LL116 -- the gradient and its direction, where LL118 and LL120/LL123 will read them.
  _geometry.xx12[2] = _math.r;
  _geometry.xx12[3] = _math.s;

  const bool nearEndOnScreen = _clip.xx13 != 0u && (_clip.xx13 & 0x80u) == 0u;
  if (!nearEndOnScreen)
  {
    // 6502: LL138 -- clip the near end.
    MovePointOnScreen(_draw, _geometry, _math);

    if ((_clip.xx13 & 0x80u) == 0u)
    {
      // The far end was already on screen, so one clip was the whole job.
      RepackClipped(_draw, _geometry);
      return false;
    }

    // 6502: LL117 -- and if clipping did not actually bring it on screen, the line misses.
    if ((_draw.y1 | _draw.y2) != 0u || _draw.x2 >= SPACE_VIEW_BOTTOM)
    {
      return true;
    }
  }

  // 6502: LLX117 -- put the other end in the near slot and clip that too.
  SwapEnds(_draw, _geometry);
  MovePointOnScreen(_draw, _geometry, _math);
  _clip.swap = static_cast<std::uint8_t>(_clip.swap - 1u);

  RepackClipped(_draw, _geometry);
  return false;
}

bool ClipLine(DrawWorkspace& _draw, GeometryWorkspace& _geometry, MathWorkspace& _math,
              ClipState& _clip) noexcept
{
  _clip.swap = 0;
  return ClipLineKeepingSwap(_draw, _geometry, _math, _clip, _draw.xx15Plus5);
}

} // namespace Elite
