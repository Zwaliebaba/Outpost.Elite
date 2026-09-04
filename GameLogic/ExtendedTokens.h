#pragma once

#include "Rng.h"
#include "Tokens.h"

#include <array>
#include <cstdint>

namespace Elite
{

/*
 * 6502: DTW1 to DTW8 -- the state the extended printer carries between bytes.
 *
 * These are eight separate bytes in the original rather than a packed set of flags, and they
 * are kept separate here for the same reason: each is written independently by a different
 * control code, and folding them together would invent invariants the game does not have.
 *
 * DTW7 is not a variable at all in the game. It is the operand byte of the `LDA #'A'` that
 * opens MT16, so the routine that changes it is rewriting an instruction. The port gives it a
 * name because what the trick achieves is a value.
 */
struct ExtendedTextState
{
  std::uint8_t lowerCaseBits = 0; ///< 6502: DTW1 -- bits set into a letter to lower its case
  std::uint8_t sentenceStart = 0; ///< 6502: DTW2 -- set when the last character ended a word
  std::uint8_t toLineBuffer = 0;  ///< 6502: DTW3 -- send characters through the recursive printer
  std::uint8_t justify = 0;       ///< 6502: DTW4 -- bit 7 buffers the line, bit 6 never flushes it
  std::uint8_t bufferLength = 0;  ///< 6502: DTW5 -- how much of the line buffer is in use
  std::uint8_t alwaysLower = 0;   ///< 6502: DTW6 -- lower case regardless of the sentence state
  std::uint8_t literal = 'A';     ///< 6502: DTW7 -- the character control code 16 prints
  std::uint8_t caseMask = 0xFF;   ///< 6502: DTW8 -- bits cleared from the next letter, once
};

/*
 * 6502: DASC, which the game also knows as TT26.
 *
 * Every printed character in Elite passes through here, from both text systems at once: the
 * recursive printer arrives by `JMP DASC` and the extended printer by DTS. What it decides is
 * where the character goes. Normally it goes straight to the screen. With DTW4 set it goes
 * into a ninety-byte line buffer instead, and a form feed then empties that buffer to the
 * screen thirty columns at a time -- widening the gaps between words until each line ends
 * exactly on a space. That is Elite's justified text, and it is why a system description reads
 * as a neat block rather than a ragged one.
 *
 * It is a TextSink because that is precisely what it is. The recursive printer is handed one of
 * these rather than the screen, exactly as the original hands it DASC, and that is what lets a
 * system name printed by TT27 take part in the justification of the sentence around it.
 */
class CharacterPrinter : public TextSink
{
public:
  /*
   * 6502: BUF.
   *
   * The game gives this ninety bytes before the next variable begins, and its own text is
   * longer than that: a system description runs to a hundred characters or so, and the buffer
   * holds all of it until the form feed at the end. So the original spills into the ship
   * position tables that follow, which is harmless -- justification only ever runs while docked
   * and those tables are the flight model's. Sizing this at ninety would truncate every
   * description in the game.
   *
   * A hundred and eighty-four is where the spill would stop being harmless: that is the
   * distance from BUF to QQ18, the recursive token table, which the text system reads while it
   * is filling this. Nothing in the game comes close.
   */
  static constexpr std::size_t BUFFER_SIZE = 184;

  /// The column a justified line breaks at. Thirty characters, then a form feed.
  static constexpr std::uint8_t LINE_WIDTH = 30;

  /// 6502: character 12, which is a newline everywhere except inside the line buffer, where it
  /// is the instruction to empty it.
  static constexpr std::uint8_t FORM_FEED = 12;

  explicit CharacterPrinter(TextSink& _screen) noexcept
    : m_screen(_screen)
  {
  }

  /// 6502: DASC -- route one character, and justify the buffered line when one is asked for.
  void Put(std::uint8_t _character) noexcept override;

  ExtendedTextState state;

  /// 6502: BUF -- the line being justified. Public because MT17 reaches into it.
  std::array<std::uint8_t, BUFFER_SIZE> buffer{};

private:
  /// 6502: DA1 to DAL4 -- empty the buffer to the screen, thirty columns at a time.
  void Justify() noexcept;

  /// 6502: DAS1 -- print the first _count characters of the buffer.
  void Emit(std::uint8_t _count) noexcept;

  /*
   * 6502: DA11 through DAL3 -- widen one gap in the line, and say whether the line is ready.
   *
   * The rotating bit that chooses which gap lives in SC+1, the screen pointer's high byte,
   * borrowed for the purpose. It is passed by reference here for the same reason it is a
   * variable there: it carries from one gap to the next within a line.
   */
  [[nodiscard]] bool PadToWidth(std::uint8_t& _rotor) noexcept;

  TextSink& m_screen; ///< 6502: CHPR
};

/*
 * The control codes that leave the text system.
 *
 * JMTB has THIRTY-ONE reachable entries, not the twenty-one the low ones suggest, and every one
 * of them is used by a token the game prints. Codes 22 to 31 are the mission briefings and the
 * disk menu: they wait for keys, spin the title ship, read a typed line, or print a token under
 * a game-state index. Nine and eleven reach the canvas.
 *
 * So this seam carries 9, 11, 22, 24, 25, 26, 27, 28, 30 and 31. Everything else in the range
 * is text and is handled by the printer below.
 *
 * Codes 8, 21, 23 and 29 are SPLIT rather than deferred whole: the flags they set belong to the
 * text system and are set here, and only the cursor move or the screen clear is passed on. A
 * handler for those four must not set those flags again, or it will set them twice.
 *
 * A printer built without a handler ignores the deferred codes, and the tests count how often
 * that happens.
 */
class ControlCodes
{
public:
  virtual ~ControlCodes() = default;
  virtual void Run(std::uint8_t _code) = 0;
};

/*
 * 6502: DETOK, DETOK2, DETOK3.
 *
 * The second and larger of Elite's two text systems. Where the recursive tokens are a
 * compression scheme, these are closer to a small interpreter: a byte can be a character, a
 * letter pair, another extended token, one of several randomised alternatives, or a control
 * code. That is how the game fits its mission briefings and system descriptions into a few
 * kilobytes and still has them read differently each time.
 */
class ExtendedTokenPrinter
{
public:
  ExtendedTokenPrinter(CharacterPrinter& _characters, TokenPrinter& _recursive, Rng& _rng,
                       ControlCodes* _controls = nullptr) noexcept
    : m_characters(_characters)
    , m_recursive(_recursive)
    , m_rng(_rng)
    , m_controls(_controls)
  {
  }

  /// 6502: DETOK -- print extended token N from the main table.
  void Print(std::uint8_t _token) noexcept;

  /// 6502: DETOK3 -- the same, from the per-system override table.
  void PrintSystemOverride(std::uint8_t _token) noexcept;

  /// 6502: DETOK2 -- act on one byte of a token's text. Public because the walkers are not the
  /// only callers in the game.
  void PrintByte(std::uint8_t _byte) noexcept;

  /// 6502: DASC -- the routine this printer sends its characters to, and the one every other
  /// part of the game prints through as well. Callers that print a NUMBER rather than a token
  /// need it, because BPRNT ends there too.
  [[nodiscard]] CharacterPrinter& Characters() noexcept { return m_characters; }

  /// 6502: DTW1 to DTW8, and BUF. They live with DASC because that is the routine that reads
  /// and writes most of them.
  [[nodiscard]] ExtendedTextState& State() noexcept { return m_characters.state; }

private:
  void Walk(const std::uint8_t* _table, std::size_t _size, std::uint8_t _token) noexcept;

  /// 6502: DTS -- one character, under the case state, then on to DASC.
  void PrintCharacter(std::uint8_t _character) noexcept;

  /// 6502: DT6 -- pick one of up to five alternatives and print that instead.
  void PrintRandomVariant(std::uint8_t _byte) noexcept;

  /// 6502: DT3 and the JMTB jump table -- one control code.
  void RunControlCode(std::uint8_t _code) noexcept;

  /// 6502: MT17 -- the current system's name, turned into an adjective.
  void PrintSystemAdjective() noexcept;

  /// 6502: MT18 -- a random pronounceable word, one to four letter pairs long.
  void PrintRandomWord() noexcept;

  CharacterPrinter& m_characters;
  TokenPrinter& m_recursive;
  Rng& m_rng;
  ControlCodes* m_controls = nullptr;
};

} // namespace Elite
