#include "pch.h"

#include "ShipDraw.h"

#include "EliteTypes.h"

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

} // namespace Elite
