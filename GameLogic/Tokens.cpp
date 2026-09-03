#include "pch.h"

#include "Tokens.h"

#include "TextPrint.h"

#include "EliteConfig.h"
#include "LookupTables.h"

/*
 * The recursive token system (slice 1c).
 *
 * A token byte means one of several things depending on its value, and the original decides
 * which by a ladder of decrements rather than a table, so the boundaries below are the same
 * boundaries the game uses rather than a tidied version of them.
 *
 *     0        cash, and 1 to 5 other values that live in game state
 *     6, 8     set and clear the sentence-case flag
 *     9        a line break, which also prints a colon
 *     7, 10-13, 32-95   an ordinary character
 *     14-31    a phrase, reached by adding an offset
 *     96-127   a phrase
 *     128-159  a pair of letters
 *     160-255  a phrase
 *
 * Token 7 is not a mistake: the ladder steps past it without a test, so it prints as a
 * character like any other byte in its range.
 */

namespace Elite
{

namespace
{
constexpr std::uint8_t UPPER_A = 'A';
constexpr std::uint8_t UPPER_Z = 'Z';
constexpr std::uint8_t CASE_TO_LOWER = 32;

constexpr std::uint8_t FLAG_SENTENCE_CASE = 0x80;
constexpr std::uint8_t FLAG_SEEN_FIRST_LETTER = 0x40;
constexpr std::uint8_t FLAG_SUPPRESS_ALL = 0xFF;

/// The token value that starts the letter-pair range, and the one that starts the phrases
/// beyond it.
constexpr std::uint8_t LETTER_PAIR_FIRST = 128;
constexpr std::uint8_t PHRASE_AFTER_PAIRS = 160;
constexpr std::uint8_t PHRASE_FIRST = 96;

/// 6502: crlf -- LDA #21 / JSR DOXC. Where control code 9 puts the cursor before its colon.
constexpr std::uint8_t CONTROL_CODE_9_COLUMN = 21;

/// Characters 14 to 31 are phrases in disguise; this is the offset that maps them.
constexpr std::uint8_t LOW_PHRASE_OFFSET = 114;
constexpr std::uint8_t LOW_PHRASE_FIRST = 14;
constexpr std::uint8_t LOW_PHRASE_LAST = 31;
} // namespace

void TokenPrinter::Print(std::uint8_t _token) noexcept
{
  // Tokens 0 to 5 print values rather than text, and those values are game state.
  if (_token <= 5)
  {
    if (m_values != nullptr)
    {
      m_values->Print(_token, m_sink);
    }
    return;
  }

  if (_token >= LETTER_PAIR_FIRST)
  {
    PrintLetterPair(_token);
    return;
  }

  if (_token == 6)
  {
    m_caseFlags = FLAG_SENTENCE_CASE;
    return;
  }

  if (_token == 8)
  {
    m_caseFlags = 0;
    return;
  }

  if (_token == 9)
  {
    // 6502: crlf -- LDA #21 / JSR DOXC / JMP TT73. Tab to column 21, then a colon.
    if (m_cursor != nullptr)
    {
      m_cursor->column = CONTROL_CODE_9_COLUMN;
    }
    Print(':');
    return;
  }

  if (_token >= PHRASE_FIRST)
  {
    PrintPhrase(_token);
    return;
  }

  if (_token >= LOW_PHRASE_FIRST && _token <= LOW_PHRASE_LAST)
  {
    PrintPhrase(static_cast<std::uint8_t>(_token + LOW_PHRASE_OFFSET));
    return;
  }

  PrintCharacter(_token);
}

void TokenPrinter::PrintCharacter(std::uint8_t _character) noexcept
{
  const std::uint8_t flags = m_caseFlags;

  // Nothing asked for any transformation.
  if (flags == 0)
  {
    m_sink.Put(_character);
    return;
  }

  if ((flags & FLAG_SENTENCE_CASE) != 0u)
  {
    if ((flags & FLAG_SEEN_FIRST_LETTER) != 0u)
    {
      // 6502: TT45 -- past the first letter of a sentence.
      if (flags == FLAG_SUPPRESS_ALL)
      {
        return;
      }

      if (_character >= UPPER_A)
      {
        // Everything after the first letter is lowered.
        if (_character <= UPPER_Z)
        {
          m_sink.Put(static_cast<std::uint8_t>(_character + CASE_TO_LOWER));
        }
        else
        {
          m_sink.Put(_character);
        }
        return;
      }

      // A non-letter ends the run, so the next letter starts a word again.
      m_caseFlags = static_cast<std::uint8_t>(flags & ~FLAG_SEEN_FIRST_LETTER);
      m_sink.Put(_character);
      return;
    }

    // 6502: TT41 -- looking for the first letter of the sentence, which stays capital.
    if (_character < UPPER_A)
    {
      m_sink.Put(_character);
      return;
    }

    m_caseFlags = static_cast<std::uint8_t>(flags | FLAG_SEEN_FIRST_LETTER);
    m_sink.Put(_character);
    return;
  }

  if ((flags & FLAG_SEEN_FIRST_LETTER) != 0u)
  {
    // 6502: TT46 -- clear the marker and print as-is.
    m_caseFlags = static_cast<std::uint8_t>(flags & ~FLAG_SEEN_FIRST_LETTER);
    m_sink.Put(_character);
    return;
  }

  // 6502: TT42 -- lower case throughout.
  if (_character >= UPPER_A && _character <= UPPER_Z)
  {
    m_sink.Put(static_cast<std::uint8_t>(_character + CASE_TO_LOWER));
    return;
  }

  m_sink.Put(_character);
}

void TokenPrinter::PrintLetterPair(std::uint8_t _token) noexcept
{
  if (_token >= PHRASE_AFTER_PAIRS)
  {
    // 6502: TT47 -- above the pair range this is a phrase after all.
    PrintPhrase(static_cast<std::uint8_t>(_token - PHRASE_AFTER_PAIRS));
    return;
  }

  const std::size_t index = static_cast<std::size_t>(_token & 0x7Fu) * 2u;
  if (index + 1 >= TWO_LETTER_TABLE.size())
  {
    return;
  }

  Print(TWO_LETTER_TABLE[index]);

  const std::uint8_t second = TWO_LETTER_TABLE[index + 1];
  if (second == '?')
  {
    // A question mark in the second slot means the pair is really one letter.
    return;
  }

  Print(second);
}

void TokenPrinter::PrintPhrase(std::uint8_t _token) noexcept
{
  // The table is one run of text after another, separated by zero bytes, so reaching token N
  // means stepping over N terminators. There is no index; walking is the lookup.
  std::size_t offset = 0;
  for (std::uint8_t remaining = _token; remaining != 0; --remaining)
  {
    while (offset < RECURSIVE_TOKEN_TABLE.size() && RECURSIVE_TOKEN_TABLE[offset] != 0u)
    {
      ++offset;
    }
    ++offset;

    if (offset >= RECURSIVE_TOKEN_TABLE.size())
    {
      return;
    }
  }

  // The first character is printed before any terminator check, so an empty run still emits
  // one byte. That is the original's behaviour and callers do not rely on it either way.
  while (offset < RECURSIVE_TOKEN_TABLE.size())
  {
    Print(static_cast<std::uint8_t>(RECURSIVE_TOKEN_TABLE[offset] ^ RECURSIVE_TOKEN_KEY));
    ++offset;

    if (offset >= RECURSIVE_TOKEN_TABLE.size() || RECURSIVE_TOKEN_TABLE[offset] == 0u)
    {
      return;
    }
  }
}

void PrintSpace(TokenPrinter& _printer) noexcept
{
  _printer.Print(' ');
}

void PrintNewline(TokenPrinter& _printer) noexcept
{
  _printer.Print(12);
}

void SetSentenceCaseAndNewline(TokenPrinter& _printer) noexcept
{
  _printer.SetCaseFlags(0x80);
  PrintNewline(_printer);
}

void PrintThenSpace(TokenPrinter& _printer, std::uint8_t _token) noexcept
{
  _printer.Print(_token);
  PrintSpace(_printer);
}

void PrintThenQuestion(TokenPrinter& _printer, std::uint8_t _token) noexcept
{
  _printer.Print(_token);
  _printer.Print('?');
}

void PrintThenColon(TokenPrinter& _printer, std::uint8_t _token) noexcept
{
  _printer.Print(_token);
  _printer.Print(':');
}

void PrintThenNewline(TokenPrinter& _printer, std::uint8_t _token) noexcept
{
  _printer.Print(_token);
  PrintNewline(_printer);
}

} // namespace Elite
