#include "pch.h"

#include "ShipDraw.h"

#include "EliteTypes.h"
#include "ShipBlueprint.h"

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
    _math.t = CombineSigned(_math, static_cast<std::uint8_t>(sign[1] ^ _geometry.xx16[base + 3])).value;

    _math.q = magnitude[2];
    _math.q = MultiplyByLog(_math, _geometry.xx16[base + 4]);
    _math.r = _math.t;

    _geometry.xx12[vector * 2u] =
      CombineSigned(_math, static_cast<std::uint8_t>(sign[2] ^ _geometry.xx16[base + 5])).value;
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


namespace
{

/// 6502: LL15 and LL21 -- copy the ship's three orientation vectors into XX16 and scale each
/// magnitude down by 197. The `ASL A` on the magnitude puts its top bit into the carry and the
/// `ROL A` on the sign byte rotates it in, so what gets divided is the pair read as nine bits.
void ScaleOrientation(const ShipBlock& _work, GeometryWorkspace& _geometry, MathWorkspace& _math) noexcept
{
  for (int byte = 5; byte >= 0; --byte)
  {
    const std::size_t at = static_cast<std::size_t>(byte);
    _geometry.xx16[at] = _work[21u + at];
    _geometry.xx16[at + 6u] = _work[15u + at];
    _geometry.xx16[at + 12u] = _work[9u + at];
  }

  _math.q = 197;
  for (int index = 16; index >= 0; index -= 2)
  {
    const std::size_t at = static_cast<std::size_t>(index);
    const ShiftResult raised = RotateLeftValue(_geometry.xx16[at], false);
    (void)DivideToR(_math, RotateLeft(_geometry.xx16[at + 1u], raised.carry).value);
    _geometry.xx16[at] = _math.r;
  }
}

/// 6502: the twenty-four instructions at the top of LL42 -- transpose XX16, so the three vectors
/// become their own x, y and z components. `LL51` is then the same code doing a different
/// rotation, which is why there is a transpose rather than a second routine.
void TransposeOrientation(GeometryWorkspace& _geometry) noexcept
{
  std::swap(_geometry.xx16[2], _geometry.xx16[6]);
  std::swap(_geometry.xx16[3], _geometry.xx16[7]);
  std::swap(_geometry.xx16[4], _geometry.xx16[12]);
  std::swap(_geometry.xx16[5], _geometry.xx16[13]);
  std::swap(_geometry.xx16[10], _geometry.xx16[14]);
  std::swap(_geometry.xx16[11], _geometry.xx16[15]);
}

/// 6502: LL89 -- the dot product of a face's normal in XX12 with the position in XX15, whose SIGN
/// decides whether the face is drawn. What gets stored is the MAGNITUDE, so a face seen exactly
/// edge-on comes out invisible.
std::uint8_t FaceVisibility(const DrawWorkspace& _draw, const GeometryWorkspace& _geometry,
                            MathWorkspace& _math) noexcept
{
  _math.q = _geometry.xx12[0];
  _math.t = MultiplyByLog(_math, _draw.x1);
  _math.s = static_cast<std::uint8_t>(_geometry.xx12[1] ^ _draw.y1);

  _math.q = _geometry.xx12[2];
  _math.q = MultiplyByLog(_math, _draw.x2);
  _math.r = _math.t;
  _math.t = CombineSigned(_math, static_cast<std::uint8_t>(_geometry.xx12[3] ^ _draw.y2)).value;

  _math.q = _geometry.xx12[4];
  _math.q = MultiplyByLog(_math, _draw.xx15Plus4);
  _math.r = _math.t;
  const std::uint8_t magnitude =
    CombineSigned(_math, static_cast<std::uint8_t>(_draw.xx15Plus5 ^ _geometry.xx12[5])).value;

  // `BIT S / BMI P%+4 / LDA #0` -- the branch skips the zero, so a negative S keeps the answer.
  return ((_math.s & 0x80u) != 0u) ? magnitude : std::uint8_t{ 0 };
}

/*
 * 6502: the sixteen-bit add-or-subtract at LL49/LL52 and LL53/LL54 -- the ship's own coordinate
 * plus or minus the rotated vertex, under the two signs, negated again when the subtraction
 * crossed zero.
 *
 * The two halves are written differently in the original -- `LDA #1 / SBC XX15` against
 * `EOR #FF / ADC #1`, and the sign flip on opposite sides of the branch that increments the high
 * byte -- and they compute the same thing, which is why one function serves both.
 */
void PlaceVertexAxis(std::uint8_t& _low, std::uint8_t& _high, std::uint8_t& _sign,
                     std::uint8_t _productLow, std::uint8_t _productSign, const ShipBlock& _work,
                     std::size_t _axis) noexcept
{
  _sign = _work[_axis + 2u];

  if (((_sign ^ _productSign) & 0x80u) == 0u)
  {
    const AddResult sum = AddWithCarry(_productLow, _work[_axis], false);
    _low = sum.value;
    _high = AddWithCarry(_work[_axis + 1u], 0, sum.carry).value;
    return;
  }

  const SubResult low = SubtractWithCarry(_work[_axis], _productLow, true);
  _low = low.value;
  const SubResult high = SubtractWithCarry(_work[_axis + 1u], 0, low.carry);
  _high = high.value;

  if (high.carry)
  {
    return;
  }

  _high = static_cast<std::uint8_t>(high.value ^ 0xFFu);

  const SubResult negated = SubtractWithCarry(1, _low, high.carry);
  _low = negated.value;

  // `BCC P%+4 / INC XX15+1`, so the increment happens when the negation did NOT borrow -- which
  // is only when the low byte was zero and the carry rippled all the way up.
  if (negated.carry)
  {
    _high = static_cast<std::uint8_t>(_high + 1u);
  }

  _sign = static_cast<std::uint8_t>(_sign ^ 0x80u);
}

/// 6502: LL80 -- put a clipped line's four bytes on the ship's line heap.
void PushHeapLine(LineHeap& _heap, std::uint16_t _address, const DrawWorkspace& _draw,
                  std::uint8_t& _next) noexcept
{
  _heap.Write(static_cast<std::uint16_t>(_address + _next), _draw.x1);
  _heap.Write(static_cast<std::uint16_t>(_address + _next + 1u), _draw.y1);
  _heap.Write(static_cast<std::uint16_t>(_address + _next + 2u), _draw.x2);
  _heap.Write(static_cast<std::uint16_t>(_address + _next + 3u), _draw.y2);
  _next = static_cast<std::uint8_t>(_next + 4u);
}

/// Whether the two faces a nibble pair names are both invisible, which is what makes a vertex or
/// an edge not worth drawing. 6502: the four `LDA XX2,X / BNE` tests in parts 6 and 10.
bool EitherFaceVisible(const GeometryWorkspace& _geometry, std::uint8_t _pair) noexcept
{
  return _geometry.xx2[static_cast<std::size_t>(_pair & 0x0Fu)] != 0u
         || _geometry.xx2[static_cast<std::size_t>(_pair >> 4)] != 0u;
}

} // namespace


void DrawShip(Canvas& _canvas, DrawWorkspace& _draw, GeometryWorkspace& _geometry,
              MathWorkspace& _math, ClipState& _clip, Projection& _screen, ShipBlock& _work,
              ShipBlock& _slot, LineHeap& _heap, std::uint16_t _blueprint, std::uint8_t _type,
              ShipDrawEffects& _effects) noexcept
{
  // ---- part 1: is there anything to draw at all? ------------------------------------------

  // 6502: LL25 -- a negative type is the planet or the sun, which is a different routine.
  if ((_type & 0x80u) != 0u)
  {
    _effects.DrawPlanetOrSun();
    return;
  }

  _geometry.xx4 = 31;

  // 6502: bit 7 of NEWB -- scooped or docked, so take it off the screen and forget it.
  if ((_work[SHIP_FLAGS_OFFSET] & 0x80u) != 0u)
  {
    EraseShip(_canvas, _draw, _work, _heap);
    return;
  }

  const std::uint8_t entryState = _work[SHIP_STATE_OFFSET];
  if ((entryState & SHIP_STATE_EXPLODING) == 0u && (entryState & SHIP_STATE_KILLED) != 0u)
  {
    // Killed and not yet exploding. Bits 6 and 7 are cleared by the same instruction that sets
    // bit 5, so the ship stops firing in the moment it starts to blow up.
    _work[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>((SHIP_STATE_EXPLODING | entryState) & 0x3Fu);

    // Written through INF into the ship's block in K% rather than into INWK, so that the
    // caller's copy back does not undo them.
    _slot[28] = 0;
    _slot[30] = 0;

    EraseShip(_canvas, _draw, _work, _heap);
    _effects.SeedExplosionCloud(_heap, ShipHeapAddress(_work), _blueprint);
  }

  // 6502: EE28 / EE49 and LL10 -- four ways of being not worth drawing, sharing one exit. The
  // two coordinate tests are sixteen-bit: |x| or |y| at least as large as z puts the ship outside
  // a ninety-degree view whatever the projection would make of it.
  bool gone = (_work[SHIP_Z_OFFSET + 2] & 0x80u) != 0u;
  if (!gone)
  {
    gone = _work[SHIP_Z_OFFSET + 1] >= 192u;
  }
  for (std::size_t axis = 0; !gone && axis < 2u; ++axis)
  {
    const std::size_t at = (axis == 0u) ? SHIP_X_OFFSET : SHIP_Y_OFFSET;
    const SubResult low = SubtractWithCarry(_work[at], _work[SHIP_Z_OFFSET], true);
    gone = SubtractWithCarry(_work[at + 1u], _work[SHIP_Z_OFFSET + 1], low.carry).carry;
  }

  if (gone)
  {
    // 6502: LL14.
    if ((_work[SHIP_STATE_OFFSET] & SHIP_STATE_EXPLODING) == 0u)
    {
      EraseShip(_canvas, _draw, _work, _heap);
      return;
    }

    _work[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>(_work[SHIP_STATE_OFFSET] & 0xF7u);
    _effects.DrawExplosion();
    return;
  }

  // ---- part 2: how far away is it, and is that too far? -----------------------------------

  // Blueprint byte 6 is a vertex's offset in XX3, and 255 there means "this one did not
  // project". The laser line in part 9 reads it back and gives up when it is still 255.
  const std::uint8_t laserVertex = ShipByte(static_cast<std::uint16_t>(_blueprint + 6u));
  _geometry.xx3[laserVertex] = 255;
  _geometry.xx3[static_cast<std::size_t>(laserVertex) + 1u] = 255;

  // z divided by sixteen into (A T), and then by another eight. The `ROR A` after the fourth
  // `LSR A` picks up the carry that shift left, so the two halves are one number and not two.
  std::uint8_t distanceLow = _work[SHIP_Z_OFFSET];
  std::uint8_t distanceHigh = _work[SHIP_Z_OFFSET + 1];
  for (int shift = 0; shift < 3; ++shift)
  {
    const bool into = (distanceHigh & 0x01u) != 0u;
    distanceHigh = static_cast<std::uint8_t>(distanceHigh >> 1);
    distanceLow = RotateRight(distanceLow, into).value;
  }
  const bool spare = (distanceHigh & 0x01u) != 0u;
  distanceHigh = static_cast<std::uint8_t>(distanceHigh >> 1);

  if (distanceHigh == 0u)
  {
    _geometry.xx4 = static_cast<std::uint8_t>(RotateRight(distanceLow, spare).value >> 3);
  }
  else if (ShipByte(static_cast<std::uint16_t>(_blueprint + 13u)) < _work[SHIP_Z_OFFSET + 1]
           && (_work[SHIP_STATE_OFFSET] & SHIP_STATE_EXPLODING) == 0u)
  {
    // 6502: LL13 -- past the blueprint's own visibility distance, so a dot will do.
    DrawShipAsPoint(_canvas, _draw, _work, _heap, _math, _screen);
    return;
  }

  // ---- part 3: the orientation vectors, scaled -------------------------------------------

  ScaleOrientation(_work, _geometry, _math);

  for (int byte = 8; byte >= 0; --byte)
  {
    _geometry.xx18[static_cast<std::size_t>(byte)] = _work[static_cast<std::size_t>(byte)];
  }
  _geometry.xx2[15] = 255;

  const std::uint8_t faceBytes = ShipByte(static_cast<std::uint16_t>(_blueprint + 12u));

  // ---- parts 4 and 5: which faces can be seen --------------------------------------------

  if ((_work[SHIP_STATE_OFFSET] & SHIP_STATE_EXPLODING) != 0u)
  {
    // 6502: EE30 -- an exploding ship shows every face and every vertex, so that the whole cloud
    // can be built out of them.
    for (int face = faceBytes >> 2; face >= 0; --face)
    {
      _geometry.xx2[static_cast<std::size_t>(face)] = 255;
    }
    _geometry.xx4 = 0;
  }
  else if (faceBytes != 0u)
  {
    // 6502: EE29 -- halve the ship's position until its z fits in a byte, counting the halvings
    // on top of the blueprint's own scale in byte 18.
    _geometry.xx20 = faceBytes;

    std::uint8_t shifts = ShipByte(static_cast<std::uint16_t>(_blueprint + 18u));
    std::uint8_t z = _geometry.xx18[7];
    while (z != 0u)
    {
      ++shifts;

      const bool intoY = (_geometry.xx18[4] & 0x01u) != 0u;
      _geometry.xx18[4] = static_cast<std::uint8_t>(_geometry.xx18[4] >> 1);
      _geometry.xx18[3] = RotateRight(_geometry.xx18[3], intoY).value;

      const bool intoX = (_geometry.xx18[1] & 0x01u) != 0u;
      _geometry.xx18[1] = static_cast<std::uint8_t>(_geometry.xx18[1] >> 1);
      _geometry.xx18[0] = RotateRight(_geometry.xx18[0], intoX).value;

      const bool intoZ = (z & 0x01u) != 0u;
      z = static_cast<std::uint8_t>(z >> 1);
      _geometry.xx18[6] = RotateRight(_geometry.xx18[6], intoZ).value;
    }
    _geometry.xx17 = shifts;

    // 6502: LL91 -- the position, rotated into the ship's own frame.
    _draw.xx15Plus5 = _geometry.xx18[8];
    _draw.x1 = _geometry.xx18[0];
    _draw.y1 = _geometry.xx18[2];
    _draw.x2 = _geometry.xx18[3];
    _draw.y2 = _geometry.xx18[5];
    _draw.xx15Plus4 = _geometry.xx18[6];
    DotProducts(_draw, _geometry, _math);
    _geometry.xx18[0] = _geometry.xx12[0];
    _geometry.xx18[2] = _geometry.xx12[1];
    _geometry.xx18[3] = _geometry.xx12[2];
    _geometry.xx18[5] = _geometry.xx12[3];
    _geometry.xx18[6] = _geometry.xx12[4];
    _geometry.xx18[8] = _geometry.xx12[5];

    const AddResult faceLow =
      AddWithCarry(ShipByte(static_cast<std::uint16_t>(_blueprint + 4u)),
                   static_cast<std::uint8_t>(_blueprint), false);
    const std::uint8_t faceHigh =
      AddWithCarry(ShipByte(static_cast<std::uint16_t>(_blueprint + 17u)),
                   static_cast<std::uint8_t>(_blueprint >> 8), faceLow.carry)
        .value;
    _geometry.v = static_cast<std::uint16_t>(faceLow.value | (faceHigh << 8));

    std::uint8_t at = 0;
    do
    {
      // 6502: LL86 -- a face whose own distance is under the ship's is taken as visible without
      // the arithmetic.
      const std::uint8_t flags = ShipByte(static_cast<std::uint16_t>(_geometry.v + at));
      _geometry.xx12[1] = flags;

      if ((flags & 0x1Fu) < _geometry.xx4)
      {
        _geometry.xx2[static_cast<std::size_t>(at >> 2)] = 255;
        at = static_cast<std::uint8_t>(at + 4u);
        continue;
      }

      // 6502: LL87 -- the face's normal, with its three sign bits spread out by doubling.
      _geometry.xx12[3] = static_cast<std::uint8_t>(flags << 1);
      _geometry.xx12[5] = static_cast<std::uint8_t>(flags << 2);
      _geometry.xx12[0] = ShipByte(static_cast<std::uint16_t>(_geometry.v + at + 1u));
      _geometry.xx12[2] = ShipByte(static_cast<std::uint16_t>(_geometry.v + at + 2u));
      _geometry.xx12[4] = ShipByte(static_cast<std::uint16_t>(_geometry.v + at + 3u));

      if (_geometry.xx17 >= 4u)
      {
        // 6502: LL143 -- the position is already small enough to use as it stands.
        _draw.x1 = _geometry.xx18[0];
        _draw.y1 = _geometry.xx18[2];
        _draw.x2 = _geometry.xx18[3];
        _draw.y2 = _geometry.xx18[5];
        _draw.xx15Plus4 = _geometry.xx18[6];
        _draw.xx15Plus5 = _geometry.xx18[8];
      }
      else
      {
        /*
         * 6502: LL92 to LL94, with `ovflw` as the retry.
         *
         * Scale the normal down by the shift count and add the position to it, one axis at a
         * time. Any of the three overflowing sends the whole thing back to the start with the
         * POSITION halved and the scale reset to one -- so this is a loop that can run several
         * times, and the partial results it leaves behind on the way are what the original
         * leaves too.
         */
        std::uint8_t scale = _geometry.xx17;
        for (;;)
        {
          _draw.x1 = _geometry.xx12[0];
          _draw.x2 = _geometry.xx12[2];
          std::uint8_t third = _geometry.xx12[4];
          for (std::uint8_t left = scale; left != 0u; --left)
          {
            _draw.x1 = static_cast<std::uint8_t>(_draw.x1 >> 1);
            _draw.x2 = static_cast<std::uint8_t>(_draw.x2 >> 1);
            third = static_cast<std::uint8_t>(third >> 1);
          }

          _math.r = third;
          _math.s = _geometry.xx12[5];
          _math.q = _geometry.xx18[6];
          const SignedSum alongZ = CombineSigned(_math, _geometry.xx18[8]);
          if (!alongZ.carry)
          {
            _draw.xx15Plus4 = alongZ.value;
            _draw.xx15Plus5 = _math.s;

            _math.r = _draw.x1;
            _math.s = _geometry.xx12[1];
            _math.q = _geometry.xx18[0];
            const SignedSum alongX = CombineSigned(_math, _geometry.xx18[2]);
            if (!alongX.carry)
            {
              _draw.x1 = alongX.value;
              _draw.y1 = _math.s;

              _math.r = _draw.x2;
              _math.s = _geometry.xx12[3];
              _math.q = _geometry.xx18[3];
              const SignedSum alongY = CombineSigned(_math, _geometry.xx18[5]);
              if (!alongY.carry)
              {
                _draw.x2 = alongY.value;
                _draw.y2 = _math.s;
                break;
              }
            }
          }

          // 6502: ovflw.
          _geometry.xx18[0] = static_cast<std::uint8_t>(_geometry.xx18[0] >> 1);
          _geometry.xx18[6] = static_cast<std::uint8_t>(_geometry.xx18[6] >> 1);
          _geometry.xx18[3] = static_cast<std::uint8_t>(_geometry.xx18[3] >> 1);
          scale = 1;
        }
      }

      _geometry.xx2[static_cast<std::size_t>(at >> 2)] = FaceVisibility(_draw, _geometry, _math);
      at = static_cast<std::uint8_t>(at + 4u);
    } while (at < _geometry.xx20);
  }

  // ---- parts 6 to 8: project the vertices the visible faces touch --------------------------

  TransposeOrientation(_geometry);

  _geometry.xx20 = ShipByte(static_cast<std::uint16_t>(_blueprint + 8u));
  _geometry.v = static_cast<std::uint16_t>(_blueprint + 20u);
  _geometry.cnt = 0;

  for (std::uint8_t vertex = 0;;)
  {
    _geometry.xx17 = vertex;

    _draw.x1 = ShipByte(static_cast<std::uint16_t>(_geometry.v + vertex));
    _draw.x2 = ShipByte(static_cast<std::uint16_t>(_geometry.v + vertex + 1u));
    _draw.xx15Plus4 = ShipByte(static_cast<std::uint16_t>(_geometry.v + vertex + 2u));
    const std::uint8_t flags = ShipByte(static_cast<std::uint16_t>(_geometry.v + vertex + 3u));

    const bool nearEnough = (flags & 0x1Fu) >= _geometry.xx4;
    const bool visible = nearEnough
                         && (EitherFaceVisible(_geometry,
                                               ShipByte(static_cast<std::uint16_t>(_geometry.v + vertex + 4u)))
                             || EitherFaceVisible(_geometry,
                                                  ShipByte(static_cast<std::uint16_t>(_geometry.v + vertex + 5u))));

    if (visible)
    {
      // 6502: LL49 -- the vertex's three sign bits, spread out by doubling, then rotated into
      // the player's frame and added to the ship's own position.
      _draw.y1 = flags;
      _draw.y2 = static_cast<std::uint8_t>(flags << 1);
      _draw.xx15Plus5 = static_cast<std::uint8_t>(flags << 2);

      DotProducts(_draw, _geometry, _math);

      PlaceVertexAxis(_draw.x1, _draw.y1, _draw.x2, _geometry.xx12[0], _geometry.xx12[1], _work,
                      SHIP_X_OFFSET);
      PlaceVertexAxis(_draw.y2, _draw.xx15Plus4, _draw.xx15Plus5, _geometry.xx12[2],
                      _geometry.xx12[3], _work, SHIP_Y_OFFSET);

      // 6502: LL55 / LL56 / LL140 -- and z, which is a plain sixteen-bit add or subtract with a
      // floor of four rather than a sign-magnitude one, because a vertex behind the player has
      // to be pulled in front of it before anything is divided by it.
      if ((_geometry.xx12[5] & 0x80u) == 0u)
      {
        const AddResult sum = AddWithCarry(_geometry.xx12[4], _work[SHIP_Z_OFFSET], false);
        _math.t = sum.value;
        _math.u = AddWithCarry(_work[SHIP_Z_OFFSET + 1], 0, sum.carry).value;
      }
      else
      {
        const SubResult low = SubtractWithCarry(_work[SHIP_Z_OFFSET], _geometry.xx12[4], true);
        _math.t = low.value;
        const SubResult high = SubtractWithCarry(_work[SHIP_Z_OFFSET + 1], 0, low.carry);
        _math.u = high.value;

        if (!high.carry || (high.value == 0u && low.value < 4u))
        {
          _math.u = 0;
          _math.t = 4;
        }
      }

      // 6502: LL57 -- halve all three until the two coordinates and the distance fit in a byte
      // each, so that the division below is an eight-bit one.
      while ((_math.u | _draw.y1 | _draw.xx15Plus4) != 0u)
      {
        const bool intoX = (_draw.y1 & 0x01u) != 0u;
        _draw.y1 = static_cast<std::uint8_t>(_draw.y1 >> 1);
        _draw.x1 = RotateRight(_draw.x1, intoX).value;

        const bool intoY = (_draw.xx15Plus4 & 0x01u) != 0u;
        _draw.xx15Plus4 = static_cast<std::uint8_t>(_draw.xx15Plus4 >> 1);
        _draw.y2 = RotateRight(_draw.y2, intoY).value;

        const bool intoZ = (_math.u & 0x01u) != 0u;
        _math.u = static_cast<std::uint8_t>(_math.u >> 1);
        _math.t = RotateRight(_math.t, intoZ).value;
      }

      // 6502: LL60 to LL70 -- the projection itself, and the only place in the port where the
      // divide is picked by which of two routines can do it: LL28 when the coordinate is smaller
      // than the distance, LL61 when it is not.
      std::uint8_t x = _geometry.cnt;

      _math.q = _math.t;
      if (_draw.x1 < _math.q)
      {
        (void)DivideToR(_math, _draw.x1);
      }
      else
      {
        DivideToUR(_math, _draw.x1);
      }

      if ((_draw.x2 & 0x80u) != 0u)
      {
        // 6502: LL62 -- 128 - (U R), for a vertex to the left of centre.
        const SubResult low = SubtractWithCarry(128, _math.r, true);
        _geometry.xx3[x] = low.value;
        ++x;
        _geometry.xx3[x] = SubtractWithCarry(0, _math.u, low.carry).value;
      }
      else
      {
        const AddResult low = AddWithCarry(_math.r, 128, false);
        _geometry.xx3[x] = low.value;
        ++x;
        _geometry.xx3[x] = AddWithCarry(_math.u, 0, low.carry).value;
      }

      // 6502: LL66 -- and the same again for y, with U cleared first because `LL28` does not
      // write it and the last vertex's value would otherwise be added in.
      _math.u = 0;
      _math.q = _math.t;
      if (_draw.y2 < _math.q)
      {
        (void)DivideToR(_math, _draw.y2);
      }
      else
      {
        DivideToUR(_math, _draw.y2);
      }

      ++x;
      if ((_draw.xx15Plus5 & 0x80u) != 0u)
      {
        // 6502: LL70 -- below the centre of the view.
        const AddResult low = AddWithCarry(SPACE_VIEW_CENTRE_Y, _math.r, false);
        _geometry.xx3[x] = low.value;
        ++x;
        _geometry.xx3[x] = AddWithCarry(0, _math.u, low.carry).value;
      }
      else
      {
        const SubResult low = SubtractWithCarry(SPACE_VIEW_CENTRE_Y, _math.r, true);
        _geometry.xx3[x] = low.value;
        ++x;
        _geometry.xx3[x] = SubtractWithCarry(0, _math.u, low.carry).value;
      }
    }

    // 6502: LL50 -- on to the next vertex, six bytes along, and stop when the count runs out or
    // the index wraps. The carry out of CNT's addition is what feeds XX17's, so a heap that has
    // filled past 255 ends the loop as well.
    const AddResult nextCnt = AddWithCarry(_geometry.cnt, 4, false);
    _geometry.cnt = nextCnt.value;
    const AddResult nextVertex = AddWithCarry(_geometry.xx17, 6, nextCnt.carry);
    vertex = nextVertex.value;

    if (nextVertex.carry || vertex >= _geometry.xx20)
    {
      break;
    }
  }

  // ---- part 9: the ship is on the screen from here, and the laser goes on the heap ---------

  if ((_work[SHIP_STATE_OFFSET] & SHIP_STATE_EXPLODING) != 0u)
  {
    _work[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>(_work[SHIP_STATE_OFFSET] | SHIP_STATE_DRAWN);
    _effects.DrawExplosion();
    return;
  }

  // 6502: EE31 -- rub out the last frame's ship, then mark this one as being on the screen.
  const std::uint16_t heap = ShipHeapAddress(_work);
  if ((_work[SHIP_STATE_OFFSET] & SHIP_STATE_DRAWN) != 0u)
  {
    DrawShipLines(_canvas, _draw, _heap, heap);
  }
  _work[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>(_work[SHIP_STATE_OFFSET] | SHIP_STATE_DRAWN);

  _geometry.xx20 = ShipByte(static_cast<std::uint16_t>(_blueprint + 9u));
  _geometry.xx17 = 0;
  _math.u = 1;

  if ((_work[SHIP_STATE_OFFSET] & SHIP_STATE_FIRING) != 0u)
  {
    _work[SHIP_STATE_OFFSET] = static_cast<std::uint8_t>(_work[SHIP_STATE_OFFSET] & 0xBFu);

    const std::size_t muzzle = ShipByte(static_cast<std::uint16_t>(_blueprint + 6u));
    _draw.x1 = _geometry.xx3[muzzle];
    _draw.y1 = _geometry.xx3[muzzle + 1u];

    // Both bytes are tested by incrementing them, so 255 -- which is what part 2 wrote there and
    // what a vertex that did not project leaves -- is the one value that means "no laser".
    if (static_cast<std::uint8_t>(_draw.x1 + 1u) != 0u
        && static_cast<std::uint8_t>(_draw.y1 + 1u) != 0u)
    {
      _draw.x2 = _geometry.xx3[muzzle + 2u];
      _draw.y2 = _geometry.xx3[muzzle + 3u];
      _draw.xx15Plus4 = 0;
      _draw.xx15Plus5 = 0;
      _geometry.xx12[1] = 0;
      _geometry.xx12[0] = _work[SHIP_Z_OFFSET];

      // The laser fires towards the player, so the far end is the origin -- and to the left of
      // it when the ship is to the left, which is the whole of this `DEC`.
      if ((_work[SHIP_X_OFFSET + 2] & 0x80u) != 0u)
      {
        _draw.xx15Plus4 = 255;
      }

      if (!ClipLine(_draw, _geometry, _math, _clip))
      {
        std::uint8_t next = _math.u;
        PushHeapLine(_heap, heap, _draw, next);
        _math.u = next;
      }
    }
  }

  // ---- parts 10 and 11: the edges ---------------------------------------------------------

  const AddResult edgeLow = AddWithCarry(ShipByte(static_cast<std::uint16_t>(_blueprint + 3u)),
                                         static_cast<std::uint8_t>(_blueprint), false);
  const std::uint8_t edgeHigh = AddWithCarry(ShipByte(static_cast<std::uint16_t>(_blueprint + 16u)),
                                             static_cast<std::uint8_t>(_blueprint >> 8), edgeLow.carry)
                                  .value;
  _geometry.v = static_cast<std::uint16_t>(edgeLow.value | (edgeHigh << 8));
  _math.t1 = ShipByte(static_cast<std::uint16_t>(_blueprint + 5u));

  for (;;)
  {
    // 6502: LL75 -- four bytes per edge: how far away it stays visible, the two faces it joins,
    // and the two vertices it runs between.
    const std::uint8_t distance = ShipByte(_geometry.v);
    if (distance >= _geometry.xx4
        && EitherFaceVisible(_geometry, ShipByte(static_cast<std::uint16_t>(_geometry.v + 1u))))
    {
      const std::size_t from = ShipByte(static_cast<std::uint16_t>(_geometry.v + 2u));
      const std::size_t to = ShipByte(static_cast<std::uint16_t>(_geometry.v + 3u));

      _draw.y1 = _geometry.xx3[from + 1u];
      _draw.x1 = _geometry.xx3[from];
      _draw.x2 = _geometry.xx3[from + 2u];
      _draw.y2 = _geometry.xx3[from + 3u];
      _draw.xx15Plus4 = _geometry.xx3[to];
      _geometry.xx12[1] = _geometry.xx3[to + 3u];
      _geometry.xx12[0] = _geometry.xx3[to + 2u];
      _draw.xx15Plus5 = _geometry.xx3[to + 1u];

      if (!ClipLineKeepingSwap(_draw, _geometry, _math, _clip, _draw.xx15Plus5))
      {
        // 6502: LL80 -- and stop as soon as the heap this blueprint asked for is full.
        std::uint8_t next = _math.u;
        PushHeapLine(_heap, heap, _draw, next);
        _math.u = next;
        if (next >= _math.t1)
        {
          break;
        }
      }
    }

    // 6502: LL78.
    _geometry.xx17 = static_cast<std::uint8_t>(_geometry.xx17 + 1u);
    if (_geometry.xx17 >= _geometry.xx20)
    {
      break;
    }
    _geometry.v = static_cast<std::uint16_t>(_geometry.v + 4u);
  }

  // 6502: LL81 -- the heap's length goes in byte 0, and then it is drawn.
  StoreLineCountAndDraw(_canvas, _draw, _heap, heap, _math.u);
}

} // namespace Elite
