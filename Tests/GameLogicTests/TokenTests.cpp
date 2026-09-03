#include "pch.h"

#include "OracleImage.h"

#include "Tokens.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::TextSink;
using Elite::TokenPrinter;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The token printer against the shipped one (slice 1c).
 *
 * These are the first tests that compare OUTPUT rather than arithmetic, and they work by
 * trapping the game's character routine: every call to it is recorded with the byte it was
 * handed, and returns immediately. So the comparison is between two lists of characters, with
 * none of the screen code running on either side.
 *
 * Tokens 0 to 5 print commander and system values, which phase 2 owns, so they are excluded
 * here rather than guessed at. Everything from 6 upward is compared for every value.
 */
namespace GameLogicTests
{

namespace
{

/// Collects what the port prints.
class CapturingSink : public TextSink
{
public:
  void Put(std::uint8_t _character) override { characters.push_back(_character); }
  std::vector<std::uint8_t> characters;
};

/*
 * Notes when an expansion reaches a value token instead of printing one.
 *
 * Some phrases embed tokens 0 to 5, which print cash, fuel or the current system. Those read
 * commander and system state that phase 2 owns, so the port has nothing to print and the game
 * prints whatever its uninitialised state holds. Comparing those would be comparing against
 * noise, so they are recorded and skipped -- and counted, so that a change which quietly made
 * *everything* skip would be visible rather than green.
 */
class DeferredValueTokens : public Elite::ValueTokens
{
public:
  void Print(std::uint8_t _token, TextSink&) override
  {
    reached = true;
    lastToken = _token;
  }

  bool reached = false;
  std::uint8_t lastToken = 0;
};

bool OracleMissing()
{
  const OracleImage& oracle = OracleImage::Instance();
  if (oracle.Available())
  {
    return false;
  }
  Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
  return true;
}

std::wstring Describe(const std::vector<std::uint8_t>& _bytes)
{
  std::wstring text = L"[";
  for (std::size_t index = 0; index < _bytes.size() && index < 40; ++index)
  {
    if (index != 0)
    {
      text += L' ';
    }
    text += std::to_wstring(_bytes[index]);
  }
  if (_bytes.size() > 40)
  {
    text += L" ...";
  }
  return text + L"]";
}

/// Runs one token through the shipped printer and returns the characters it emitted, plus the
/// capitalisation state it left behind.
struct OracleRun
{
  std::vector<std::uint8_t> characters;
  std::uint8_t caseFlags = 0;
  bool completed = false;
};

OracleRun RunShippedPrinter(std::uint8_t _token, std::uint8_t _caseFlags)
{
  const OracleImage& oracle = OracleImage::Instance();

  Cpu6502 cpu = oracle.Fresh();
  cpu.AddTrap(oracle.Label("TT26"));

  const std::uint16_t caseFlagsAddress = oracle.Label("QQ17");
  cpu.memory[caseFlagsAddress] = _caseFlags;
  cpu.a = _token;
  cpu.x = cpu.y = 0;
  cpu.sp = 0xFD;
  cpu.c = false;

  const auto run = cpu.CallSubroutine(oracle.Label("TT27"), 200'000);

  OracleRun result;
  result.completed = run.completed && !run.illegalOpcode;
  result.caseFlags = cpu.memory[caseFlagsAddress];
  for (const auto& hit : cpu.trapHits)
  {
    result.characters.push_back(hit.a);
  }
  return result;
}

/// Compares one token. Returns false when the expansion reached game state and was skipped.
bool CompareToken(std::uint8_t _token, std::uint8_t _caseFlags)
{
  const std::wstring where =
    L" for token " + std::to_wstring(_token) + L" with case flags " + std::to_wstring(_caseFlags);

  CapturingSink sink;
  DeferredValueTokens deferred;
  TokenPrinter printer(sink, &deferred);
  printer.SetCaseFlags(_caseFlags);
  printer.Print(_token);

  if (deferred.reached)
  {
    return false;
  }

  const OracleRun expected = RunShippedPrinter(_token, _caseFlags);
  Assert::IsTrue(expected.completed, (L"the shipped printer should return" + where).c_str());

  if (sink.characters != expected.characters)
  {
    const std::wstring message = L"characters differ" + where + L"\n  game: " + Describe(expected.characters)
                                 + L"\n  port: " + Describe(sink.characters);
    Assert::Fail(message.c_str());
  }

  Assert::AreEqual<std::uint32_t>(expected.caseFlags, printer.CaseFlags(), (L"case flags differ" + where).c_str());
  return true;
}

/// Compares every token from 6 upward and asserts that the great majority were comparable.
void CompareEveryToken(std::uint8_t _caseFlags)
{
  std::uint32_t compared = 0;
  std::uint32_t skipped = 0;

  for (std::uint32_t token = 6; token < 256; ++token)
  {
    if (CompareToken(static_cast<std::uint8_t>(token), _caseFlags))
    {
      ++compared;
    }
    else
    {
      ++skipped;
    }
  }

  Logger::WriteMessage(
    ("case flags " + std::to_string(_caseFlags) + ": compared " + std::to_string(compared) + ", deferred to phase 2 "
     + std::to_string(skipped))
      .c_str());

  Assert::IsTrue(compared > 200, L"most tokens should be comparable without game state");
  Assert::IsTrue(skipped < 30, L"only a few phrases should reach commander or system state");
}

} // namespace

TEST_CLASS(TokenPrinterAgainstTheShippedGame)
{
public:
  /// Every token from 6 upward, with no capitalisation asked for.
  TEST_METHOD(EveryTokenMatchesInPlainMode)
  {
    if (OracleMissing())
    {
      return;
    }
    CompareEveryToken(0);
  }

  /// The same, in sentence case, which is the mode that makes the printer stateful.
  TEST_METHOD(EveryTokenMatchesInSentenceCase)
  {
    if (OracleMissing())
    {
      return;
    }
    CompareEveryToken(0x80);
  }

  /// And in the three remaining states the flags can be in.
  TEST_METHOD(EveryTokenMatchesInTheRemainingCaseStates)
  {
    if (OracleMissing())
    {
      return;
    }
    for (const std::uint8_t flags : { std::uint8_t{ 0x40 }, std::uint8_t{ 0xC0 }, std::uint8_t{ 0xFF } })
    {
      CompareEveryToken(flags);
    }
  }

  /// The flags survive across calls, which is the whole reason the printer holds state.
  TEST_METHOD(CapitalisationCarriesBetweenCalls)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();

    // Ask for sentence case, then print two letters: the first stays capital, the second does
    // not, and the game decides that across three separate calls.
    Cpu6502 cpu = oracle.Fresh();
    cpu.AddTrap(oracle.Label("TT26"));
    const std::uint16_t caseFlagsAddress = oracle.Label("QQ17");
    cpu.memory[caseFlagsAddress] = 0;

    for (const std::uint8_t token : { std::uint8_t{ 6 }, std::uint8_t{ 'A' }, std::uint8_t{ 'B' } })
    {
      cpu.a = token;
      cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      Assert::IsTrue(cpu.CallSubroutine(oracle.Label("TT27"), 200'000).completed);
    }

    std::vector<std::uint8_t> expected;
    for (const auto& hit : cpu.trapHits)
    {
      expected.push_back(hit.a);
    }

    CapturingSink sink;
    TokenPrinter printer(sink);
    printer.Print(6);
    printer.Print('A');
    printer.Print('B');

    Assert::IsTrue(sink.characters == expected,
                   (L"a sequence of calls should agree\n  game: " + Describe(expected) + L"\n  port: "
                    + Describe(sink.characters))
                     .c_str());
    Assert::AreEqual<std::uint32_t>(cpu.memory[caseFlagsAddress], printer.CaseFlags());
  }

  /// A phrase token expands into real text rather than into nothing, which no byte-for-byte
  /// comparison against the oracle would notice if both sides were empty.
  TEST_METHOD(PhraseTokensExpandToSomething)
  {
    CapturingSink sink;
    TokenPrinter printer(sink);

    std::size_t nonEmpty = 0;
    for (std::uint32_t token = 96; token < 128; ++token)
    {
      sink.characters.clear();
      printer.SetCaseFlags(0);
      printer.Print(static_cast<std::uint8_t>(token));
      if (sink.characters.size() > 1)
      {
        ++nonEmpty;
      }
    }

    Assert::IsTrue(nonEmpty > 20, L"most phrase tokens should expand to several characters");
  }
};

} // namespace GameLogicTests
