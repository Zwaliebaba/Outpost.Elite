#pragma once

#include <cstdint>

namespace Elite
{

/*
 * Where printed characters go.
 *
 * The game prints to the screen. The port keeps that behind a sink so that token expansion can
 * be built and verified before the canvas exists, and so the oracle can be compared against a
 * plain list of characters rather than against pixels (ADR-003).
 */
class TextSink
{
public:
  virtual ~TextSink() = default;
  virtual void Put(std::uint8_t _character) = 0;
};

/*
 * The six tokens that print a value rather than text: cash, fuel, the current system's name and
 * its neighbours' details. Each reads commander or system state, which phase 2 owns, so a
 * printer built without a provider skips them.
 *
 * This is a seam rather than a stub: the character-level machinery below is complete and
 * verified, and phase 2 supplies the missing half without touching it.
 */
class ValueTokens
{
public:
  virtual ~ValueTokens() = default;
  virtual void Print(std::uint8_t _token, TextSink& _sink) = 0;
};

/*
 * 6502: TT27 and the routines it falls through into.
 *
 * Elite's text is a small language rather than a set of strings. A byte is either a character,
 * a pair of letters, a whole phrase to be expanded (which may itself contain more of the same),
 * or an instruction about how to capitalise what follows. That last part is why this is a class
 * and not a function: the capitalisation flags persist between calls, and callers rely on it.
 */
class TokenPrinter
{
public:
  explicit TokenPrinter(TextSink& _sink, ValueTokens* _values = nullptr) noexcept
    : m_sink(_sink)
    , m_values(_values)
  {
  }

  /// 6502: TT27 -- print one token, expanding whatever it turns out to mean.
  void Print(std::uint8_t _token) noexcept;

  /*
   * The value-token seam, settable after construction.
   *
   * It has to be, because the thing that answers those tokens needs a TokenPrinter of its own --
   * control code 5 prints "FUEL" and "LIGHT YEARS" as tokens, and control code 0 prints " CR" --
   * so the two refer to each other and one of them has to be built first.
   */
  void SetValueTokens(ValueTokens* _values) noexcept { m_values = _values; }

  /// 6502: QQ17 -- the capitalisation state. Bit 7 asks for sentence case, bit 6 records that
  /// the first letter has been seen, and 255 suppresses output entirely.
  [[nodiscard]] std::uint8_t CaseFlags() const noexcept { return m_caseFlags; }
  void SetCaseFlags(std::uint8_t _flags) noexcept { m_caseFlags = _flags; }

private:
  /// 6502: the TT41/TT42/TT45/TT46/TT74 chain -- one character, under the case flags.
  void PrintCharacter(std::uint8_t _character) noexcept;

  /// 6502: TT43 -- a letter pair, or a recursive token when the value is high enough.
  void PrintLetterPair(std::uint8_t _token) noexcept;

  /// 6502: ex -- walk the table to the token's text and print it, character by character.
  void PrintPhrase(std::uint8_t _token) noexcept;

  TextSink& m_sink;
  ValueTokens* m_values = nullptr;
  std::uint8_t m_caseFlags = 0;
};

/*
 * The one- and two-instruction wrappers around TT27.
 *
 * Every one of these is a JSR or a JMP to the token printer with a constant in the accumulator,
 * and the game leans on them everywhere. The ledger files them under `TextPrint.cpp`; they are
 * here instead, because TextPrint is the layer BELOW this one -- CHPR does not know what a token
 * is -- and putting them there would have the lower layer calling the higher one.
 *
 * The three that also move the cursor live in TextPrint.h, which can see both a TokenPrinter and
 * a TextState.
 */

/// 6502: TT162 -- LDA #' ' / JMP TT27. Through the TOKEN printer, so the space is subject to the
/// case flags like any other character.
void PrintSpace(TokenPrinter& _printer) noexcept;

/// 6502: TT67 -- LDA #12 / JMP TT27. Character twelve is the newline the screen understands.
void PrintNewline(TokenPrinter& _printer) noexcept;

/*
 * 6502: TT69, which FALLS INTO TT67 -- sentence case, and then a newline.
 *
 * The fall-through is not visible in the source and is exact in the assembled build: TT69 is
 * `LDA #%10000000 / STA QQ17`, four bytes, and TT67 begins at TT69 + 4. So there is no RTS, and
 * every `JSR TT69` in the game prints a newline as well as setting the flag. The sell screen
 * calls it once per line and relies on it.
 */
void SetSentenceCaseAndNewline(TokenPrinter& _printer) noexcept;

/// 6502: spc -- JSR TT27 / JMP TT162. A token then a space.
void PrintThenSpace(TokenPrinter& _printer, std::uint8_t _token) noexcept;

/// 6502: prq -- JSR TT27 / LDA #'?' / JMP TT27. A token then a question mark, which is how every
/// "QUANTITY?" and "CASH?" on the trading screens is built.
void PrintThenQuestion(TokenPrinter& _printer, std::uint8_t _token) noexcept;

/// 6502: TT68, which falls into TT73 -- a token then a colon.
void PrintThenColon(TokenPrinter& _printer, std::uint8_t _token) noexcept;

/// 6502: plf -- JSR TT27 / JMP TT67. A token then a newline.
void PrintThenNewline(TokenPrinter& _printer, std::uint8_t _token) noexcept;

} // namespace Elite
