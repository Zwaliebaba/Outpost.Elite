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

bool PlotRelativePixel(Canvas& _canvas, DrawWorkspace& _work) noexcept
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
    // 6502: CMP #Y / BCS PX4 -- the branch was taken, so the carry it left is SET, and `PX4` is
    // a bare `RTS`.
    return true;
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

  /*
   * 6502: `PIXEL`'s exit carry, and it is exactly `ZZ >= 80`.
   *
   * Three of the four ways out leave it set -- `CMP #144 / BCS PX3`, `CMP #80 / BCS PX13`, and
   * `PIXEL2`'s own off-screen `BCS PX4` -- and the fourth, the near case that plots twice, comes
   * through `CMP #80` without branching and so leaves it clear. Nothing between there and the
   * `RTS` touches it.
   */
  return _work.zz >= 80u;
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

/*
 * 6502: LOIN / LL30 -- the line.
 *
 * The densest routine in phase 1, and almost all of that density is unrolling. The shipped code
 * has thirty-two copies of two loops: eight starting bit positions, each reached through a
 * self-modifying JMP whose operand is patched from LIJT1..LIJT8, times up/down, times
 * left/right. The port has the two loops, with the starting bit as a variable. Nothing about the
 * result changes; the original was buying speed a 1 MHz machine needed and we do not.
 *
 * Two things are NOT unrolling and are ported as they are:
 *
 * The line is plotted one BIT at a time, not one multicolour pixel at a time -- TWOS is the
 * eight single-bit masks. So a line alternates between the %01 and %10 colour of every cell it
 * crosses, which is what gives Elite's wireframe its two-tone look, and is another thing a
 * colour-per-pixel canvas could not have reproduced.
 *
 * And the carry threads through the SCREEN POINTER. Stepping to the next character cell is
 * ADC #8 on the pointer's low byte, and the carry that leaves is still there when the next
 * iteration adds the slope to the accumulator -- so on the iterations where a cell boundary
 * happens to carry, the line advances by one extra step. That is why this keeps SC as two bytes
 * with explicit carries rather than as a flat offset: a flat offset loses exactly that bit.
 */

namespace
{
/// 6502: SC and SCH -- the screen pointer, kept as the two bytes the original keeps, because the
/// carry between them is load-bearing (see the note above).
struct ScreenPointer
{
  std::uint8_t low = 0;
  std::uint8_t high = 0;

  [[nodiscard]] std::uint16_t At(std::uint8_t _y) const noexcept
  {
    return static_cast<std::uint16_t>(((static_cast<std::uint16_t>(high) << 8) | low) + _y);
  }

  /// Returns the carry out, which the caller may well still be using.
  bool Add(std::uint8_t _amount, bool _carryIn) noexcept
  {
    const AddResult sum = AddWithCarry(low, _amount, _carryIn);
    low = sum.value;
    return sum.carry;
  }

  bool Subtract(std::uint8_t _amount, bool _carryIn) noexcept
  {
    const SubResult difference = SubtractWithCarry(low, _amount, _carryIn);
    low = difference.value;
    return difference.carry;
  }
};

/// 6502: the LIlog chain in LOIN, and the LIloG chain in its steep half -- 256 * A / Q through
/// the logarithm tables, saturating at 255. The same shape as LL28's body, but this copy stores
/// nothing in `widget` and takes the operands in registers, so it is written out rather than
/// shared: a helper that had to grow a flag to say whether it scribbles on zero page would be
/// worse than two readable copies.
[[nodiscard]] std::uint8_t Slope(std::uint8_t _numerator, std::uint8_t _denominator) noexcept
{
  if (_numerator == 0)
  {
    return 0;
  }

  const SubResult low = SubtractWithCarry(LOG_LOW_TABLE[_numerator], LOG_LOW_TABLE[_denominator], true);
  const bool useOddTable = (low.value & 0x80u) != 0u;

  const SubResult high = SubtractWithCarry(LOG_TABLE[_numerator], LOG_TABLE[_denominator], low.carry);
  if (high.carry)
  {
    return 255;
  }

  return useOddTable ? ANTILOG_ODD_TABLE[high.value] : ANTILOG_TABLE[high.value];
}


/*
 * 6502: STPX and everything it reaches -- the shallow case, where the line moves further across
 * than it does up or down, so it plots one pixel per column and steps rows when the accumulator
 * says to.
 */
void DrawShallowLine(Canvas& _canvas, DrawWorkspace& _work, std::uint8_t _p2, std::uint8_t _q2, std::uint8_t _s2,
                     bool _swapped) noexcept
{
  // 6502: LDX X1 / CPX X2 / BCC LI3 -- draw left to right, swapping the ends if they arrived the
  // other way round. DEC SWAP is what records that, and the record matters: a swapped line does
  // not plot its first pixel.
  if (_work.x1 >= _work.x2)
  {
    _swapped = true;
    _work.swap = static_cast<std::uint8_t>(_work.swap - 1u); // 6502: DEC SWAP
    std::uint8_t swap = _work.x1;
    _work.x1 = _work.x2;
    _work.x2 = swap;
    swap = _work.y1;
    _work.y1 = _work.y2;
    _work.y2 = swap;
  }

  // 6502: LI3 -- the slope, as a fraction of a row per column.
  _q2 = Slope(_q2, _p2);

  const bool goingUp = _work.y1 >= _work.y2;
  const std::uint16_t rowAddress = static_cast<std::uint16_t>(Canvas::RowOffset(_work.y1));

  ScreenPointer sc;
  std::uint8_t y = 0;

  /*
   * The carry the address setup leaves, which is the FIRST OPERAND of the accumulator below.
   *
   * Between the last instruction of either setup and the loop's first `ADC Q2` there is a `TYA`,
   * an `AND`, a `TAX`, a `BIT`, four table loads, an `LDX`, sometimes an `INX` and a `BEQ` -- and
   * not one of them touches the carry. So whatever the setup left is what the first step adds,
   * and on the downward path that is the carry out of `SBC #247`, which is set whenever the
   * pointer's low byte had reached 248.
   *
   * The port started this at false and was right for every line whose start did not reach that,
   * which is most of them: one pixel of one line in nine lands on a row boundary because of it
   * (§6.47). It is the third time in this routine and the seventh in the project that an
   * uncleared 6502 flag has been the defect.
   */
  bool carry = false;

  if (goingUp)
  {
    // 6502: the AC19 block. SC is the row plus the byte within it, Y the pixel row in the cell.
    const AddResult base = AddWithCarry(static_cast<std::uint8_t>(_work.x1 & 0xF8u),
                                        static_cast<std::uint8_t>(rowAddress & 0xFFu), false);
    sc.low = base.value;
    const AddResult top = AddWithCarry(static_cast<std::uint8_t>(rowAddress >> 8), 0, base.carry);
    sc.high = top.value;
    carry = top.carry;
    y = static_cast<std::uint8_t>(_work.y1 & 0x07u);
  }
  else
  {
    /*
     * 6502: DOWN. Same address by a different route: Y is biased up to 0xF8..0xFF and SC pulled
     * down by 248 to match, so that INY runs off the end of a cell into zero and the branch that
     * tests it is a BNE. The port keeps the bias rather than normalising it, because SC's low
     * byte is what the carry chain reads.
     */
    sc.high = static_cast<std::uint8_t>(rowAddress >> 8);
    const AddResult base = AddWithCarry(static_cast<std::uint8_t>(_work.x1 & 0xF8u),
                                        static_cast<std::uint8_t>(rowAddress & 0xFFu), false);
    sc.low = base.value;
    if (base.carry)
    {
      ++sc.high;
    }
    carry = sc.Subtract(0xF7u, false);
    if (!carry)
    {
      --sc.high;
    }
    y = static_cast<std::uint8_t>((_work.y1 & 0x07u) ^ 0xF8u);
  }

  std::uint8_t bit = static_cast<std::uint8_t>(_work.x1 & 0x07u);
  std::uint8_t count = _p2;
  bool skipFirst = false;

  if (_swapped)
  {
    // 6502: LDX P2 / INX / BEQ -- the swapped entry counts one more and enters past the plot.
    count = static_cast<std::uint8_t>(_p2 + 1u);
    if (count == 0)
    {
      return;
    }
    skipFirst = true;
  }
  else if (!goingUp && _p2 == 0)
  {
    // 6502: LDX P2 / BEQ LIE0 -- the downward entry checks for an empty line and the upward one
    // does not. Not a symmetry the port may impose: upward with P2 = 0 really does plot 256
    // pixels, because DEX wraps.
    return;
  }

  for (;;)
  {
    if (!skipFirst)
    {
      _canvas.ExclusiveOr(sc.At(y), PIXEL_MASK_TABLE[bit]);
    }
    skipFirst = false;

    // 6502: DEX / BEQ -- the pixel counter, tested before the step rather than after the plot.
    --count;
    if (count == 0)
    {
      return;
    }

    const AddResult accumulated = AddWithCarry(_s2, _q2, carry);
    _s2 = accumulated.value;
    carry = accumulated.carry;

    if (carry)
    {
      if (goingUp)
      {
        // 6502: DEY / BPL -- up a pixel row, and up a character row when that runs out.
        --y;
        if ((y & 0x80u) != 0u)
        {
          const bool noBorrow = sc.Subtract(0x40u, carry);
          sc.high = SubtractWithCarry(sc.high, 1u, noBorrow).value;
          y = 7;
        }
      }
      else
      {
        // 6502: INY / BNE -- the biased Y runs up to zero rather than down past it.
        ++y;
        if (y == 0)
        {
          const bool over = sc.Add(0x3Fu, carry);
          sc.high = AddWithCarry(sc.high, 1u, over).value;
          y = 0xF8u;
        }
      }
      carry = false; // 6502: the CLC that every step path ends with
    }

    ++bit;
    if (bit == 8)
    {
      /*
       * 6502: LI89 / LI29 -- one character cell to the right, and here is the carry that a flat
       * offset would lose. ADC #8 on the pointer's low byte can carry, and nothing clears it
       * before the next iteration adds the slope, so that iteration advances one step further
       * than the slope alone would take it.
       */
      bit = 0;
      carry = sc.Add(8u, false);
      if (carry)
      {
        ++sc.high;
      }
    }
  }
}

/*
 * 6502: STPY and everything it reaches -- the steep case, one pixel per ROW, stepping across
 * when the accumulator says to. The mask is carried in R2 and shifted rather than indexed, which
 * is why this half has no bit counter.
 */
void DrawSteepLine(Canvas& _canvas, DrawWorkspace& _work, std::uint8_t _p2, std::uint8_t _q2, std::uint8_t _s2,
                   bool _swapped) noexcept
{
  // 6502: CPY Y2 / BCS LI15 -- draw downwards, swapping the ends if needed.
  if (_work.y1 < _work.y2)
  {
    _swapped = true;
    _work.swap = static_cast<std::uint8_t>(_work.swap - 1u); // 6502: DEC SWAP
    std::uint8_t swap = _work.x1;
    _work.x1 = _work.x2;
    _work.x2 = swap;
    swap = _work.y1;
    _work.y1 = _work.y2;
    _work.y2 = swap;
  }

  const std::uint16_t rowAddress = static_cast<std::uint16_t>(Canvas::RowOffset(_work.y1));

  ScreenPointer sc;
  const AddResult base = AddWithCarry(static_cast<std::uint8_t>(_work.x1 & 0xF8u),
                                      static_cast<std::uint8_t>(rowAddress & 0xFFu), false);
  sc.low = base.value;
  sc.high = AddWithCarry(static_cast<std::uint8_t>(rowAddress >> 8), 0, base.carry).value;

  std::uint8_t y = static_cast<std::uint8_t>(_work.y1 & 0x07u);
  std::uint8_t mask = PIXEL_MASK_TABLE[_work.x1 & 0x07u];

  // 6502: LDX P2 / BEQ LIfudge -- a vertical line keeps a slope of zero rather than dividing.
  if (_p2 != 0)
  {
    _p2 = Slope(_p2, _q2);
  }

  // 6502: LIfudge -- SEC / LDX Q2 / INX, then the direction test.
  std::uint8_t count = static_cast<std::uint8_t>(_q2 + 1u);
  const bool goingRight = SubtractWithCarry(_work.x2, _work.x1, true).carry;

  // 6502: LDA SWAP / BEQ LI17 -- unswapped enters past the plot, swapped plots and counts one
  // fewer. Both halves, left and right, do this the same way.
  bool skipFirst = !_swapped;
  if (_swapped)
  {
    --count;
  }

  bool carry = false;

  for (;;)
  {
    if (!skipFirst)
    {
      _canvas.ExclusiveOr(sc.At(y), mask);
    }
    skipFirst = false;

    /*
     * 6502: LI17 / LI18 -- one pixel row up, every iteration. This is the steep case, so the row
     * always moves and it is the column that waits for the accumulator.
     *
     * And the borrow this leaves is NOT cleared. Both shallow paths end their row step with a
     * CLC and this one does not, so on any iteration that crosses a character row the SBC's
     * carry survives into the accumulator below and the line advances one step further. Drop it
     * and the port is right for 71 of a 72-pixel line, with one pixel one bit out -- which is
     * what it did until this comment existed.
     */
    --y;
    if ((y & 0x80u) != 0u)
    {
      const bool noBorrow = sc.Subtract(0x3Fu, carry);
      const SubResult high = SubtractWithCarry(sc.high, 1u, noBorrow);
      sc.high = high.value;
      carry = high.carry;
      y = 7;
    }

    const AddResult accumulated = AddWithCarry(_s2, _p2, carry);
    _s2 = accumulated.value;
    carry = accumulated.carry;

    if (carry)
    {
      if (goingRight)
      {
        // 6502: LSR R2 / BCC / ROR R2 -- the mask walks right a bit at a time, and when it falls
        // out of the byte it comes back at the top and the pointer steps a cell.
        const bool fellOut = (mask & 0x01u) != 0u;
        mask = static_cast<std::uint8_t>(mask >> 1);
        if (fellOut)
        {
          mask = 0x80u;
          carry = sc.Add(8u, false);
          if (carry)
          {
            ++sc.high;
          }
        }
      }
      else
      {
        // 6502: LFT's ASL R2 / ROL R2 -- the same, leftwards.
        const bool fellOut = (mask & 0x80u) != 0u;
        mask = static_cast<std::uint8_t>(mask << 1);
        if (fellOut)
        {
          mask = 0x01u;
          if (!sc.Subtract(0x07u, false))
          {
            --sc.high;
          }
        }
      }
      carry = false; // 6502: every one of those paths reaches LIC5 or LIC6 with carry clear
    }

    // 6502: LIC5 / LIC6 -- DEX / BNE, so the count is tested after the step, not before it.
    --count;
    if (count == 0)
    {
      return;
    }
  }
}
} // namespace

void DrawLine(Canvas& _canvas, DrawWorkspace& _work) noexcept
{
  // 6502: LDA #128 / STA S2 / ASL A / STA SWAP. The shift does three jobs at once: it leaves the
  // accumulator seeded at half, it zeroes the swap flag, and it SETS carry, which is why the
  // subtraction below has no SEC in front of it.
  std::uint8_t s2 = 0x80;
  bool swapped = false;
  _work.swap = 0;

  // 6502: LI1, LI2 -- the two spans, as magnitudes. Negating with EOR #255 / ADC #1 works
  // because the branch that reaches it left carry clear.
  SubResult span = SubtractWithCarry(_work.x2, _work.x1, true);
  std::uint8_t p2 = span.carry ? span.value : AddWithCarry(static_cast<std::uint8_t>(span.value ^ 0xFFu), 1u, false).value;

  span = SubtractWithCarry(_work.y2, _work.y1, true);
  std::uint8_t q2 = span.carry ? span.value : AddWithCarry(static_cast<std::uint8_t>(span.value ^ 0xFFu), 1u, false).value;

  if (q2 < p2)
  {
    DrawShallowLine(_canvas, _work, p2, q2, s2, swapped);
    return;
  }

  DrawSteepLine(_canvas, _work, p2, q2, s2, swapped);
}

} // namespace Elite
