#include "pch.h"

#include "ExtendedTokens.h"

#include "EliteConfig.h"
#include "LookupTables.h"

/*
 * The extended token system (slice 1c-b).
 *
 * A byte inside a token's text means one of five things, and the ranges are the game's:
 *
 *     0-31      a control code: move the cursor, clear the screen, change the subject
 *     32-90     a character
 *     91-128    one of up to five randomised alternatives
 *     129-214   another extended token, expanded in place
 *     215-255   a pair of letters
 *
 * The randomised range is what makes the game's system descriptions read differently on each
 * visit, and it is why this printer needs the random generator: the choice comes out of the
 * same sequence everything else does, so a description is reproducible from the seed rather
 * than merely varied.
 */

namespace Elite
{

namespace
{
constexpr std::uint8_t FIRST_CHARACTER = 32;
constexpr std::uint8_t FIRST_VARIANT = 91;
constexpr std::uint8_t FIRST_NESTED = 129;
constexpr std::uint8_t FIRST_PAIR = 215;
constexpr std::uint8_t UPPER_A = 'A';

/// The four thresholds that turn a random byte into one of five alternatives.
constexpr std::uint8_t VARIANT_THRESHOLDS[4] = { 51, 102, 153, 204 };
} // namespace

void ExtendedTokenPrinter::Print(std::uint8_t _token) noexcept
{
  Walk(EXTENDED_TOKEN_TABLE.data(), EXTENDED_TOKEN_TABLE.size(), _token);
}

void ExtendedTokenPrinter::PrintSystemOverride(std::uint8_t _token) noexcept
{
  Walk(SYSTEM_TOKEN_TABLE.data(), SYSTEM_TOKEN_TABLE.size(), _token);
}

void ExtendedTokenPrinter::Walk(const std::uint8_t* _table, std::size_t _size, std::uint8_t _token) noexcept
{
  // Find the token by counting terminators, exactly as the recursive table is walked. The
  // difference is the key, and that the text may run past what a single byte could index.
  std::size_t offset = 0;
  std::uint8_t remaining = _token;

  while (remaining != 0 && offset < _size)
  {
    const std::uint8_t decoded = static_cast<std::uint8_t>(_table[offset] ^ EXTENDED_TOKEN_KEY);
    if (decoded == 0)
    {
      --remaining;
    }
    ++offset;
  }

  if (remaining != 0)
  {
    return;
  }

  // Unlike the recursive table this checks before printing, so an empty token prints nothing.
  while (offset < _size)
  {
    const std::uint8_t decoded = static_cast<std::uint8_t>(_table[offset] ^ EXTENDED_TOKEN_KEY);
    if (decoded == 0)
    {
      return;
    }

    PrintByte(decoded);
    ++offset;
  }
}

void ExtendedTokenPrinter::PrintByte(std::uint8_t _byte) noexcept
{
  if (_byte < FIRST_CHARACTER)
  {
    // 6502: DT3 -- a control code, dispatched through a jump table into the MT routines. Every
    // one of them reaches the canvas or game state, so they land with those.
    if (m_controls != nullptr)
    {
      m_controls->Run(_byte);
    }
    return;
  }

  if ((state.toLineBuffer & 0x80u) != 0u)
  {
    // Characters go to the justified line buffer instead, which is the recursive printer's job.
    m_recursive.Print(_byte);
    return;
  }

  if (_byte < FIRST_VARIANT)
  {
    PrintCharacter(_byte);
    return;
  }

  if (_byte < FIRST_NESTED)
  {
    PrintRandomVariant(_byte);
    return;
  }

  if (_byte < FIRST_PAIR)
  {
    Print(_byte);
    return;
  }

  // 6502: the TKN2 path -- a pair of letters from the top of the range.
  const std::size_t index = static_cast<std::size_t>(_byte - FIRST_PAIR) * 2u;
  if (index + 1 >= EXTENDED_PAIR_TABLE.size())
  {
    return;
  }

  PrintCharacter(EXTENDED_PAIR_TABLE[index]);
  PrintCharacter(EXTENDED_PAIR_TABLE[index + 1]);
}

void ExtendedTokenPrinter::PrintCharacter(std::uint8_t _character) noexcept
{
  if (_character < UPPER_A)
  {
    // Digits, spaces and punctuation are never case-folded.
    m_sink.Put(_character);
    return;
  }

  std::uint8_t value = _character;

  // Lower case applies always, or while a word is already under way. The one case that skips
  // it is a sentence that has just started with nothing forcing lower case.
  const bool forceLower = (state.alwaysLower & 0x80u) != 0u;
  const bool midSentence = (state.sentenceStart & 0x80u) != 0u;

  if (forceLower || !midSentence)
  {
    value = static_cast<std::uint8_t>(value | state.lowerCaseBits);
  }

  value = static_cast<std::uint8_t>(value & state.caseMask);
  m_sink.Put(value);
}

void ExtendedTokenPrinter::PrintRandomVariant(std::uint8_t _byte) noexcept
{
  const std::uint8_t roll = m_rng.NextRepeatable().value;

  // How many thresholds the roll cleared decides which alternative is used. The last threshold
  // is folded into the addition rather than counted separately, which is an instruction saved
  // in the original and a footnote here.
  std::uint8_t choice = 0;
  for (std::size_t index = 0; index < 3; ++index)
  {
    if (roll >= VARIANT_THRESHOLDS[index])
    {
      ++choice;
    }
  }

  const std::size_t baseIndex = static_cast<std::size_t>(_byte) - FIRST_VARIANT;
  if (baseIndex >= VARIANT_BASE_TABLE.size())
  {
    return;
  }

  const bool clearedLast = roll >= VARIANT_THRESHOLDS[3];
  const std::uint8_t token =
    static_cast<std::uint8_t>(choice + VARIANT_BASE_TABLE[baseIndex] + (clearedLast ? 1u : 0u));

  Print(token);
}

} // namespace Elite
