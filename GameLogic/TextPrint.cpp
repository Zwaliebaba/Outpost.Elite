#include "pch.h"

#include "TextPrint.h"

#include "EliteTypes.h"
#include "LookupTables.h"

namespace Elite
{

namespace
{
/// 6502: the 128 that TTX66 and CLYNS both store into QQ17 -- bit 7, "sentence case".
constexpr std::uint8_t SENTENCE_CASE = 0x80;

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

/*
 * 6502: BPRNT and the TT11 / pr2 / pr5 / pr6 entry points.
 *
 * The accumulator is five bytes -- S ahead of K's four -- because multiplying by ten needs the
 * headroom. The original spells that multiply as three shifts and an add: shift once and keep a
 * copy, shift twice more, add the copy back, which is x * 8 + x * 2.
 *
 * Digits come out by repeated subtraction of ten to the eleventh, counting how many times it
 * fits before multiplying by ten and going round again. Eleven digits every time, with leading
 * zeros suppressed into spaces until the first significant one -- the T byte is what remembers
 * that, and the reason it is cleared rather than tested is that everything after the first digit
 * prints even when it is a zero.
 */
void PrintNumber(TextSink& _sink, NumberWorkspace& _work, bool _withPoint) noexcept
{
  /*
   * 6502: LDX #11 / STX T / PHP / BCC TT30.
   *
   * BCC skips the two decrements when the carry is CLEAR, so they happen for a number that IS
   * getting a decimal point -- the point occupies one of the eleven positions, so both the
   * leading-zero counter and the width lose one to pay for it. Reading the branch the other way
   * round shifts the padding by two characters and puts the point where a digit belongs.
   */
  std::uint8_t t = 11;
  if (_withPoint)
  {
    --t;
    --_work.u;
  }

  // 6502: TT30. XX17 counts the digits down; U becomes the position the point falls at.
  std::uint8_t xx17 = 11;
  _work.u = static_cast<std::uint8_t>(11u - _work.u);
  ++_work.u;

  std::uint8_t s = 0;
  std::uint8_t digit = 0;

  for (;;)
  {
    // 6502: TT36 / tt37 -- how many times ten to the eleventh goes into what is left.
    for (;;)
    {
      std::uint8_t remainder[4] = { 0, 0, 0, 0 };
      bool noBorrow = true;
      for (int index = 3; index >= 0; --index)
      {
        const std::uint16_t difference =
          static_cast<std::uint16_t>(_work.k[index]) - TEN_TO_THE_ELEVENTH[index] - (noBorrow ? 0u : 1u);
        remainder[index] = static_cast<std::uint8_t>(difference);
        noBorrow = difference < 0x100u;
      }
      const std::uint16_t top = static_cast<std::uint16_t>(s) - 0x17u - (noBorrow ? 0u : 1u);

      if (top >= 0x100u)
      {
        // 6502: BCC TT37 -- it did not fit, so this digit is done.
        break;
      }

      for (int index = 0; index < 4; ++index)
      {
        _work.k[index] = remainder[index];
      }
      s = static_cast<std::uint8_t>(top);
      ++digit;
    }

    /*
     * 6502: TT37. Three ways to reach a character: a non-zero digit prints; a zero prints once
     * the first significant digit has been seen (T cleared); and a zero before that prints a
     * space, but only while U says there is still padding to spend.
     */
    bool print = true;
    std::uint8_t character = 0;

    if (digit != 0 || t == 0)
    {
      // 6502: TT32 -- a digit, and from here on zeros are digits too.
      t = 0;
      character = static_cast<std::uint8_t>(digit + 0x30u);
    }
    else
    {
      --_work.u;
      if ((_work.u & 0x80u) == 0u)
      {
        // 6502: BPL TT34 -- still inside the number's own width, so nothing is printed at all.
        print = false;
      }
      else
      {
        character = ' ';
      }
    }

    if (print)
    {
      _sink.Put(character);
    }

    // 6502: TT34 -- DEC T / BPL / INC T, which is a decrement that will not go below zero.
    if (t != 0)
    {
      --t;
    }

    --xx17;
    if ((xx17 & 0x80u) != 0u)
    {
      // 6502: rT10 -- eleven digits done.
      return;
    }

    if (xx17 == 0 && _withPoint)
    {
      // 6502: PLP / BCC -- the carry that was stashed at the top decides this, which is why it
      // was stashed rather than tested there.
      _sink.Put('.');
    }

    /*
     * 6502: TT35 -- multiply the five-byte accumulator by ten. Shift once and keep a copy, shift
     * twice more, then add the copy back: x * 8 + x * 2.
     */
    std::uint8_t copy[4] = { 0, 0, 0, 0 };
    std::uint8_t copyHigh = 0;

    const auto shiftLeft = [&]() noexcept {
      bool carry = false;
      for (int index = 3; index >= 0; --index)
      {
        const ShiftResult shifted = RotateLeftValue(_work.k[index], carry);
        _work.k[index] = shifted.value;
        carry = shifted.carry;
      }
      s = RotateLeftValue(s, carry).value;
    };

    shiftLeft();
    for (int index = 0; index < 4; ++index)
    {
      copy[index] = _work.k[index];
    }
    copyHigh = s;

    shiftLeft();
    shiftLeft();

    bool carry = false;
    for (int index = 3; index >= 0; --index)
    {
      const AddResult sum = AddWithCarry(_work.k[index], copy[index], carry);
      _work.k[index] = sum.value;
      carry = sum.carry;
    }
    s = AddWithCarry(copyHigh, s, carry).value;

    digit = 0;
  }
}

void PrintValue(TextSink& _sink, std::uint16_t _value, std::uint8_t _digits, bool _withPoint) noexcept
{
  // 6502: TT11 -- STA U / LDA #0 / STA K / STA K+1 / STY K+2 / STX K+3. Only the low two bytes
  // carry a value; the caller's sixteen bits arrive in Y and X.
  NumberWorkspace work;
  work.u = _digits;
  work.k[2] = static_cast<std::uint8_t>(_value >> 8);
  work.k[3] = static_cast<std::uint8_t>(_value);
  PrintNumber(_sink, work, _withPoint);
}

void PrintByteValue(TextSink& _sink, std::uint8_t _value, bool _withPoint) noexcept
{
  // 6502: pr2 -- LDA #3 / LDY #0, so three digits and the byte in X.
  PrintValue(_sink, _value, 3, _withPoint);
}

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

void ResetCellColours(Canvas& _canvas) noexcept
{
  /*
   * 6502: BOL3 / BOL4. SC starts at &6004 -- three cells past `celllook`'s base, which is the
   * four-cell left margin the bitmap has too -- and the outer loop runs 24 times, stepping SC on
   * by 40 rather than by 32, because a screen row is 40 cells wide and Elite uses the middle 32.
   *
   * Row 24 is not filled. TTX66K counts 24 rows from row 0, so the bottom row of the screen keeps
   * whatever it had; nothing the port prints reaches it (`ClearTextArea` stops at row 23 too).
   *
   * NOR ARE THE MARGINS, and on the hardware they do not need to be: the LOADER fills both whole
   * 1 KB blocks of screen RAM with the same &10 before the game starts, and TTX66K only refreshes
   * the 32 cells the game screen occupies. The port has no loader stage, so its margins stay at
   * zero -- which is black on black, and is invisible only because nothing draws into them.
   */
  for (int row = 0; row < 24; ++row)
  {
    const std::uint16_t base = static_cast<std::uint16_t>(Canvas::CellRowOffset(row) + 1);
    for (int cell = 0; cell < 32; ++cell)
    {
      _canvas.Write(static_cast<std::uint16_t>(base + cell), TEXT_COLOUR_WHITE);
    }
  }
}

void SetUpTextScreen(TokenPrinter& _printer, TextState& _text, ExtendedTextState& _extended) noexcept
{
  // 6502: JSR MT2 -- LDA #32 / STA DTW1 / LDA #0 / STA DTW6. Sentence case for the extended
  // printer: bit 5 is what lowers a letter, and DTW6 is the override that forces it always.
  _extended.lowerCaseBits = 32;
  _extended.alwaysLower = 0;

  // 6502: LDA #128 / STA QQ17 / STA DTW2 -- and only DTW2 keeps it. See the header: the routine's
  // last five bytes put QQ17 back to zero.
  _extended.sentenceStart = SENTENCE_CASE;

  /*
   * 6502: LDX #1 / STX XC / STX YC / DEX / STX QQ17.
   *
   * QQ17 IS ASSIGNED TWICE HERE because the port keeps one 6502 byte in two places: the token
   * printer owns it, and `TextState` carries a copy that CHPR reads for the single value 255
   * ("print nothing"). Every routine that assigns QQ17 has to assign both or they drift, and this
   * is the first caller outside the token printer that holds both. Section 6.28 of the plan
   * records why that duplication is worth removing and why doing it here would be the wrong slice.
   */
  _printer.SetCaseFlags(0);
  _text.caseFlags = 0;

  _text.column = 1;
  _text.row = 1;
}

void ClearMessageRows(Canvas& _canvas, TokenPrinter& _printer, TextState& _text,
                      ExtendedTextState& _extended, MessageState& _message) noexcept
{
  // 6502: CLYNS -- LDA #0 / STA DLY / STA de. Whatever message was up is forgotten, which is why
  // `MESS` can clear the screen and then test `DLY` and find it zero (§6.67).
  _message.delay = 0;
  _message.append = 0;

  // 6502: CLYNS2 -- LDA #255 / STA DTW2 / LDA #128 / STA QQ17 / LDA #21 / STA YC / LDA #1 / STA XC.
  _extended.sentenceStart = 0xFF;
  _printer.SetCaseFlags(SENTENCE_CASE);
  _text.caseFlags = SENTENCE_CASE;
  _text.row = MESSAGE_ROW;
  _text.column = 1;

  /*
   * 6502: CLYLOOP2 / CLYLOOP. Three passes, each 256 bytes, starting at SCBASE + &1A60 and
   * stepping &140 -- and the inner loop is the same store-then-count-down that ClearTextArea
   * uses, so offset 0 is written first and then 255 down to 1.
   */
  std::uint16_t base = Canvas::RowOffset(MESSAGE_ROW * 8);
  for (int pass = 0; pass < 3; ++pass)
  {
    _canvas.Write(base, 0);
    for (std::uint16_t offset = 255; offset >= 1; --offset)
    {
      _canvas.Write(static_cast<std::uint16_t>(base + offset), 0);
    }
    base = static_cast<std::uint16_t>(base + Canvas::ROW_BYTES);
  }
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

void MoveCursorDown(TextState& _text) noexcept
{
  ++_text.row;
}

void PrintTitleLine(TokenPrinter& _printer, TextState& _text, std::uint8_t _token) noexcept
{
  _printer.Print(_token);
  MoveDownAndNewline(_printer, _text);
}

void MoveDownAndNewline(TokenPrinter& _printer, TextState& _text) noexcept
{
  MoveCursorDown(_text);
  SetSentenceCaseAndNewline(_printer);
}

void PrintThenIndent(TokenPrinter& _printer, TextState& _text, std::uint8_t _token) noexcept
{
  PrintThenNewline(_printer, _token);
  _text.column = 6;
}

} // namespace Elite
