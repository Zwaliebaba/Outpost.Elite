#include "pch.h"

#include "TextPrint.h"

#include "EliteTypes.h"
#include "Lines2x.h"
#include "LookupTables.h"
#include "TextPrint2x.h"

namespace Elite
{

  namespace
  {
    /// The 128 that TTX66 and CLYNS both store into QQ17 -- bit 7, "sentence case".
    constexpr std::uint8_t SENTENCE_CASE = 0x80;

    /// The font pointer CHPR builds -- 0x0A00 + (character << 3), assembled from the two high
    /// bits of the character and a shifted low byte. For a printable character that lands on
    /// FONT + (character - 32) * 8, which is where FONT_DATA starts.
    constexpr std::uint16_t FONT_BASE = 0x0B00;

    /// The high byte starts at 10 and steps to 12 or 13 as the character's top bits shift
    /// out.
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
   * BPRNT and the TT11 / pr2 / pr5 / pr6 entry points.
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
  std::uint8_t PrintNumber(TextSink& _sink, NumberBytes _value, std::uint8_t _digits, bool _withPoint) noexcept
  {
    std::uint8_t pointPosition = _digits; // U, which the routine rewrites and leaves
    /*
     * The width set to 11, the carry STASHED on the stack, and a branch past two
     * decrements.
     *
     * BCC skips the two decrements when the carry is CLEAR, so they happen for a number that IS
     * getting a decimal point -- the point occupies one of the eleven positions, so both the
     * leading-zero counter and the width lose one to pay for it. Reading the branch the other way
     * round shifts the padding by two characters and puts the point where a digit belongs.
     */
    std::uint8_t width = 11;
    if (_withPoint)
    {
      --width;
      --pointPosition;
    }

    // TT30. XX17 counts the digits down; U becomes the position the point falls at.
    std::uint8_t digitsLeft = 11;
    pointPosition = static_cast<std::uint8_t>(11u - pointPosition);
    ++pointPosition;

    std::uint8_t printedYet = 0;
    std::uint8_t digit = 0;

    for (;;)
    {
      // TT36 / tt37 -- how many times ten to the eleventh goes into what is left.
      for (;;)
      {
        std::array<std::uint8_t, 4> remainder = {0, 0, 0, 0};
        bool noBorrow = true;
        for (int index = 3; index >= 0; --index)
        {
          const std::uint16_t difference =
            static_cast<std::uint16_t>(_value[static_cast<std::size_t>(index)]) - TEN_TO_THE_ELEVENTH[index] - (noBorrow ? 0u : 1u);
          remainder[index] = static_cast<std::uint8_t>(difference);
          noBorrow = difference < 0x100u;
        }
        const std::uint16_t top = static_cast<std::uint16_t>(printedYet) - 0x17u - (noBorrow ? 0u : 1u);

        if (top >= 0x100u)
        {
          // It did not fit, so this digit is done.
          break;
        }

        for (int index = 0; index < 4; ++index)
        {
          _value[static_cast<std::size_t>(index)] = remainder[index];
        }
        printedYet = static_cast<std::uint8_t>(top);
        ++digit;
      }

      /*
       * TT37. Three ways to reach a character: a non-zero digit prints; a zero prints once
       * the first significant digit has been seen (T cleared); and a zero before that prints a
       * space, but only while U says there is still padding to spend.
       */
      bool print = true;
      std::uint8_t character = 0;

      if (digit != 0 || width == 0)
      {
        // A digit, and from here on zeros are digits too.
        width = 0;
        character = static_cast<std::uint8_t>(digit + 0x30u);
      }
      else
      {
        --pointPosition;
        if ((pointPosition & 0x80u) == 0u)
        {
          // Still inside the number's own width, so nothing is printed at all.
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

      // A decrement guarded by a branch, so the width will not go below zero.
      if (width != 0)
      {
        --width;
      }

      --digitsLeft;
      if ((digitsLeft & 0x80u) != 0u)
      {
        // Eleven digits done, and `U` goes back as the routine leaves it.
        return pointPosition;
      }

      if (digitsLeft == 0 && _withPoint)
      {
        // The carry PULLED BACK off the stack decides this, which is why it was stashed at
        // the top rather than tested there.
        // was stashed rather than tested there.
        _sink.Put('.');
      }

      /*
       * Multiply the five-byte accumulator by ten. Shift once and keep a copy, shift
       * twice more, then add the copy back: x * 8 + x * 2.
       */
      std::array<std::uint8_t, 4> copy = {0, 0, 0, 0};
      std::uint8_t copyHigh = 0;

      const auto shiftLeft = [&]() noexcept
      {
        bool carry = false;
        for (int index = 3; index >= 0; --index)
        {
          const ShiftResult shifted = RotateLeftValue(_value[static_cast<std::size_t>(index)], carry);
          _value[static_cast<std::size_t>(index)] = shifted.value;
          carry = shifted.carry;
        }
        printedYet = RotateLeftValue(printedYet, carry).value;
      };

      shiftLeft();
      for (int index = 0; index < 4; ++index)
      {
        copy[index] = _value[static_cast<std::size_t>(index)];
      }
      copyHigh = printedYet;

      shiftLeft();
      shiftLeft();

      bool carry = false;
      for (int index = 3; index >= 0; --index)
      {
        const AddResult sum = AddWithCarry(_value[static_cast<std::size_t>(index)], copy[index], carry);
        _value[static_cast<std::size_t>(index)] = sum.value;
        carry = sum.carry;
      }
      printedYet = AddWithCarry(copyHigh, printedYet, carry).value;

      digit = 0;
    }
  }

  void PrintValue(TextSink& _sink, std::uint16_t _value, std::uint8_t _digits, bool _withPoint) noexcept
  {
    // The digit count kept and the block zeroed. Only the low two bytes carry a
    // value; the caller's sixteen bits arrive in Y and X.
    const NumberBytes value = {0u, 0u, static_cast<std::uint8_t>(_value >> 8), static_cast<std::uint8_t>(_value)};
    (void)PrintNumber(_sink, value, _digits, _withPoint);
  }

  void PrintByteValue(TextSink& _sink, std::uint8_t _value, bool _withPoint) noexcept
  {
    // Three digits, and the byte arrives in X.
    PrintValue(_sink, _value, 3, _withPoint);
  }

  void ClearTextArea(Canvas& _canvas, TextState& _state) noexcept
  {
    /*
     * T6SL1 / T6SL2. The outer loop indexes ylookup by a screen row that is a multiple of
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

    // Y reached zero on the way out and is stepped once, so this is (1, 1).
    _state.column = 1;
    _state.row = 1;
  }

  void ResetCellColours(Canvas& _canvas, Picture* _picture) noexcept
  {
    /*
     * BOL3 / BOL4. SC starts at &6004 -- three cells past `celllook`'s base, which is the
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
      if (DrawingTwins(_picture))
      {
        // The same thirty-two cells, as the four wide ones each becomes. `base` is one past the
        // block, which is `celllook`'s three-cell offset plus the cursor's own one (ADR-002 §7).
        SetCellRun2x(*_picture, row * Canvas::CELL_COLUMNS + 1, 32, TEXT_COLOUR_WHITE);
      }
    }
  }

  void SetUpTextScreen(TokenPrinter& _printer, TextState& _text, ExtendedTextState& _extended) noexcept
  {
    // Sentence case for the extended printer: bit 5 is what lowers a letter, and
    // `DTW6` is the override that forces it always.
    _extended.lowerCaseBits = 32;
    _extended.alwaysLower = 0;

    // 128 into both `QQ17` and `DTW2` -- and only `DTW2` keeps it. See the header: the
    // routine's last five bytes put `QQ17` back to zero.
    _extended.sentenceStart = SENTENCE_CASE;

    /*
     * The cursor to (1, 1), then X stepped down to zero and stored into `QQ17`.
     *
     * QQ17 WAS ASSIGNED TWICE HERE until M5-e-2c, because the port kept one 6502 byte in two
     * places -- the token printer's copy and the `TextState` byte CHPR reads for 255 -- and every
     * routine that stored it had to store both or they drifted (the conversion plan's §6.28). The
     * printer binds to `_text` now and there is one byte.
     */
    _text.caseFlags = 0;

    _text.column = 1;
    _text.row = 1;
  }

  void ClearMessageRows(Canvas& _canvas, TokenPrinter& _printer, TextState& _text, ExtendedTextState& _extended,
                        MessageState& _message, Picture* _picture,
                        TextLayout _layout) noexcept
  {
    // Whatever message was up is forgotten, which is why `MESS` can clear the
    // screen and then test `DLY` and find it zero (§6.67).
    _message.delay = 0;
    _message.append = 0;

    // The measuring flag to 255, sentence case on, and the cursor to row 21,
    // column 1.
    _extended.sentenceStart = 0xFF;
    _text.caseFlags = SENTENCE_CASE;
    _text.row = MESSAGE_ROW;
    _text.column = 1;

    /*
     * CLYLOOP2 / CLYLOOP. Three passes, each 256 bytes, starting at SCBASE + &1A60 and
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

    if (DrawingTwins(_picture))
    {
      ClearMessageRows2x(*_picture, _layout);
    }
  }

  std::uint8_t TextPrinter::Print(std::uint8_t _character) noexcept
  {
    // A `QQ17` of 255 suppresses output entirely, and the token printer sets it that
    // way while it is measuring rather than printing.
    if (m_state.caseFlags == 0xFFu)
    {
      return _character;
    }

    /*
     * A label run lasts one line and ends at the first control code, which for `TT23` is the
     * newline after the name (`TextPrinter::SetLabelRun`). Clearing it here rather than trusting the
     * row to change is what stops a run outliving its screen: the next screen to print on the same
     * canvas row would otherwise inherit it.
     */
    if (_character < 32u)
    {
      m_labelActive = false;
    }

    if (_character == 7)
    {
      // The beep, whose carry `dn2`, `R5` and `DK4` all drop.
      if (m_sound != nullptr)
      {
        (void)Beep(*m_sound, false);
      }
      return _character;
    }

    if (_character >= 32)
    {
      PrintGlyph(_character);
      return _character;
    }

    /*
     * RRX2 / RRX1 -- the control codes, and the order matters more than it looks.
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
    // Past the right margin, so wrap instead of printing. The character is lost
    // rather than carried to the next line; the original does not re-enter.
    if (m_state.column >= 31)
    {
      m_state.column = 1;
      ++m_state.row;
      return;
    }

    if (m_state.row >= 24)
    {
      /*
       * `TT66simp`, then the character reloaded and printed again on the fresh
       * screen through `RRafter`.
       *
       * IT IS `TT66simp` AND NOT `TT66`, and until M3-b-4a this was a seam the executable answered
       * with the whole of `TT66` -- the palette, the dashboard, the sprites, the border and `QQ11`
       * (§8). `ClearTextArea` is the routine: rows 1 to 23 of the bitmap and the cursor home to
       * (1, 1), with row 0 and the dashboard left alone.
       *
       * The recursion terminates because the clear leaves `YC` at 1.
       */
      ClearTextArea(m_canvas, m_state);
      if (DrawingTwins(m_picture))
      {
        ClearTextArea2x(*m_picture, Layout()); // Resolution.md §4, rule T3 -- every erase has a twin
      }
      PrintGlyph(_character);
      return;
    }

    /*
     * The bitmap address of the cell, which is SCBASE + YC * 320 + 32.
     *
     * The original gets there by rotating a 16-bit value made of YC and a seeded 0x80 down two
     * places and then adding YC back, which is a times-320 in five instructions. The 32 is the
     * same four-cell left margin ylookup carries, so text and the space view share an origin.
     */
    std::uint16_t offset = static_cast<std::uint16_t>(m_state.row * Canvas::ROW_BYTES + Canvas::SPACE_VIEW_MARGIN + m_state.column * 8);

    if (_character == 127)
    {
      /*
       * The delete path. Stepping the pointer's high byte down and the index up to 248 is a
       * way of subtracting eight without touching the low byte, and ZESNEW then zeroes the eight
       * bytes it lands on. So the cell to the left is blanked outright rather than being drawn
       * over, which is the one place the text code does not EOR.
       */
      --m_state.column;
      const std::uint16_t previous = static_cast<std::uint16_t>(offset - 8u);
      for (std::uint8_t row = 0; row < 8; ++row)
      {
        m_canvas.Write(static_cast<std::uint16_t>(previous + row), 0);
      }
      if (DrawingTwins(m_picture))
      {
        // The decrement above already moved the cursor, so this IS the cell just blanked. The
        // layout counts canvas cells, and `XC` is four cells in from the first of them.
        EraseCell2x(*m_picture, Layout(), static_cast<std::uint8_t>(TEXT_FIRST_COLUMN + m_state.column), m_state.row);
      }
      return;
    }

    // The cursor advances before the glyph is drawn, and the colour write below
    // relies on that. celllook is three cells in, so celllook[YC] + XC now names cell 4 + XC.
    ++m_state.column;

    const std::uint16_t glyph = GlyphPointer(_character);

    // The eight bytes are KEPT as the loop reads them, so that the twin below draws the glyph the
    // faithful printer drew rather than looking one up again. One font lookup in the tree means the
    // two surfaces can disagree about where a glyph goes and never about which glyph it is.
    std::array<std::uint8_t, 8> drawn{};

    for (int row = 7; row >= 0; --row)
    {
      const std::uint16_t source = static_cast<std::uint16_t>(glyph + row);
      const std::uint8_t bits =
        (source >= FONT_BASE && source < FONT_BASE + FONT_DATA.size()) ? FONT_DATA[source - FONT_BASE] : std::uint8_t{0};

      drawn[static_cast<std::size_t>(row)] = bits;
      m_canvas.ExclusiveOr(static_cast<std::uint16_t>(offset + row), bits);
    }

    // The cell's colour, written after the cursor moved, which is what makes the
    // three-cell offset in `celllook` come out right.
    m_canvas.Write(static_cast<std::uint16_t>(Canvas::CellRowOffset(m_state.row) + m_state.column), m_state.palette);

    if (DrawingTwins(m_picture))
    {
      /*
       * The cursor was advanced before the glyph was drawn, so the cell the glyph went on is the
       * one BEFORE it -- which is what `offset` was computed from and what `celllook`'s three-cell
       * base makes the colour write land on. `TEXT_FIRST_COLUMN` is the four cells of margin that
       * turn an `XC` into a canvas cell.
       */
      const std::uint8_t cell = static_cast<std::uint8_t>(TEXT_FIRST_COLUMN + m_state.column - 1u);
      PrintGlyphAt2x(*m_picture, WideCellFor(cell, m_state.row), drawn, m_state.palette);
    }
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
