#pragma once

#include "Canvas.h"

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
