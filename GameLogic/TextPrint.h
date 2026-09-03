#pragma once

#include "Canvas.h"
#include "Tokens.h"

#include <cstdint>

namespace Elite
{

/*
 * The text cursor and the character printer (slice 1d-b).
 *
 * 6502: XC, YC, QQ17, COL2, K3 -- the zero-page bytes CHPR reads and leaves behind. XC and YC
 * are character cells, not pixels, and the printer advances them itself, so a caller that prints
 * two characters in a row does not touch them.
 */
struct TextState
{
  std::uint8_t column = 0; ///< 6502: XC
  std::uint8_t row = 0;    ///< 6502: YC

  /// 6502: QQ17 -- the capitalisation state the token printer owns. CHPR only reads it, and only
  /// to notice the value 255, which means "print nothing at all".
  std::uint8_t caseFlags = 0;

  /// 6502: COL2 -- the cell colour written alongside every glyph. MAG2 (0x40) is purple on
  /// black, which is what the text view uses.
  std::uint8_t cellColour = 0;
};

/*
 * The two things CHPR does that this slice cannot finish.
 *
 * Character 7 rings the bell, which is a sound event and belongs to phase 5; and a character
 * printed below the last row clears the screen and starts again, which needs TT66 and lands in
 * 1d-c. Both are seams rather than stubs -- the printer around them is complete -- and the tests
 * count how often they are reached rather than passing over them quietly.
 */
class TextEffects
{
public:
  virtual ~TextEffects() = default;

  /// 6502: R5 -- JSR BEEP.
  virtual void Beep() = 0;

  /// 6502: clss -- JSR TT66simp, then print the character again on the fresh screen.
  virtual void ClearScreen() = 0;
};

/*
 * 6502: BPRNT -- print a number, right-aligned in a fixed width, with an optional decimal point.
 *
 * The number is a 32-bit value in K, and the printer works by repeated subtraction of ten to the
 * eleventh from a 40-bit accumulator that it multiplies by ten between digits. Eleven digits
 * always, of which `digits` come after the point; leading zeros print as spaces until the first
 * significant digit, and everything narrower than the width is padded on the left.
 *
 * Characters go to a TextSink rather than to the canvas, for the reason slice 1c-a's printer
 * does: the original reaches DASC, which is the whole sentence-case machinery, and the number
 * printer's own behaviour is the sequence of characters it hands over.
 */
struct NumberWorkspace
{
  std::uint8_t k[4] = { 0, 0, 0, 0 }; ///< 6502: K -- the value, most significant byte first
  std::uint8_t u = 0;                 ///< 6502: U -- how many digits fall after the decimal point
};

/// 6502: BPRNT. `_withPoint` is the carry the entry points set, and it decides whether a decimal
/// point is printed at all -- pr6 clears it, pr5 leaves it as the caller had it.
void PrintNumber(TextSink& _sink, NumberWorkspace& _work, bool _withPoint) noexcept;

/// 6502: TT11 -- the same, for a sixteen-bit value, which is how nearly every caller reaches it.
void PrintValue(TextSink& _sink, std::uint16_t _value, std::uint8_t _digits, bool _withPoint) noexcept;

/// 6502: pr2 -- three digits, no decimal point, for a byte.
void PrintByteValue(TextSink& _sink, std::uint8_t _value, bool _withPoint) noexcept;

/*
 * 6502: TT66simp -- clear the text area and put the cursor back at (1, 1).
 *
 * It walks ylookup in steps of eight and zeroes 256 bytes from each character row's start, which
 * is exactly the 32 cells the space view and the text screens occupy -- the four-cell margins
 * either side are left alone. Rows 1 to 23 only; row 0 and the dashboard are not its business.
 */
void ClearTextArea(Canvas& _canvas, TextState& _state) noexcept;

/*
 * 6502: TT26 / CHPR -- print one character at the cursor and advance it.
 *
 * Two entry points in the original share this body, and the control codes below 32 are handled
 * here rather than by the caller, so this is where a newline actually moves the cursor.
 */
class TextPrinter
{
public:
  TextPrinter(Canvas& _canvas, TextState& _state, TextEffects* _effects = nullptr) noexcept
    : m_canvas(_canvas)
    , m_state(_state)
    , m_effects(_effects)
  {
  }

  /// 6502: CHPR. Returns the character, as the routine does in A.
  std::uint8_t Print(std::uint8_t _character) noexcept;

private:
  /// 6502: RR1 onwards -- the printable path, which is the glyph and its cell colour.
  void PrintGlyph(std::uint8_t _character) noexcept;

  Canvas& m_canvas;
  TextState& m_state;
  TextEffects* m_effects = nullptr;
};

} // namespace Elite
