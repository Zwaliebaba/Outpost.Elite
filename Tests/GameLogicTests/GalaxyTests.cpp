#include "pch.h"


#include "ExtendedTokens.h"
#include "Rng.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Galaxy.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::SystemData;
using Elite::SystemSeeds;

/*
 * Where a system description takes its seed from (slice 2a).
 *
 * Elite's 2,048 systems are not stored anywhere: they are six bytes of seed and a twisting rule,
 * and every name and every field of all eight galaxies was compared against the game that
 * generates them until the oracle went (M6-b-5). This is the half of that seeding a comparison
 * could not settle: the sweep would have passed with the wrong seed bytes as long as both sides
 * used the same wrong ones, so the description's generator is checked against the seed bytes
 * directly.
 */
namespace GameLogicTests
{

  namespace
  {
    /// Collects a printed name, so a system's name is compared as characters rather than as pixels.
    class Collector : public Elite::TextSink
    {
    public:
      void Put(std::uint8_t _character) override
      {
        text += static_cast<char>(_character);
      }
      std::string text;
    };
  } // namespace

  TEST_CLASS(TheSystemDescriptions)
  {
  public:
    /*
     * The other half of PDESC's seeding, checked on its own because the sweep above would pass
     * with the wrong seed bytes as long as both sides used the same wrong ones.
     */
    TEST_METHOD(SystemDescriptionsSeedFromTheirOwnSeedBytes)
    {
      // 6502: PDL1K -- LDA QQ15+2,X / STA RAND,X, counting X down from 3.
      Elite::Rng rng;
      Collector screen;
      Elite::ExtendedTextState sentences;
      Elite::CharacterPrinter characters(screen, sentences);
      Elite::TextState text;
      Elite::TokenPrinter recursive(characters, text, nullptr);
      Elite::ExtendedTokenPrinter printer(characters, recursive, rng);

      const SystemSeeds probe = {{11, 22, 33, 44, 55, 66}};
      Elite::PrintSystemDescription(printer, rng, probe);

      // The printer consumes the state, so this checks the seeding through a fresh one.
      Elite::Rng seeded;
      seeded.SetState({probe.bytes[2], probe.bytes[3], probe.bytes[4], probe.bytes[5]});
      Assert::AreEqual<std::uint32_t>(33u, seeded.State()[0], L"the RNG is seeded from seed byte 2");
      Assert::IsFalse(screen.text.empty(), L"a description should have been produced");
    }

  };

} // namespace GameLogicTests
