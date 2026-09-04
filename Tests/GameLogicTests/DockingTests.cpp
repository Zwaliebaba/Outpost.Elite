#include "pch.h"

#include "OracleImage.h"

#include "Commander.h"
#include "Docking.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::DockingOutcome;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * Docking at the station (slice 2e).
 *
 * DOENTRY's dispatch is eight branches over five variables, and the branches overlap: eight of
 * them fall into the same tail, two of them read the SAME byte of TP through different masks, and
 * one compares a single byte of a four-byte number. That is a shape where a port is wrong in one
 * state out of a hundred and passes every test written from the same reading of the source -- so
 * this does not sample it. It runs the shipped routine over 12,800 commanders and compares which
 * of seven labels each one reaches.
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

const char* Name(DockingOutcome _outcome)
{
  switch (_outcome)
  {
    case DockingOutcome::DockingBay: return "DockingBay";
    case DockingOutcome::BriefMission1: return "BriefMission1";
    case DockingOutcome::DebriefMission1: return "DebriefMission1";
    case DockingOutcome::BriefMission2: return "BriefMission2";
    case DockingOutcome::CollectPlans: return "CollectPlans";
    case DockingOutcome::DebriefMission2: return "DebriefMission2";
    case DockingOutcome::OfferTrumbles: return "OfferTrumbles";
  }
  return "?";
}

class RecordingEffects : public Elite::StartUpEffects
{
public:
  void ResetUniverse() override { seams.push_back("RESET"); }
  void ResetShip() override { seams.push_back("RES2"); }
  void ClearKeyLogger() override { seams.push_back("ZEKTRAN"); }
  void StartTheme() override { seams.push_back("startat"); }
  void StopTheme() override { seams.push_back("stopat"); }
  void ResetMissileIndicators() override { seams.push_back("msblob"); }
  void ShowDockingTunnel() override { seams.push_back("LAUN"); }
  void WaitFrames(std::uint8_t _frames) override
  {
    seams.push_back("DELAY");
    frames = _frames;
  }
  std::uint8_t ShowTitleScreen(std::uint8_t, std::uint8_t, std::uint8_t) override
  {
    seams.push_back("TITLE");
    return 0;
  }

  std::vector<std::string> seams;
  std::uint8_t frames = 0;
};
} // namespace

TEST_CLASS(DockingMatchesTheShippedGame)
{
public:
  /*
   * 6502: DOENTRY, over the states that decide a briefing.
   *
   * The sweep is a cross product rather than a list, because the branches are not independent:
   * mission 2's stage is only read in galaxy 2, the coordinates only at one stage each, and the
   * Trumbles tail only when everything above it has declined. Sampling would leave whole
   * combinations unvisited, and those are where a port that read one mask as the other survives.
   */
  TEST_METHOD(EveryCommanderReachesTheSameBriefingAsTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();

    const std::vector<std::pair<std::uint16_t, DockingOutcome>> OUTCOMES = {
      { oracle.Label("BRIEF"), DockingOutcome::BriefMission1 },
      { oracle.Label("DEBRIEF"), DockingOutcome::DebriefMission1 },
      { oracle.Label("BRIEF2"), DockingOutcome::BriefMission2 },
      { oracle.Label("BRIEF3"), DockingOutcome::CollectPlans },
      { oracle.Label("DEBRIEF2"), DockingOutcome::DebriefMission2 },
      { oracle.Label("TBRIEF"), DockingOutcome::OfferTrumbles },
      { oracle.Label("BAY"), DockingOutcome::DockingBay },
    };

    std::map<std::uint16_t, DockingOutcome> outcome;
    for (const auto& entry : OUTCOMES)
    {
      outcome[entry.first] = entry.second;
    }

    const std::uint16_t doentry = oracle.Label("DOENTRY");
    const std::uint16_t tp = oracle.Label("TP");
    const std::uint16_t tally = oracle.Label("TALLY");
    const std::uint16_t gcnt = oracle.Label("GCNT");
    const std::uint16_t cash = oracle.Label("CASH");
    const std::uint16_t qq0 = oracle.Label("QQ0");

    /*
     * The cash values, and they are the point of this list.
     *
     * `LDA CASH+2 / CMP #&C4` reads ONE byte of four. 0x0000C400 is 5017.6 credits and passes;
     * 0x000186A0 is ten thousand credits and FAILS, because its third byte is 0x86. 0x0001C400 is
     * 11,571.2 and passes again. The upstream source's own comments on these three instructions
     * say "5017.6 credits" in one place and "6553.6 credits" in the next; neither is the rule,
     * and these five values are what say so.
     */
    static constexpr std::array<std::uint32_t, 5> CASH_VALUES = {
      0x00000000u, 0x0000C3FFu, 0x0000C400u, 0x000186A0u, 0x0001C400u,
    };

    struct Place
    {
      std::uint8_t x;
      std::uint8_t y;
    };
    static constexpr std::array<Place, 4> PLACES = {
      Place{ 215, 84 }, Place{ 63, 72 }, Place{ 0, 0 }, Place{ 215, 72 },
    };
    static constexpr std::array<std::uint8_t, 5> RANKS = { 0, 1, 4, 5, 255 };

    std::uint32_t compared = 0;
    std::map<std::string, std::uint32_t> reached;

    for (std::uint16_t missions = 0; missions < 32; ++missions)
    {
      for (const std::uint8_t rank : RANKS)
      {
        for (std::uint8_t galaxy = 0; galaxy < 4; ++galaxy)
        {
          for (const Place& place : PLACES)
          {
            for (const std::uint32_t money : CASH_VALUES)
            {
              Elite::CommanderBlock commander = Elite::DefaultCommander();
              commander.At(Elite::Field::MissionProgress) = static_cast<std::uint8_t>(missions);
              commander.bytes[static_cast<std::size_t>(Elite::Field::Kills) + 1u] = rank;
              commander.At(Elite::Field::GalaxyNumber) = galaxy;
              commander.At(Elite::Field::SystemX) = place.x;
              commander.At(Elite::Field::SystemY) = place.y;
              commander.SetCash(money);

              // ---- the shipped routine -------------------------------------------------------
              Cpu6502 cpu = oracle.Fresh();
              cpu.AddTrap(oracle.Label("RES2"));
              cpu.AddTrap(oracle.Label("LAUN"));
              cpu.AddTrap(oracle.Label("DELAY"));

              cpu.memory[tp] = static_cast<std::uint8_t>(missions);
              cpu.memory[static_cast<std::uint16_t>(tally + 1)] = rank;
              cpu.memory[gcnt] = galaxy;
              cpu.memory[qq0] = place.x;
              cpu.memory[static_cast<std::uint16_t>(qq0 + 1)] = place.y;
              for (std::size_t index = 0; index < 4; ++index)
              {
                cpu.memory[static_cast<std::uint16_t>(cash + index)] =
                  commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + index];
              }

              cpu.a = cpu.x = cpu.y = 0;
              cpu.sp = 0xFD;
              cpu.pc = doentry;

              std::uint16_t stoppedAt = 0;
              bool found = false;
              for (int step = 0; step < 20'000; ++step)
              {
                const auto at = outcome.find(cpu.pc);
                if (at != outcome.end())
                {
                  stoppedAt = cpu.pc;
                  found = true;
                  break;
                }
                Assert::IsTrue(cpu.Step(), L"DOENTRY should not reach an unimplemented opcode");
              }

              const std::wstring where =
                Widen("DOENTRY: TP " + std::to_string(missions) + " rank " + std::to_string(rank)
                      + " galaxy " + std::to_string(galaxy) + " at (" + std::to_string(place.x) + ","
                      + std::to_string(place.y) + ") cash " + std::to_string(money));
              Assert::IsTrue(found, (where + L": a known label should be reached").c_str());

              const DockingOutcome expected = outcome[stoppedAt];
              const DockingOutcome got = Elite::MissionOnDocking(commander);
              Assert::AreEqual(static_cast<int>(expected), static_cast<int>(got),
                               (where + L": the game reaches " + Widen(Name(expected)) + L", the port "
                                + Widen(Name(got)))
                                 .c_str());

              ++reached[Name(expected)];
              ++compared;
            }
          }
        }
      }
    }

    // Every branch has to have been taken, or the sweep is comparing against code it never ran.
    for (const auto& entry : OUTCOMES)
    {
      Assert::IsTrue(reached[Name(entry.second)] > 0,
                     (L"the sweep never reached " + Widen(Name(entry.second))).c_str());
    }

    std::string summary = "DOENTRY: " + std::to_string(compared) + " commanders docked;";
    for (const auto& entry : reached)
    {
      summary += " " + entry.first + "=" + std::to_string(entry.second);
    }
    Logger::WriteMessage((summary + "\n").c_str());
  }

  /*
   * 6502: the six stores and the two calls above the dispatch.
   *
   * Compared against the routine run to the point where it branches, because the state it writes
   * is what the flight model then reads -- a port that recharged the shields and forgot the
   * energy banks would pass the dispatch comparison above and leave the player docked with an
   * empty ship.
   */
  TEST_METHOD(ArrivingResetsTheSameStateAsTheShippedRoutine)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t bay = oracle.Label("BAY");

    Cpu6502 cpu = oracle.Fresh();
    cpu.AddTrap(oracle.Label("RES2"));
    cpu.AddTrap(oracle.Label("LAUN"));
    const std::uint16_t delay = oracle.Label("DELAY");
    cpu.AddTrap(delay);

    // A commander that earns nothing, so the run reaches BAY and every store above it has run.
    Elite::CommanderBlock commander = Elite::DefaultCommander();
    commander.At(Elite::Field::MissionProgress) = 0x02;
    commander.At(Elite::Field::GalaxyNumber) = 7;
    commander.SetCash(0);
    cpu.memory[oracle.Label("TP")] = 0x02;
    cpu.memory[oracle.Label("GCNT")] = 7;

    // Values the routine has to overwrite, chosen so a store that did not happen is visible.
    for (const char* name : { "DELTA", "GNTMP", "FSH", "ASH", "ENERGY" })
    {
      cpu.memory[oracle.Label(name)] = 0x5C;
    }
    cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ22") + 1)] = 0x5C;

    cpu.a = cpu.x = cpu.y = 0;
    cpu.sp = 0xFD;
    cpu.pc = oracle.Label("DOENTRY");

    bool reachedBay = false;
    for (int step = 0; step < 20'000; ++step)
    {
      if (cpu.pc == bay)
      {
        reachedBay = true;
        break;
      }
      Assert::IsTrue(cpu.Step(), L"DOENTRY should not reach an unimplemented opcode");
    }
    Assert::IsTrue(reachedBay, L"DOENTRY should run off its dispatch into BAY");

    // ---- the port ----------------------------------------------------------------------------
    RecordingEffects effects;
    Elite::DockedShip ship;
    ship.speed = 0x5C;
    ship.laserTemperature = 0x5C;
    ship.hyperspaceCountdown = 0x5C;
    ship.forwardShield = 0x5C;
    ship.aftShield = 0x5C;
    ship.energy = 0x5C;
    std::uint8_t dockedFlag = 0;

    const Elite::DockingResult result = Elite::DockAtStation(effects, commander, ship, dockedFlag, 0, false);

    Assert::AreEqual(static_cast<int>(DockingOutcome::DockingBay), static_cast<int>(result.outcome),
                     L"this commander earns no briefing");

    // 6502: JSR RES2 / JSR LAUN / ... / LDY #44 / JSR DELAY, in that order.
    const std::vector<std::string> EXPECTED = { "RES2", "LAUN", "DELAY" };
    Assert::AreEqual(EXPECTED.size(), effects.seams.size(), L"how many seams arriving reaches");
    for (std::size_t index = 0; index < EXPECTED.size(); ++index)
    {
      Assert::AreEqual(EXPECTED[index], effects.seams[index],
                       (L"seam " + std::to_wstring(index)).c_str());
    }

    std::uint8_t frames = 0;
    for (const Cpu6502::TrapHit& hit : cpu.trapHits)
    {
      if (hit.address == delay)
      {
        frames = hit.y; // 6502: LDY #44, the count is in Y
      }
    }
    Assert::AreEqual(frames, effects.frames, L"how long the pause is");
    Assert::AreEqual<std::uint8_t>(Elite::DOCKING_PAUSE_FRAMES, frames, L"forty-four vertical syncs");

    Assert::AreEqual(cpu.memory[oracle.Label("DELTA")], ship.speed, L"DELTA");
    Assert::AreEqual(cpu.memory[oracle.Label("GNTMP")], ship.laserTemperature, L"GNTMP");
    Assert::AreEqual(cpu.memory[static_cast<std::uint16_t>(oracle.Label("QQ22") + 1)],
                     ship.hyperspaceCountdown, L"QQ22+1");
    Assert::AreEqual(cpu.memory[oracle.Label("FSH")], ship.forwardShield, L"FSH");
    Assert::AreEqual(cpu.memory[oracle.Label("ASH")], ship.aftShield, L"ASH");
    Assert::AreEqual(cpu.memory[oracle.Label("ENERGY")], ship.energy, L"ENERGY");

    // 6502: BAY's own stores, which the JMP tail reaches.
    Assert::AreEqual<std::uint8_t>(0xFF, dockedFlag, L"BAY sets the docked flag");
    Assert::AreEqual(static_cast<int>(Elite::KeyAction::StatusMode),
                     static_cast<int>(result.bay.outcome.action), L"BAY forces the status key");

    /*
     * And a commander who HAS earned a briefing does not reach BAY from here.
     *
     * Every briefing ends `JMP BRP`, which is `JSR DETOK` and then `JMP BAY` -- so the docking bay
     * is where they all end up, but through the briefing rather than from DOENTRY. The
     * distinction is the whole meaning of the enum: it names the label DOENTRY jumps to, and a
     * caller that has been handed a briefing owes the player the briefing FIRST. A port that ran
     * BAY's stores on every path would set the docked flag before the mission screen had drawn.
     */
    RecordingEffects briefed;
    Elite::CommanderBlock earner = Elite::DefaultCommander();
    earner.At(Elite::Field::MissionProgress) = 0x00;
    earner.bytes[static_cast<std::size_t>(Elite::Field::Kills) + 1u] = 4;
    earner.At(Elite::Field::GalaxyNumber) = 0;
    earner.SetCash(0);

    Elite::DockedShip earnerShip;
    std::uint8_t earnerDocked = 0;
    const Elite::DockingResult briefing =
      Elite::DockAtStation(briefed, earner, earnerShip, earnerDocked, 0, false);

    Assert::AreEqual(static_cast<int>(DockingOutcome::BriefMission1), static_cast<int>(briefing.outcome),
                     L"this commander has earned the Constrictor mission");
    Assert::AreEqual<std::uint8_t>(0, earnerDocked, L"a briefing does not set the docked flag");
    Assert::AreEqual(static_cast<int>(Elite::KeyAction::Nothing),
                     static_cast<int>(briefing.outcome == DockingOutcome::DockingBay
                                        ? briefing.bay.outcome.action
                                        : Elite::KeyAction::Nothing),
                     L"a briefing does not force a key");
    Assert::AreEqual(static_cast<int>(Elite::KeyAction::Nothing),
                     static_cast<int>(briefing.bay.outcome.action),
                     L"the bay result is left untouched on a briefing path");

    // The state above the dispatch is reset either way -- the shields and the pause are not the
    // mission's business.
    Assert::AreEqual<std::uint8_t>(0xFF, earnerShip.energy, L"the energy banks are recharged anyway");
    Assert::AreEqual<std::uint8_t>(Elite::DOCKING_PAUSE_FRAMES, briefed.frames,
                                   L"and the pause happens anyway");
  }
  /*
   * 6502: LDA CASH+2 / CMP #&C4 -- one byte of four, stated as its own comparison.
   *
   * The sweep above already covers this, but it covers it among twelve thousand other things. It
   * is here on its own because it is the finding, and because the obvious "improvement" -- compare
   * the whole four-byte value against 50,176 tenths, as both of the upstream comments describe it
   * -- would pass every other test in this file. This one fails.
   *
   * The rule is `(tenths >> 8) & 255 >= 196`: a band 1,536 credits wide that recurs every 6,553.6.
   */
  TEST_METHOD(TheTrumblesCashTestIsABandRatherThanAThreshold)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const std::uint16_t cash = oracle.Label("CASH");
    const std::uint16_t tbrief = oracle.Label("TBRIEF");
    const std::uint16_t bay = oracle.Label("BAY");

    struct Case
    {
      const char* what;
      std::uint32_t tenths;
      bool offered;
    };

    const std::vector<Case> CASES = {
      { "nothing", 0u, false },
      { "one tenth below the band", 0x0000C3FFu, false },
      { "5017.6 credits, the bottom of the band", 0x0000C400u, true },
      { "6553.5 credits, the top of it", 0x0000FFFFu, true },
      { "6553.6 credits, one tenth past the top", 0x00010000u, false },
      { "ten thousand credits, and richer than the band", 0x000186A0u, false },
      { "11,571.2 credits, back inside the next band", 0x0001C400u, true },
      { "a hundred thousand credits", 0x000F4240u, false },
      { "and one that is rich enough again", 0x000FC400u, true },
    };

    for (const Case& item : CASES)
    {
      const std::wstring where = Widen(std::string("Trumbles: ") + item.what);

      // A commander with mission 1 finished and paid, in a galaxy where mission 2 cannot start,
      // so the only decision left is this one.
      Elite::CommanderBlock commander = Elite::DefaultCommander();
      commander.At(Elite::Field::MissionProgress) = 0x02;
      commander.At(Elite::Field::GalaxyNumber) = 7;
      commander.SetCash(item.tenths);

      Cpu6502 cpu = oracle.Fresh();
      cpu.AddTrap(oracle.Label("RES2"));
      cpu.AddTrap(oracle.Label("LAUN"));
      cpu.AddTrap(oracle.Label("DELAY"));
      cpu.memory[oracle.Label("TP")] = 0x02;
      cpu.memory[oracle.Label("GCNT")] = 7;
      for (std::size_t index = 0; index < 4; ++index)
      {
        cpu.memory[static_cast<std::uint16_t>(cash + index)] =
          commander.bytes[static_cast<std::size_t>(Elite::Field::Cash) + index];
      }
      cpu.a = cpu.x = cpu.y = 0;
      cpu.sp = 0xFD;
      cpu.pc = oracle.Label("DOENTRY");

      bool offered = false;
      bool settled = false;
      for (int step = 0; step < 20'000; ++step)
      {
        if (cpu.pc == tbrief)
        {
          offered = true;
          settled = true;
          break;
        }
        if (cpu.pc == bay)
        {
          settled = true;
          break;
        }
        Assert::IsTrue(cpu.Step(), (where + L": no unimplemented opcode").c_str());
      }
      Assert::IsTrue(settled, (where + L": the routine should settle on one of the two").c_str());

      Assert::AreEqual(item.offered, offered, (where + L": what the shipped routine decides").c_str());
      Assert::AreEqual(item.offered,
                       Elite::MissionOnDocking(commander) == DockingOutcome::OfferTrumbles,
                       (where + L": what the port decides").c_str());
    }

    Logger::WriteMessage(("Trumbles: " + std::to_string(CASES.size())
                          + " cash amounts, and being richer is not being more eligible\n")
                           .c_str());
  }
};

} // namespace GameLogicTests
