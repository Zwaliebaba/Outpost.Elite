#pragma once

#include "Rng.h"
#include "Tokens.h"

#include <cstdint>

namespace Elite
{

/*
 * 6502: DTW1, DTW2, DTW3, DTW6, DTW8 -- the state the extended printer carries between bytes.
 *
 * These are five separate zero-page bytes in the original rather than a packed set of flags,
 * and they are kept separate here for the same reason: each is written independently by a
 * different control code, and folding them together would invent invariants the game does not
 * have.
 */
struct ExtendedTextState
{
  std::uint8_t lowerCaseBits = 0; ///< 6502: DTW1 -- bits set into a letter to lower its case
  std::uint8_t sentenceStart = 0; ///< 6502: DTW2 -- set while a word is being started
  std::uint8_t toLineBuffer = 0;  ///< 6502: DTW3 -- send characters to the buffer, not the screen
  std::uint8_t alwaysLower = 0;   ///< 6502: DTW6 -- lower case regardless of the sentence state
  std::uint8_t caseMask = 0xFF;   ///< 6502: DTW8 -- bits cleared from every letter
};

/*
 * The control codes, 0 to 31.
 *
 * These do not print: they move the cursor, clear the screen, switch to a different view, or
 * change what is being described. Every one of them reaches either the canvas or game state,
 * so they are a seam rather than something this slice can port. A printer built without a
 * handler ignores them, and the tests count how often that happens.
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
  ExtendedTokenPrinter(TextSink& _sink, TokenPrinter& _recursive, Rng& _rng, ControlCodes* _controls = nullptr) noexcept
    : m_sink(_sink)
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

  ExtendedTextState state;

private:
  void Walk(const std::uint8_t* _table, std::size_t _size, std::uint8_t _token) noexcept;

  /// 6502: DTS -- one character, under the case state.
  void PrintCharacter(std::uint8_t _character) noexcept;

  /// 6502: DT6 -- pick one of up to five alternatives and print that instead.
  void PrintRandomVariant(std::uint8_t _byte) noexcept;

  TextSink& m_sink;
  TokenPrinter& m_recursive;
  Rng& m_rng;
  ControlCodes* m_controls = nullptr;
};

} // namespace Elite
