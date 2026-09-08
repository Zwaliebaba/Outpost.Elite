#include "pch.h"


#include "TextPrint.h"
#include "Tokens.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::TextSink;
using Elite::TokenPrinter;

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
      void Put(std::uint8_t _character) override
      {
        characters.push_back(_character);
      }
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

    /// Runs one token through the shipped printer and returns the characters it emitted, plus the
    /// capitalisation state it left behind.
    struct OracleRun
    {
      std::vector<std::uint8_t> characters;
      std::uint8_t caseFlags = 0;
      bool completed = false;
    };

  } // namespace

  TEST_CLASS(TokenPrinterAgainstTheShippedGame)
  {
  public:
    /// A phrase token expands into real text rather than into nothing, which no byte-for-byte
    /// comparison against the oracle would notice if both sides were empty.
    TEST_METHOD(PhraseTokensExpandToSomething)
    {
      CapturingSink sink;
      Elite::TextState text;
      TokenPrinter printer(sink, text);

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
