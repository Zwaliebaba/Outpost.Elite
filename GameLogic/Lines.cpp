#include "pch.h"

#include "Canvas.h"

#include "EliteTypes.h"
#include "LookupTables.h"

/*
 * The pixel primitives (slice 1d-a).
 *
 * Every one of these ends in an exclusive-or of a byte into the bitmap, and the byte is a mask
 * out of a table rather than a colour. Reading them, the thing to keep hold of is that a byte is
 * four two-bit pixels: a mask that looks like it covers one pixel may cover a bit of two, and
 * three of TWOS2's eight entries do exactly that (ADR-002 section 7).
 *
 * The original addresses the bitmap through a zero-page pointer SC with the byte offset in Y, so
 * that adding 8 to Y steps one character cell to the right. The port computes a flat offset
 * instead, which is the same arithmetic with the split removed; where a routine's structure
 * depends on the split, the comment says so.
 */
namespace Elite
{

void PlotPixel(Canvas& _canvas, DrawWorkspace& _work, std::uint8_t _x, std::uint8_t _y) noexcept
{
  const std::uint8_t mask = DASH_MASK_TABLE[_x & 0x07u];
  std::uint8_t subRow = static_cast<std::uint8_t>(_y & 0x07u);
  const std::uint16_t cell = static_cast<std::uint16_t>(Canvas::RowOffset(_y) + (_x & 0xF8u));

  if (_work.zz >= 144)
  {
    // 6502: PX3 -- far away, so one mark and nothing else.
    _canvas.ExclusiveOr(static_cast<std::uint16_t>(cell + subRow), mask);
    return;
  }

  _canvas.ExclusiveOr(static_cast<std::uint16_t>(cell + subRow), mask);

  if (_work.zz >= 80)
  {
    // 6502: PX13 -- a middle distance, so the dash on its own is enough.
    return;
  }

  /*
   * 6502: DEY / BPL PX3 / LDY #1.
   *
   * Closer still, so a second dash goes above the first to make a square -- except on the top
   * row of a character cell, where decrementing the pixel row would leave the cell entirely.
   * The original does not clamp: it notices Y went negative and sets it to 1, drawing BELOW
   * instead. The square moves rather than being clipped, and the port keeps that.
   */
  subRow = (subRow == 0) ? 1u : static_cast<std::uint8_t>(subRow - 1u);
  _canvas.ExclusiveOr(static_cast<std::uint16_t>(cell + subRow), mask);
}

void PlotRelativePixel(Canvas& _canvas, DrawWorkspace& _work) noexcept
{
  /*
   * 6502: PIXEL2. The coordinates arrive as sign-magnitude offsets from the centre of the space
   * view, and come out as screen coordinates: x measured from the left edge, y downwards.
   *
   * The x conversion is the sign-magnitude idiom this codebase keeps running into: negate the
   * magnitude by EOR #%01111111 and adding one, then flip bit 7 to move the origin from the
   * centre to the edge. Both branches end at the same EOR #%10000000, which is what makes it
   * one expression rather than two.
   */
  const std::uint8_t x1 = _work.x1;
  std::uint8_t x = x1;
  if ((x1 & 0x80u) != 0u)
  {
    x = static_cast<std::uint8_t>((x1 ^ 0x7Fu) + 1u);
  }
  x ^= 0x80u;

  // 6502: AND #%01111111 / CMP #72 / BCS PX4 -- a point more than 72 rows from the centre is off
  // the top or bottom of the space view, and the routine simply returns.
  if ((_work.y1 & 0x7Fu) >= 72u)
  {
    return;
  }

  /*
   * The y half is where this routine earns its comment, because the carry threads through it.
   *
   * The comparison above did not branch, so it left carry CLEAR, and there is no CLC before the
   * ADC that negates a downward offset -- unlike the x half above, which does have one. So the
   * negation is (y1 EOR 127) + 1 + 0, and the carry it produces is what the SBC below then
   * borrows against.
   *
   * The visible consequence: for y1 = 128 the negation wraps to zero and SETS carry, so the
   * subtraction does not borrow and the point lands on row 73. For y1 = 129 it does borrow, and
   * that lands on row 73 as well. Negative zero and negative one are the same pixel. That is the
   * original's behaviour and the port keeps it; collapsing this to 72 - magnitude would be a
   * tidier routine and a different one.
   */
  bool carry = false;
  std::uint8_t magnitude = _work.y1;

  if ((_work.y1 & 0x80u) != 0u)
  {
    const AddResult negated = AddWithCarry(static_cast<std::uint8_t>(_work.y1 ^ 0x7Fu), 1u, carry);
    magnitude = negated.value;
    carry = negated.carry;
  }

  // 6502: STA T / LDA #73 / SBC T -- 73 rather than 72, because the borrow is usually taken.
  const std::uint8_t y = static_cast<std::uint8_t>(73u - magnitude - (carry ? 0u : 1u));

  PlotPixel(_canvas, _work, x, y);
}

void PlotDash(Canvas& _canvas, DrawWorkspace& _work) noexcept
{
  const std::uint16_t cell = static_cast<std::uint16_t>(Canvas::RowOffset(_work.y1) + (_work.x1 & 0xF8u));
  const std::uint8_t subRow = static_cast<std::uint8_t>(_work.y1 & 0x07u);
  const std::uint8_t index = static_cast<std::uint8_t>(_work.x1 & 0x07u);

  // 6502: LDA CTWOS2,X / AND COL -- the aligned mask, narrowed to the colour being drawn.
  _canvas.ExclusiveOr(static_cast<std::uint16_t>(cell + subRow),
                      static_cast<std::uint8_t>(MULTICOLOUR_MASK_TABLE[index] & _work.col));

  /*
   * 6502: LDA CTWOS2+2,X / BPL CP1.
   *
   * The dash's second pixel is two entries along, and the routine works out whether that pixel
   * has crossed into the next character cell by looking at the MASK rather than at x: only the
   * leftmost pixel of a byte has bit 7 set, so a negative mask means the pixel wrapped. That is
   * why CTWOS2 needs its two extra entries -- they are the wrapped cases, not padding.
   */
  const std::uint8_t second = MULTICOLOUR_MASK_TABLE[index + 2];
  const std::uint16_t secondCell = ((second & 0x80u) != 0u) ? static_cast<std::uint16_t>(cell + 8u) : cell;

  _canvas.ExclusiveOr(static_cast<std::uint16_t>(secondCell + subRow), static_cast<std::uint8_t>(second & _work.col));
}

void PlotBlock(Canvas& _canvas, DrawWorkspace& _work) noexcept
{
  // 6502: CPIX4 -- one dash, then DEC Y1 and fall through into CPIX2 for the row above. Y1 is
  // left decremented, and callers see that.
  PlotDash(_canvas, _work);
  _work.y1 = static_cast<std::uint8_t>(_work.y1 - 1u);
  PlotDash(_canvas, _work);
}

void DrawHorizontalLine(Canvas& _canvas, DrawWorkspace& _work) noexcept
{
  // 6502: CPX X2 / BEQ HL6 -- a line of no length is not drawn at all.
  if (_work.x1 == _work.x2)
  {
    return;
  }

  if (_work.x1 > _work.x2)
  {
    const std::uint8_t swap = _work.x1;
    _work.x1 = _work.x2;
    _work.x2 = swap;
  }

  // 6502: DEC X2 -- the right end is exclusive, and X2 is left decremented for the caller.
  _work.x2 = static_cast<std::uint8_t>(_work.x2 - 1u);

  const std::uint16_t row = static_cast<std::uint16_t>(Canvas::RowOffset(_work.y1) + (_work.y1 & 0x07u));
  std::uint16_t offset = static_cast<std::uint16_t>(row + (_work.x1 & 0xF8u));

  _work.t2 = static_cast<std::uint8_t>(_work.x1 & 0xF8u);
  const std::uint8_t span = static_cast<std::uint8_t>((_work.x2 & 0xF8u) - _work.t2);

  if (span == 0)
  {
    /*
     * 6502: HL2 -- both ends are in the same byte, so the two end masks are ANDed rather than
     * written one after the other. Writing them separately would EOR the overlap twice and
     * erase it, which is the kind of thing erase-by-EOR punishes.
     */
    const std::uint8_t mask =
      static_cast<std::uint8_t>(LINE_RIGHT_MASK_TABLE[_work.x1 & 0x07u] & LINE_LEFT_MASK_TABLE[_work.x2 & 0x07u]);
    _canvas.ExclusiveOr(offset, mask);
    return;
  }

  _work.r2 = static_cast<std::uint8_t>(span >> 3);

  // 6502: TWFR -- the first byte is filled from x rightwards to the end of the byte.
  _canvas.ExclusiveOr(offset, LINE_RIGHT_MASK_TABLE[_work.x1 & 0x07u]);
  offset = static_cast<std::uint16_t>(offset + 8u);

  // 6502: HLL1 -- every whole byte between the ends, which in multicolour terms is four pixels
  // of colour %11 at a time.
  for (std::uint8_t remaining = static_cast<std::uint8_t>(_work.r2 - 1u); remaining != 0; --remaining)
  {
    _canvas.ExclusiveOr(offset, 0xFFu);
    offset = static_cast<std::uint16_t>(offset + 8u);
  }

  // 6502: HL3 / TWFL -- the last byte is filled leftwards from the start of the byte to x.
  _canvas.ExclusiveOr(offset, LINE_LEFT_MASK_TABLE[_work.x2 & 0x07u]);
}

} // namespace Elite
