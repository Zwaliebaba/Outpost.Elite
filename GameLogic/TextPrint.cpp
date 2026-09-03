#include "pch.h"

#include "TextPrint.h"

#include "LookupTables.h"

namespace Elite
{

namespace
{
/// 6502: the font pointer CHPR builds -- 0x0A00 + (character << 3), assembled from the two high
/// bits of the character and a shifted low byte. For a printable character that lands on
/// FONT + (character - 32) * 8, which is where FONT_DATA starts.
constexpr std::uint16_t FONT_BASE = 0x0B00;

/// 6502: LDX #10 / ASL A / ASL A / BCC / LDX #12 / ASL A / BCC / INX.
[[nodiscard]] std::uint16_t GlyphPointer(std::uint8_t _character) noexcept
{
  std::uint8_t high = 0x0A;
  if ((_character & 0x40u) != 0u)
  {
    high = 0x0C;
  }
  if ((_character & 0x20u) != 0u)
  {
    ++high;
  }
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8) | ((_character << 3) & 0xFFu));
}
} // namespace

void ClearTextArea(Canvas& _canvas, TextState& _state) noexcept
{
  /*
   * 6502: T6SL1 / T6SL2. The outer loop indexes ylookup by a screen row that is a multiple of
   * eight, so it visits character rows 1 to 23; the inner one starts at Y = 0 and counts DOWN to
   * 1, which stores at offset 0 first and then 255 down to 1 -- 256 bytes, the 32 cells this
   * screen actually uses. The margins survive, which is why a cleared screen still has its
   * border.
   */
  for (std::uint16_t row = 8; row < 0xC0u; row = static_cast<std::uint16_t>(row + 8u))
  {
    const std::uint16_t base = Canvas::RowOffset(static_cast<std::uint8_t>(row));
    _canvas.Write(base, 0);
    for (std::uint16_t offset = 255; offset >= 1; --offset)
    {
      _canvas.Write(static_cast<std::uint16_t>(base + offset), 0);
    }
  }

  // 6502: INY / STY XC / STY YC -- Y reached zero on the way out, so this is (1, 1).
  _state.column = 1;
  _state.row = 1;
}

std::uint8_t TextPrinter::Print(std::uint8_t _character) noexcept
{
  // 6502: LDY QQ17 / CPY #255 / BEQ RR4S -- 255 suppresses output entirely, and the token
  // printer sets it that way while it is measuring rather than printing.
  if (m_state.caseFlags == 0xFFu)
  {
    return _character;
  }

  if (_character == 7)
  {
    // 6502: R5 -- the bell, which is a sound event and so belongs to phase 5.
    if (m_effects != nullptr)
    {
      m_effects->Beep();
    }
    return _character;
  }

  if (_character >= 32)
  {
    PrintGlyph(_character);
    return _character;
  }

  /*
   * 6502: RRX2 / RRX1 -- the control codes, and the order matters more than it looks.
   *
   * 10 skips the column reset, so it moves down without returning to the left; 13 resets the
   * column and does NOT move down; everything else does both. Three different behaviours out of
   * two branches, which is why the original reads as a fall-through chain and this does too.
   */
  if (_character != 10)
  {
    m_state.column = 1;
  }

  if (_character == 13)
  {
    return _character;
  }

  ++m_state.row;
  return _character;
}

void TextPrinter::PrintGlyph(std::uint8_t _character) noexcept
{
  // 6502: LDA XC / CMP #31 / BCS RRX2 -- past the right margin, so wrap instead of printing.
  // The character is lost rather than carried to the next line; the original does not re-enter.
  if (m_state.column >= 31)
  {
    m_state.column = 1;
    ++m_state.row;
    return;
  }

  if (m_state.row >= 24)
  {
    // 6502: JMP clss -- off the bottom, so clear the screen and print the character again.
    if (m_effects != nullptr)
    {
      m_effects->ClearScreen();
      PrintGlyph(_character);
    }
    return;
  }

  /*
   * 6502: RR3 -- the bitmap address of the cell, which is SCBASE + YC * 320 + 32.
   *
   * The original gets there by rotating a 16-bit value made of YC and a seeded 0x80 down two
   * places and then adding YC back, which is a times-320 in five instructions. The 32 is the
   * same four-cell left margin ylookup carries, so text and the space view share an origin.
   */
  std::uint16_t offset = static_cast<std::uint16_t>(m_state.row * Canvas::ROW_BYTES + Canvas::SPACE_VIEW_MARGIN
                                                    + m_state.column * 8);

  if (_character == 127)
  {
    /*
     * 6502: DEC XC / DEC SCH / LDY #248 / JSR ZESNEW -- delete. Stepping the pointer's high byte
     * down and the index up to 248 is a way of subtracting eight without touching the low byte,
     * and ZESNEW then zeroes the eight bytes it lands on. So the cell to the left is blanked
     * outright rather than being drawn over, which is the one place the text code does not EOR.
     */
    --m_state.column;
    const std::uint16_t previous = static_cast<std::uint16_t>(offset - 8u);
    for (std::uint8_t row = 0; row < 8; ++row)
    {
      m_canvas.Write(static_cast<std::uint16_t>(previous + row), 0);
    }
    return;
  }

  // 6502: RR2 -- the cursor advances before the glyph is drawn, and the colour write below
  // relies on that. celllook is three cells in, so celllook[YC] + XC now names cell 4 + XC.
  ++m_state.column;

  const std::uint16_t glyph = GlyphPointer(_character);
  for (int row = 7; row >= 0; --row)
  {
    const std::uint16_t source = static_cast<std::uint16_t>(glyph + row);
    const std::uint8_t bits = (source >= FONT_BASE && source < FONT_BASE + FONT_DATA.size())
                                ? FONT_DATA[source - FONT_BASE]
                                : std::uint8_t{ 0 };

    m_canvas.ExclusiveOr(static_cast<std::uint16_t>(offset + row), bits);
  }

  // 6502: LDY YC / celllook / LDY XC / LDA COL2 / STA (SC),Y -- the cell's colour, written after
  // the cursor moved, which is what makes the three-cell offset in celllook come out right.
  m_canvas.Write(static_cast<std::uint16_t>(Canvas::CellRowOffset(m_state.row) + m_state.column), m_state.cellColour);
}

} // namespace Elite
