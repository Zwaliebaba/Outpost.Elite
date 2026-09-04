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

} // namespace Elite
