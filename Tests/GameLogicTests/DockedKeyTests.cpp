#include "pch.h"

#include "OracleImage.h"

#include "DockedKeys.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::KeyAction;
using Elite::KeyOutcome;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * The top-level keyboard dispatch, against the game (slice 2e).
 *
 * TT102 is a chain of comparisons, and a chain of comparisons is exactly the shape of code that a
 * port gets almost right: one key in the wrong branch of the docked test, or one comparison in the
 * wrong order, and a screen becomes unreachable in a state nobody thinks to try. So this does not
 * sample. It runs the shipped routine for EVERY key code in both states, on every view, with and
 * without the counter running and with and without the hyperspace key held, and compares which
 * label it reaches -- 8,192 dispatches.
 *
 * Nothing is trapped. The routine is stepped until it reaches one of the addresses that ARE the
 * answer, which is what lets a label inside TT102 (`t95`, `TT107`, `T95`) be an outcome alongside
 * a routine it jumps to. The distinction matters: `JSR TT16` falls into `TT107` and `JSR TT103`
 * does not, and a comparison that only watched subroutine entries could not tell those apart.
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

    const char* Name(KeyAction _action)
    {
      switch (_action)
      {
      case KeyAction::Nothing:
        return "Nothing";
      case KeyAction::StatusMode:
        return "StatusMode";
      case KeyAction::LongRangeChart:
        return "LongRangeChart";
      case KeyAction::ShortRangeChart:
        return "ShortRangeChart";
      case KeyAction::DataOnSystem:
        return "DataOnSystem";
      case KeyAction::Inventory:
        return "Inventory";
      case KeyAction::MarketPrice:
        return "MarketPrice";
      case KeyAction::Launch:
        return "Launch";
      case KeyAction::EquipShip:
        return "EquipShip";
      case KeyAction::BuyCargo:
        return "BuyCargo";
      case KeyAction::DiskAccess:
        return "DiskAccess";
      case KeyAction::SellCargo:
        return "SellCargo";
      case KeyAction::ChangeView:
        return "ChangeView";
      case KeyAction::Hyperspace:
        return "Hyperspace";
      case KeyAction::ShowDistance:
        return "ShowDistance";
      case KeyAction::SearchBySystemName:
        return "SearchBySystemName";
      case KeyAction::HomeCrosshairs:
        return "HomeCrosshairs";
      case KeyAction::MoveCrosshairs:
        return "MoveCrosshairs";
      case KeyAction::CountdownOnly:
        return "CountdownOnly";
      }
      return "?";
    }
  } // namespace

  TEST_CLASS(TheTopLevelKeyDispatchMatchesTheShippedGame)
  {
  public:
    TEST_METHOD(EveryKeyReachesTheSameLabelAsTheShippedRoutine)
    {
      if (OracleMissing())
      {
        return;
      }

      const OracleImage& oracle = OracleImage::Instance();

      /*
       * The addresses that ARE the answer, and the action each one means.
       *
       * `t95` is an RTS inside TT102 and `T95` is the routine three bytes past it, so the two differ
       * by one character and by everything else; both are here, which is the only way to tell "the
       * key did nothing" from "the key printed the distance".
       */
      const std::vector<std::pair<std::uint16_t, KeyAction>> OUTCOMES = {
        {oracle.Label("STATUS"), KeyAction::StatusMode},
        {oracle.Label("TT22"), KeyAction::LongRangeChart},
        {oracle.Label("TT23"), KeyAction::ShortRangeChart},
        {oracle.Label("TT111"), KeyAction::DataOnSystem},
        {oracle.Label("TT213"), KeyAction::Inventory},
        {oracle.Label("TT167"), KeyAction::MarketPrice},
        {oracle.Label("TT110"), KeyAction::Launch},
        {oracle.Label("EQSHP"), KeyAction::EquipShip},
        {oracle.Label("TT219"), KeyAction::BuyCargo},
        {oracle.Label("SVE"), KeyAction::DiskAccess},
        {oracle.Label("TT208"), KeyAction::SellCargo},
        {oracle.Label("LOOK1"), KeyAction::ChangeView},
        {oracle.Label("hyp"), KeyAction::Hyperspace},
        {oracle.Label("T95"), KeyAction::ShowDistance},
        {oracle.Label("HME2"), KeyAction::SearchBySystemName},
        {oracle.Label("TT103"), KeyAction::HomeCrosshairs},
        {oracle.Label("TT16"), KeyAction::MoveCrosshairs},
        {oracle.Label("TT107"), KeyAction::CountdownOnly},
        {oracle.Label("t95"), KeyAction::Nothing},
      };

      std::map<std::uint16_t, KeyAction> outcome;
      for (const auto& entry : OUTCOMES)
      {
        outcome[entry.first] = entry.second;
      }

      const std::uint16_t tt102 = oracle.Label("TT102");
      const std::uint16_t qq11 = oracle.Label("QQ11");
      const std::uint16_t qq12 = oracle.Label("QQ12");
      const std::uint16_t qq22 = oracle.Label("QQ22");
      const std::uint16_t look1 = oracle.Label("LOOK1");

      // 6502: KLO+HINT -- the keyboard MATRIX byte for "H", which LABEL_3 reads with BIT.
      constexpr std::uint8_t HINT = 0x23;
      const std::uint16_t hKey = static_cast<std::uint16_t>(oracle.Label("KLO") + HINT);

      // Views: no chart, the long-range chart, the short-range chart, and a value with both bits
      // set -- because the test the routine makes is `AND #%11000000`, not a comparison.
      static constexpr std::array<std::uint8_t, 4> VIEWS = {0x00, 0x40, 0x80, 0xC0};

      std::uint32_t compared = 0;
      std::map<std::string, std::uint32_t> reached;

      /*
       * QQ12, and the two values in the middle are the point.
       *
       * The routine tests this byte TWO WAYS: `BIT QQ12 / BPL` for the docked/flight split, which
       * reads bit 7, and `LDA QQ12 / BEQ` for the system search, which asks whether it is zero. The
       * game only ever writes &FF and 0, so those two agree for every value it produces -- and a
       * sweep of only those two cannot tell the tests apart. A port using bit 7 in both places
       * passed until 0x01 and 0x40 were added here.
       */
      for (const std::uint8_t docked : {std::uint8_t{0xFF}, std::uint8_t{0x00}, std::uint8_t{0x01}, std::uint8_t{0x40}})
      {
        for (const std::uint8_t view : VIEWS)
        {
          for (const std::uint8_t countdown : {std::uint8_t{0}, std::uint8_t{3}})
          {
            for (const bool held : {false, true})
            {
              for (std::uint16_t key = 0; key < 256; ++key)
              {
                Cpu6502 cpu = oracle.Fresh();
                cpu.memory[qq11] = view;
                cpu.memory[qq12] = docked;
                cpu.memory[static_cast<std::uint16_t>(qq22 + 1)] = countdown;
                cpu.memory[hKey] = held ? 0xFF : 0x00;

                cpu.a = static_cast<std::uint8_t>(key);
                cpu.x = 0;
                cpu.y = 0;
                cpu.sp = 0xFD;
                cpu.pc = tt102;

                std::uint16_t stoppedAt = 0;
                std::uint8_t viewNumber = 0;
                bool found = false;
                for (int step = 0; step < 20'000; ++step)
                {
                  const auto at = outcome.find(cpu.pc);
                  if (at != outcome.end())
                  {
                    stoppedAt = cpu.pc;
                    viewNumber = cpu.x;
                    found = true;
                    break;
                  }
                  Assert::IsTrue(cpu.Step(), L"TT102 should not reach an unimplemented opcode");
                }

                const std::wstring where = Widen("TT102: key " + std::to_string(key) + (docked != 0 ? " docked" : " in flight") + " view " +
                                                 std::to_string(view) + " counter " + std::to_string(countdown) + (held ? " H held" : ""));
                Assert::IsTrue(found, (where + L": the dispatch should reach a known label").c_str());

                const KeyAction expected = outcome[stoppedAt];
                const KeyOutcome got = Elite::ActionForKey(static_cast<std::uint8_t>(key), docked, view, countdown, held);

                Assert::AreEqual(
                  static_cast<int>(expected), static_cast<int>(got.action),
                  (where + L": the game reaches " + Widen(Name(expected)) + L", the port says " + Widen(Name(got.action))).c_str());

                // 6502: LDX #1 / #2 / #3 -- the view number, which only exists on this one path.
                if (stoppedAt == look1)
                {
                  Assert::AreEqual(viewNumber, got.view, (where + L": the view number").c_str());
                }
                else
                {
                  Assert::AreEqual<std::uint8_t>(0, got.view, (where + L": no view number is expected").c_str());
                }

                ++reached[Name(expected)];
                ++compared;
              }
            }
          }
        }
      }

      /*
       * Every action has to have happened. A sweep that never reached one of these would be
       * comparing the port against a routine it had not exercised, and would pass with that branch
       * missing entirely.
       */
      for (const auto& entry : OUTCOMES)
      {
        Assert::IsTrue(reached[Name(entry.second)] > 0, (L"the sweep never reached " + Widen(Name(entry.second))).c_str());
      }

      std::string summary = "TT102: " + std::to_string(compared) + " dispatches compared;";
      for (const auto& entry : reached)
      {
        summary += " " + entry.first + "=" + std::to_string(entry.second);
      }
      Logger::WriteMessage((summary + "\n").c_str());
    }
  };

} // namespace GameLogicTests
