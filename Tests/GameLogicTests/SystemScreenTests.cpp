#include "pch.h"

#include "OracleImage.h"

#include "Commander.h"
#include "ExtendedTokens.h"
#include "Rng.h"
#include "StateTokens.h"
#include "SystemScreen.h"
#include "TextPrint.h"
#include "Tokens.h"
#include "Universe.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::SystemData;
using Elite::SystemSeeds;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The Data on System screen, against the game that draws it (slice 2a).
 *
 * 2a's row deferred this as "cursor and canvas work" and it is neither. Every line of TT25 is a
 * token, a number or a seed bit; the only thing it reaches outside GameLogic for is TRADEMODE,
 * which slice 2c already made a seam and four screens already use. That is plan section 6.12's
 * pattern for the fifth time, and this test is the demonstration: all 2,048 systems in all eight
 * galaxies, compared character for character with the cursor stamped on every character.
 *
 * The comparison runs the WHOLE screen, PDESC included -- so a description that came out one
 * random alternative adrift, or a line that wrapped a column early, fails here.
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

    struct StampedSink : public Elite::TextSink
    {
      void Put(std::uint8_t _character) override
      {
        const std::uint32_t column = (cursor != nullptr) ? cursor->column : 0u;
        const std::uint32_t row = (cursor != nullptr) ? cursor->row : 0u;
        stamped.push_back(static_cast<std::uint32_t>(_character) | (column << 8) | (row << 16));
      }

      Elite::TextState* cursor = nullptr;
      std::vector<std::uint32_t> stamped;
    };

    std::wstring FirstDifference(const std::vector<std::uint32_t>& _game, const std::vector<std::uint32_t>& _port)
    {
      std::size_t at = 0;
      while (at < _game.size() && at < _port.size() && _game[at] == _port[at])
      {
        ++at;
      }

      const auto window = [](const std::vector<std::uint32_t>& _seq, std::size_t _from)
      {
        std::wstring text = L"[";
        for (std::size_t index = _from; index < _seq.size() && index < _from + 16; ++index)
        {
          const std::uint8_t character = static_cast<std::uint8_t>(_seq[index]);
          text += (character >= 32 && character < 127) ? static_cast<wchar_t>(character) : L'.';
          text += L'@';
          text += std::to_wstring((_seq[index] >> 8) & 0xFFu);
          text += L',';
          text += std::to_wstring((_seq[index] >> 16) & 0xFFu);
          text += L' ';
        }
        return text + L"]";
      };

      return L"first difference at " + std::to_wstring(at) + L" of " + std::to_wstring(_game.size()) + L"/" +
             std::to_wstring(_port.size()) + L"; game " + window(_game, at) + L", port " + window(_port, at);
    }

    class RecordingEffects : public Elite::TradeScreenEffects
    {
    public:
      void SetUpTradeScreen(std::uint8_t _view) override
      {
        log.push_back(static_cast<std::uint32_t>(0x100u + _view));
      }
      void ClearBottomRows() override
      {
        log.push_back(0x200u);
      }
      void BeepAndPause() override
      {
        log.push_back(0x300u);
      }
      void ClearToView(std::uint8_t _view) override
      {
        log.push_back(static_cast<std::uint32_t>(0x400u + _view));
      }
      void ResetMissileIndicators() override
      {
        log.push_back(0x500u);
      }

      std::vector<std::uint32_t> log;
    };

    class NoKeys : public Elite::KeySource
    {
    public:
      std::uint8_t NextKey() override
      {
        asked = true;
        return 13;
      }
      bool asked = false;
    };
  } // namespace

  TEST_CLASS(TheDataOnSystemScreenMatchesTheShippedGame)
  {
  public:
    /*
     * 6502: TT25, whole screen, for every system in the game.
     *
     * The distance is varied across the sweep rather than fixed, because TT146's zero case takes a
     * different exit -- `JMP INCYC`, which moves the cursor without a newline -- and a screen that
     * always had a distance would never reach it.
     */
    TEST_METHOD(EverySystemsDataScreenMatchesTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();
      const std::uint16_t chpr = oracle.Label("CHPR");
      const std::uint16_t trademode = oracle.Label("TRADEMODE");
      const std::uint16_t qq15 = oracle.Label("QQ15");
      const std::uint16_t qq3 = oracle.Label("QQ3");
      const std::uint16_t qq8 = oracle.Label("QQ8");

      // Three distances, chosen for the branch rather than the value: none at all, one that prints
      // a single digit before the point, and one that fills the field.
      static constexpr std::array<std::uint16_t, 3> DISTANCES = {0, 47, 9999};

      SystemSeeds galaxy = Elite::GALAXY_ONE_SEEDS;
      std::uint32_t compared = 0;

      for (std::uint8_t galaxyNumber = 0; galaxyNumber < 8; ++galaxyNumber)
      {
        SystemSeeds seeds = galaxy;

        for (std::uint16_t system = 0; system < 256; ++system)
        {
          const std::uint16_t distance = DISTANCES[(system + galaxyNumber) % DISTANCES.size()];
          const SystemData data = Elite::GenerateSystemData(seeds);
          const std::wstring where = Widen("TT25: galaxy " + std::to_string(galaxyNumber + 1) + " system " + std::to_string(system) +
                                           " distance " + std::to_string(distance));

          // ---- the shipped routine -----------------------------------------------------------
          Cpu6502 cpu = oracle.Fresh();
          cpu.AddTrap(chpr, Cpu6502::TrapExit::ClearCarry);
          cpu.AddTrap(trademode);
          cpu.AddTrap(oracle.Label("NLIN"));
          cpu.watch = {oracle.Label("XC"), oracle.Label("YC"), 0, 0};

          for (std::size_t index = 0; index < seeds.bytes.size(); ++index)
          {
            cpu.memory[static_cast<std::uint16_t>(qq15 + index)] = seeds.bytes[index];
          }
          cpu.memory[qq3] = data.economy;
          cpu.memory[oracle.Label("QQ4")] = data.government;
          cpu.memory[oracle.Label("QQ5")] = data.techLevel;
          cpu.memory[oracle.Label("QQ6")] = data.population;
          cpu.memory[oracle.Label("QQ7")] = static_cast<std::uint8_t>(data.productivity & 0xFFu);
          cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ7") + 1)] = static_cast<std::uint8_t>(data.productivity >> 8);
          cpu.memory[qq8] = static_cast<std::uint8_t>(distance & 0xFFu);
          cpu.memory[static_cast<std::uint16_t>(qq8 + 1)] = static_cast<std::uint8_t>(distance >> 8);
          cpu.memory[oracle.Label("GCNT")] = galaxyNumber;

          cpu.memory[oracle.Label("QQ17")] = 0;
          cpu.memory[oracle.Label("XC")] = 1;
          cpu.memory[oracle.Label("YC")] = 1;
          cpu.memory[oracle.Label("DTW1")] = 0;
          cpu.memory[oracle.Label("DTW2")] = 0xFF;
          cpu.memory[oracle.Label("DTW3")] = 0;
          cpu.memory[oracle.Label("DTW4")] = 0;
          cpu.memory[oracle.Label("DTW5")] = 0;
          cpu.memory[oracle.Label("DTW6")] = 0;
          cpu.memory[oracle.Label("DTW8")] = 0xFF;
          for (std::uint16_t index = 0; index < Elite::CharacterPrinter::BUFFER_SIZE; ++index)
          {
            cpu.memory[static_cast<std::uint16_t>(oracle.Label("BUF") + index)] = 0;
          }

          cpu.a = cpu.x = cpu.y = 0;
          cpu.sp = 0xFD;
          const auto run = cpu.CallSubroutine(oracle.Label("TT25"), 4'000'000);
          Assert::IsTrue(run.completed && !run.illegalOpcode, (where + L": TT25 should return").c_str());

          std::vector<std::uint32_t> expected;
          for (const Cpu6502::TrapHit& hit : cpu.trapHits)
          {
            if (hit.address == chpr)
            {
              expected.push_back(static_cast<std::uint32_t>(hit.a) | (static_cast<std::uint32_t>(hit.watched[0]) << 8) |
                                 (static_cast<std::uint32_t>(hit.watched[1]) << 16));
            }
          }

          // ---- the port ------------------------------------------------------------------------
          StampedSink sink;
          Elite::TextState text;
          text.column = 1;
          text.row = 1;
          text.caseFlags = 0;
          sink.cursor = &text;
          Elite::CharacterPrinter characters(sink);
          characters.state.sentenceStart = 0xFF;
          Elite::TokenPrinter printer(characters);
          printer.SetCaseFlags(0);
          printer.SetCursor(&text);

          Elite::CommanderBlock commander = Elite::DefaultCommander();
          commander.At(Elite::Field::GalaxyNumber) = galaxyNumber;
          const std::array<std::uint8_t, Elite::COMMANDER_NAME_SIZE> name = Elite::DefaultCommanderName();
          SystemSeeds current = seeds;
          SystemSeeds selected = seeds;
          Elite::StateTokens values(printer, text, commander, std::span<const std::uint8_t, Elite::COMMANDER_NAME_SIZE>(name), current,
                                    selected, false);
          printer.SetValueTokens(&values);

          NoKeys keys;
          RecordingEffects effects;
          Elite::Rng rng;
          Elite::ExtendedTokenPrinter extended(characters, printer, rng);
          Elite::TradeScreen screen{printer, characters, extended, text, keys, effects, rng};

          Elite::SystemDataScreen(screen, selected, data, distance);

          if (sink.stamped != expected)
          {
            Assert::Fail((where + L" differs, " + FirstDifference(expected, sink.stamped)).c_str());
          }

          Assert::IsFalse(keys.asked, (where + L": TT25 reads no keys").c_str());

          // 6502: QQ17 -- the case state, which the "M" of "M CR" clears and nothing puts back.
          Assert::AreEqual(cpu.memory[oracle.Label("QQ17")], printer.CaseFlags(), (where + L": the case flags at the end").c_str());

          // 6502: QQ15 -- cpl saves and restores it, and PDESC reads it afterwards, so a screen
          // that left it twisted would give the next caller a different system.
          for (std::size_t index = 0; index < seeds.bytes.size(); ++index)
          {
            Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(qq15 + index)], selected.bytes[index],
                             (where + L": seed byte " + std::to_wstring(index) + L" on exit").c_str());
          }

          // The one seam it reaches, and the view number it reaches it with.
          Assert::AreEqual<std::size_t>(1, effects.log.size(), (where + L": how many seams").c_str());
          Assert::AreEqual<std::uint32_t>(0x100u + Elite::DATA_ON_SYSTEM_VIEW, effects.log[0],
                                          (where + L": TRADEMODE's view number").c_str());

          ++compared;
          Elite::NextSystem(seeds);
        }

        Elite::NextGalaxy(galaxy);
      }

      Logger::WriteMessage(("TT25: " + std::to_string(compared) +
                            " system data screens compared character for character, with the cursor "
                            "stamped\n")
                             .c_str());
    }
  };

} // namespace GameLogicTests
