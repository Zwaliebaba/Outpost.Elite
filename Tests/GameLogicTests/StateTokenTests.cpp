#include "pch.h"

#include "OracleImage.h"

#include "Canvas.h"
#include "Commander.h"
#include "ExtendedTokens.h"
#include "StateTokens.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::CharacterPrinter;
using Elite::CommanderBlock;
using Elite::Field;
using Elite::StateTokens;
using Elite::SystemSeeds;
using Elite::TextState;
using Elite::TokenPrinter;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The six control codes that print game state, against the game that prints them (slice 2c).
 *
 * 6502: TT27's first six branches. These were declared a seam in slice 1c-a because nothing that
 * could answer them existed; the commander block (2d) and the universe generator (2a) are what
 * closed that, and this is the comparison.
 *
 * Every one is compared CHARACTER FOR CHARACTER through a trap on DASC, which is where both the
 * shipped routines and the port put every character they produce. Two of them also change state
 * -- ypl swaps the seeds and printing twists them -- so the seeds are compared afterwards too.
 */
namespace GameLogicTests
{

namespace
{
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

std::wstring Widen(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// A sink that keeps what it was given, so two chains can be compared as lists of bytes.
class Recording : public Elite::TextSink
{
public:
  void Put(std::uint8_t _character) override { characters.push_back(_character); }
  std::vector<std::uint8_t> characters;
};

/// One state to print from. Chosen for the edges rather than for plausibility.
struct Situation
{
  const char* what;
  std::uint32_t cash;
  std::uint8_t galaxy;
  std::uint8_t fuel;
  std::array<std::uint8_t, 8> name;
  SystemSeeds current;
  SystemSeeds selected;
  bool misJumped;
};

std::string Describe(const std::vector<std::uint8_t>& _characters)
{
  std::string text;
  for (const std::uint8_t character : _characters)
  {
    text += (character >= 32 && character < 127) ? static_cast<char>(character) : '.';
  }
  return text;
}
} // namespace

TEST_CLASS(StateControlCodesMatchTheShippedGame)
{
public:
  /*
   * All six, over six situations, character for character.
   *
   * The situations cover what these routines actually branch on: no cash and the largest cash the
   * four bytes hold, the first and last galaxy, an empty tank and a full one, a name that fills
   * the block and one that ends immediately, and witchspace -- which is the only state in which
   * one of them prints nothing at all.
   */
  TEST_METHOD(EveryStateControlCodeMatches)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t tt27 = oracle.Label("TT27");
    const std::uint16_t dasc = oracle.Label("DASC");
    const std::uint16_t cashAt = oracle.Label("CASH");
    const std::uint16_t gcnt = oracle.Label("GCNT");
    const std::uint16_t qq14 = oracle.Label("QQ14");
    const std::uint16_t nameAt = oracle.Label("NAME");
    const std::uint16_t qq2 = oracle.Label("QQ2");
    const std::uint16_t qq15 = oracle.Label("QQ15");
    const std::uint16_t mj = oracle.Label("MJ");

    // 6502: the seeds for Lave and for Zaonce, which are two real systems rather than two
    // arbitrary six-byte values -- so a name that comes out wrong is recognisably wrong.
    const SystemSeeds LAVE{ { 0x5A, 0x4A, 0x02, 0x53, 0xB7, 0x00 } };
    const SystemSeeds OTHER{ { 0x93, 0xB4, 0x4E, 0x30, 0x1E, 0x02 } };

    const std::vector<Situation> SITUATIONS = {
      { "no cash, galaxy 0, no fuel", 0u, 0, 0, { 'J', 'A', 'M', 'E', 'S', 'O', 'N', 13 }, LAVE, OTHER, false },
      { "a hundred credits", 1000u, 0, 70, { 'J', 'A', 'M', 'E', 'S', 'O', 'N', 13 }, LAVE, OTHER, false },
      { "the largest cash the block holds", 0xFFFFFFFFu, 7, 255,
        { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 13 }, OTHER, LAVE, false },
      { "an empty name", 4242u, 3, 35, { 13, 'X', 'X', 'X', 'X', 'X', 'X', 13 }, LAVE, LAVE, false },
      { "a name with no room to spare", 99999u, 6, 1,
        { 'Z', 'Z', 'Z', 'Z', 'Z', 'Z', 'Z', 13 }, OTHER, OTHER, false },
      { "witchspace", 500u, 2, 20, { 'J', 'A', 'M', 'E', 'S', 'O', 'N', 13 }, LAVE, OTHER, true },
    };

    std::uint32_t compared = 0;
    std::string sample;

    for (const Situation& situation : SITUATIONS)
    {
      /*
       * Both case states, because one of these routines is only distinguishable in one of them.
       *
       * `cmn` prints the commander's name through TT26 rather than TT27, so the case flags do not
       * touch it -- and in ALL CAPS a port that used the token printer instead would look
       * identical. In Sentence Case it would print "Jameson" where the game prints "JAMESON".
       * Found by mutation: the version of this test that only ran with QQ17 = 0 passed both.
       */
      for (const std::uint8_t caseFlags : { std::uint8_t{ 0x00 }, std::uint8_t{ 0x80 } })
      {
      for (std::uint8_t token = 0; token <= 5; ++token)
      {
        const std::wstring where =
          Widen(std::string("control code ") + static_cast<char>('0' + token) + " (" + situation.what
                + (caseFlags != 0 ? ", sentence case" : ", all caps") + ")");

        // ---- the shipped routine -----------------------------------------------------------
        Cpu6502 cpu = oracle.Fresh();
        cpu.AddTrap(dasc, Cpu6502::TrapExit::ClearCarry);

        for (std::size_t index = 0; index < 4; ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(cashAt + index)] =
            static_cast<std::uint8_t>(situation.cash >> (24 - 8 * index));
        }
        cpu.memory[gcnt] = situation.galaxy;
        cpu.memory[qq14] = situation.fuel;
        cpu.memory[mj] = situation.misJumped ? 0x80u : 0x00u;
        for (std::size_t index = 0; index < situation.name.size(); ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(nameAt + index)] = situation.name[index];
        }
        for (std::size_t index = 0; index < 6; ++index)
        {
          cpu.memory[static_cast<std::uint16_t>(qq2 + index)] = situation.current.bytes[index];
          cpu.memory[static_cast<std::uint16_t>(qq15 + index)] = situation.selected.bytes[index];
        }

        // 6502: QQ17 -- 0 is ALL CAPS, bit 7 is Sentence Case with the next letter capitalised.
        cpu.memory[oracle.Label("QQ17")] = caseFlags;
        cpu.a = token;
        cpu.x = cpu.y = 0;
        cpu.sp = 0xFD;

        const auto run = cpu.CallSubroutine(tt27, 200'000);
        Assert::IsTrue(run.completed, (where + L": the shipped routine should return").c_str());

        std::vector<std::uint8_t> gameCharacters;
        for (const Cpu6502::TrapHit& hit : cpu.trapHits)
        {
          gameCharacters.push_back(hit.a);
        }

        // ---- the port ----------------------------------------------------------------------
        CommanderBlock commander;
        commander.SetCash(situation.cash);
        commander.At(Field::GalaxyNumber) = situation.galaxy;
        commander.At(Field::Fuel) = situation.fuel;

        SystemSeeds current = situation.current;
        SystemSeeds selected = situation.selected;

        Recording recording;
        CharacterPrinter characters(recording);
        TextState text;
        TokenPrinter printer(characters);
        printer.SetCaseFlags(caseFlags);

        StateTokens tokens(printer, text, commander,
                           std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(situation.name),
                           current, selected, situation.misJumped);
        printer.SetValueTokens(&tokens);
        printer.Print(token);

        // ---- compare -----------------------------------------------------------------------
        Assert::AreEqual(gameCharacters.size(), recording.characters.size(),
                         (where + L": how many characters were printed -- game \"" + Widen(Describe(gameCharacters))
                          + L"\", port \"" + Widen(Describe(recording.characters)) + L"\"")
                           .c_str());
        for (std::size_t index = 0; index < gameCharacters.size(); ++index)
        {
          Assert::AreEqual(gameCharacters[index], recording.characters[index],
                           (where + L": character " + std::to_wstring(index) + L" -- game \""
                            + Widen(Describe(gameCharacters)) + L"\", port \""
                            + Widen(Describe(recording.characters)) + L"\"")
                             .c_str());
        }

        /*
         * The seeds afterwards, which is where ypl earns its own test. It swaps QQ2 and QQ15,
         * prints, and swaps back -- and printing TWISTS the seeds, so what comes back is not what
         * went in. A port that restored the originals would look right on screen and leave the
         * game in a different state.
         */
        for (std::size_t index = 0; index < 6; ++index)
        {
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(qq2 + index)], current.bytes[index],
                           (where + L": the current system's seed byte " + std::to_wstring(index)).c_str());
          Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(qq15 + index)], selected.bytes[index],
                           (where + L": the selected system's seed byte " + std::to_wstring(index)).c_str());
        }

        Assert::AreEqual(cpu.memory[oracle.Label("QQ17")], printer.CaseFlags(),
                         (where + L": the case flags afterwards").c_str());

        if (token == 5 && situation.cash == 1000u && caseFlags == 0)
        {
          sample = Describe(gameCharacters);
        }
        ++compared;
      }
      }
    }

    Logger::WriteMessage(("state control codes: " + std::to_string(compared)
                          + " compared character for character. Code 5 reads:\n  " + sample + "\n")
                           .c_str());
  }
};

} // namespace GameLogicTests
