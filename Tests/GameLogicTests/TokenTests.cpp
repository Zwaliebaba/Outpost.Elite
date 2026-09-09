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
 * That a phrase token expands to something (slice 1c).
 *
 * Every token from 6 upward was compared against the shipped printer in each of its case states,
 * by trapping the game's character routine and comparing two lists of characters, and that went
 * with the oracle (M6-b-5). Tokens 0 to 5 print commander and system values and were never in it.
 * What is left is the guard that needs no comparison: a phrase token expands to a non-empty run
 * of characters, so a table that lost its entries fails here rather than printing blanks.
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

  } // namespace

  TEST_CLASS(TheTokenPrinter)
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
