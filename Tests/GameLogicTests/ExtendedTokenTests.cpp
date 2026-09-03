#include "pch.h"

#include "OracleImage.h"

#include "ExtendedTokens.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::ExtendedTokenPrinter;
using Elite::Rng;
using Elite::TextSink;
using Elite::TokenPrinter;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The extended token printer against the shipped one (slice 1c-b).
 *
 * Same trap on the character routine as the recursive suite. Two things make this harder than
 * that one:
 *
 * Some tokens print one of five randomised alternatives, drawn from the game's own generator.
 * So both sides start from the same four bytes of generator state, and a mismatch in how many
 * times either draws would show up immediately as diverging text rather than silently.
 *
 * And the control codes, 0 to 31, drive the cursor and the screen. Those are a seam this slice
 * does not cross, so tokens that reach one are counted and set aside rather than compared
 * against a screen the port does not have.
 */
namespace GameLogicTests
{

namespace
{

class CapturingSink : public TextSink
{
public:
  void Put(std::uint8_t _character) override { characters.push_back(_character); }
  std::vector<std::uint8_t> characters;
};

class DeferredControls : public Elite::ControlCodes
{
public:
  void Run(std::uint8_t _code) override
  {
    reached = true;
    lastCode = _code;
  }
  bool reached = false;
  std::uint8_t lastCode = 0;
};

/// Value tokens reach commander state, which is phase 2's.
class DeferredValues : public Elite::ValueTokens
{
public:
  void Print(std::uint8_t, TextSink&) override { reached = true; }
  bool reached = false;
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
  for (std::size_t index = 0; index < _bytes.size() && index < 48; ++index)
  {
    if (index != 0)
    {
      text += L' ';
    }
    text += std::to_wstring(_bytes[index]);
  }
  if (_bytes.size() > 48)
  {
    text += L" ...";
  }
  return text + L"]";
}

constexpr std::array<std::uint8_t, 4> SEED = { 0x5A, 0x4A, 0x02, 0x48 };

struct TextStateBytes
{
  std::uint8_t lowerCaseBits = 0;
  std::uint8_t sentenceStart = 0;
  std::uint8_t toLineBuffer = 0;
  std::uint8_t alwaysLower = 0;
  std::uint8_t caseMask = 0xFF;
};

/// Runs one extended token through the shipped printer and returns what it emitted.
std::vector<std::uint8_t> RunShipped(std::uint8_t _token, const TextStateBytes& _state, bool& _outCompleted)
{
  const OracleImage& oracle = OracleImage::Instance();

  Cpu6502 cpu = oracle.Fresh();
  cpu.AddTrap(oracle.Label("DASC"));

  for (std::size_t index = 0; index < SEED.size(); ++index)
  {
    cpu.memory[static_cast<std::uint16_t>(oracle.Label("RAND") + index)] = SEED[index];
  }

  cpu.memory[oracle.Label("DTW1")] = _state.lowerCaseBits;
  cpu.memory[oracle.Label("DTW2")] = _state.sentenceStart;
  cpu.memory[oracle.Label("DTW3")] = _state.toLineBuffer;
  cpu.memory[oracle.Label("DTW6")] = _state.alwaysLower;
  cpu.memory[oracle.Label("DTW8")] = _state.caseMask;
  cpu.memory[oracle.Label("QQ17")] = 0;

  cpu.a = _token;
  cpu.x = cpu.y = 0;
  cpu.sp = 0xFD;
  cpu.c = false;

  const auto run = cpu.CallSubroutine(oracle.Label("DETOK"), 500'000);
  _outCompleted = run.completed && !run.illegalOpcode;

  std::vector<std::uint8_t> characters;
  for (const auto& hit : cpu.trapHits)
  {
    characters.push_back(hit.a);
  }
  return characters;
}

/// Compares one token. Returns false when it reached a seam this slice does not cross.
bool CompareToken(std::uint8_t _token, const TextStateBytes& _state)
{
  CapturingSink sink;
  Rng rng;
  rng.SetState(SEED);
  DeferredValues values;
  TokenPrinter recursive(sink, &values);
  DeferredControls controls;
  ExtendedTokenPrinter printer(sink, recursive, rng, &controls);

  printer.state.lowerCaseBits = _state.lowerCaseBits;
  printer.state.sentenceStart = _state.sentenceStart;
  printer.state.toLineBuffer = _state.toLineBuffer;
  printer.state.alwaysLower = _state.alwaysLower;
  printer.state.caseMask = _state.caseMask;

  printer.Print(_token);

  if (controls.reached || values.reached)
  {
    return false;
  }

  bool completed = false;
  const std::vector<std::uint8_t> expected = RunShipped(_token, _state, completed);

  const std::wstring where = L" for extended token " + std::to_wstring(_token);
  Assert::IsTrue(completed, (L"the shipped printer should return" + where).c_str());

  if (sink.characters != expected)
  {
    Assert::Fail((L"characters differ" + where + L"\n  game: " + Describe(expected) + L"\n  port: "
                  + Describe(sink.characters))
                   .c_str());
  }

  return true;
}

void CompareEveryToken(const TextStateBytes& _state, const char* _label)
{
  std::uint32_t compared = 0;
  std::uint32_t deferred = 0;

  for (std::uint32_t token = 1; token < 256; ++token)
  {
    if (CompareToken(static_cast<std::uint8_t>(token), _state))
    {
      ++compared;
    }
    else
    {
      ++deferred;
    }
  }

  Logger::WriteMessage(
    (std::string(_label) + ": compared " + std::to_string(compared) + ", deferred " + std::to_string(deferred)).c_str());

  Assert::IsTrue(compared > 60, L"a substantial share of tokens should be pure text");
}

} // namespace

TEST_CLASS(ExtendedTokenPrinterAgainstTheShippedGame)
{
public:
  TEST_METHOD(EveryTokenMatchesWithNoCaseFolding)
  {
    if (OracleMissing())
    {
      return;
    }
    CompareEveryToken(TextStateBytes{}, "plain");
  }

  /// The state the game is normally in while writing a description: lower case forced, with the
  /// bits that do it set in the mask.
  TEST_METHOD(EveryTokenMatchesInLowerCaseMode)
  {
    if (OracleMissing())
    {
      return;
    }
    TextStateBytes state;
    state.lowerCaseBits = 0x20;
    state.alwaysLower = 0x80;
    state.caseMask = 0xFF;
    CompareEveryToken(state, "lower case");
  }

  /// Sentence case: the first letter of a word stays capital.
  TEST_METHOD(EveryTokenMatchesInSentenceCase)
  {
    if (OracleMissing())
    {
      return;
    }
    TextStateBytes state;
    state.lowerCaseBits = 0x20;
    state.sentenceStart = 0x80;
    CompareEveryToken(state, "sentence case");
  }

  /// The per-system override table is walked by the same machinery over different bytes.
  TEST_METHOD(SystemOverridesMatch)
  {
    if (OracleMissing())
    {
      return;
    }
    const OracleImage& oracle = OracleImage::Instance();

    std::uint32_t compared = 0;
    std::uint32_t comparedWithText = 0;

    // The override table holds 27 entries, and the last of them is not followed by a
    // terminator inside the table. That matters because the game's walker is not a bounded
    // lookup: it counts terminators with nothing to stop it, so reaching for the final entry
    // sends it off the end of the table and into whatever follows. The port bounds-checks and
    // returns instead, so past that point the two are not comparable -- one has stopped and the
    // other is still running.
    //
    // The 26 entries below it are bounded on both sides and are what this compares. In the game
    // the last entry is reached through a lookup that never asks for it in isolation.
    constexpr std::uint32_t OVERRIDE_ENTRY_COUNT = 26;

    for (std::uint32_t token = 1; token <= OVERRIDE_ENTRY_COUNT; ++token)
    {
      CapturingSink sink;
      Rng rng;
      rng.SetState(SEED);
      DeferredValues values;
      TokenPrinter recursive(sink, &values);
      DeferredControls controls;
      ExtendedTokenPrinter printer(sink, recursive, rng, &controls);
      printer.PrintSystemOverride(static_cast<std::uint8_t>(token));

      if (controls.reached || values.reached)
      {
        continue;
      }

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("DASC"));
      for (std::size_t index = 0; index < SEED.size(); ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(oracle.Label("RAND") + index)] = SEED[index];
      }

      // Every state byte has to be set, not just the mask. These live inside a loaded code
      // block, so leaving one alone means comparing against whatever the binary happens to
      // hold there rather than against the port's starting state.
      cpu.memory[oracle.Label("DTW1")] = 0;
      cpu.memory[oracle.Label("DTW2")] = 0;
      cpu.memory[oracle.Label("DTW3")] = 0;
      cpu.memory[oracle.Label("DTW6")] = 0;
      cpu.memory[oracle.Label("DTW8")] = 0xFF;
      cpu.memory[oracle.Label("QQ17")] = 0;
      cpu.a = static_cast<std::uint8_t>(token);
      cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      Assert::IsTrue(cpu.CallSubroutine(oracle.Label("DETOK3"), 500'000).completed,
                     (L"the shipped walker should return for override " + std::to_wstring(token)).c_str());

      std::vector<std::uint8_t> expected;
      for (const auto& hit : cpu.trapHits)
      {
        expected.push_back(hit.a);
      }

      if (sink.characters != expected)
      {
        Assert::Fail((L"system override " + std::to_wstring(token) + L" differs\n  game: " + Describe(expected)
                      + L"\n  port: " + Describe(sink.characters))
                       .c_str());
      }
      ++compared;
      if (!sink.characters.empty())
      {
        ++comparedWithText;
      }
    }

    // Most override entries open with a nested token whose own text begins with a control
    // code, so the number that stay inside this slice is small. Counting how many produced
    // actual characters is what makes this assertion mean something: a port that returned
    // nothing everywhere would match an oracle that also emitted nothing, and pass.
    Logger::WriteMessage(("system overrides compared: " + std::to_string(compared) + ", of which produced text: "
                          + std::to_string(comparedWithText))
                           .c_str());
    Assert::IsTrue(comparedWithText >= 1, L"at least one override must be compared with real text on both sides");
  }
};

} // namespace GameLogicTests
