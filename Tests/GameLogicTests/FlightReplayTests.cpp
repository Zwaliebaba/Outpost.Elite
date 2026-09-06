#include "pch.h"

#include "FlightPort.h"
#include "FlightUniverse.h"
#include "UniverseImage.h"

#include "Commander.h"
#include "DockedKeys.h"
#include "Flight.h"
#include "Galaxy.h"
#include "ShipSlot.h"
#include "Spawn.h"
#include "Tactics.h"

#include <cstdint>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The flight replay (Design/Modernize.md slice M0-c).
 *
 * One scripted flight -- launch from Lave, coast, accelerate, fight a hostile Viper, engage the
 * docking computer and let it fly you in -- through the null port, with the replay digest taken
 * at every hundredth step and at every turn of the script. The digests are RECORDED below, and
 * the suite fails when a step's digest is not the one recorded.
 *
 * WHAT THIS PINS THAT NOTHING ELSE DOES. Every routine in the flight is compared against the
 * shipped game somewhere in this suite, one call at a time. A refactor that keeps every one of
 * those green can still change how they compose: the order two side effects happen in, a value
 * one routine leaves for the next (`XX0`, `K3+1`), a seam answered with a different argument.
 * The replay sees the composition, and it sees it without the oracle -- which is what lets it
 * outlive the oracle (Modernize.md M6) and what the modernisation's every slice is measured by
 * (Risk R14).
 *
 * WHEN THE RECORD MAY CHANGE. Never for a refactor: a changed digest is a changed game, and the
 * slice that changed it has found a defect or introduced one. The record is re-taken in two cases
 * and the journal entry says which: the DIGEST is deliberately widened -- more cells in the image,
 * the sound buffer at M3-a -- or a defect in the port is found and fixed, and the record follows
 * the fix ("the port was wrong, the record is not needed", Modernize.md section 1 R-e). The failure
 * message prints the whole new record in the form below, so re-taking it is a paste and a diff,
 * never a retype.
 */
namespace GameLogicTests
{

  namespace
  {

    struct Checkpoint
    {
      std::uint32_t step;
      std::uint64_t digest;
    };

    /// Digests are taken this often, and at every turn of the script besides.
    constexpr std::uint32_t CHECKPOINT_EVERY = 100;

    /// How long the autopilot is given. `DOCKIT` takes the ship in well inside this in the script
    /// as recorded; the cap exists so that a broken autopilot fails rather than runs for ever.
    constexpr std::uint32_t AUTOPILOT_LIMIT = 4000;

    /// The record. Empty is a failure, so a tree can never carry an unpinned replay.
    /*
     * RE-TAKEN TWICE, both under Modernize.md rule 1's second case (the port was wrong).
     *
     * 2026-09-06, M2-c-1: every digest moved and not one step did. What changed was the SHAPE of
     * the image the digest is taken over -- `UniverseImage` hashed a `T2` cell holding a byte the
     * game does not write, because `HLOIN` and `BOX2` park their scratch in `T` and the port had
     * followed the BBC commentary's `T2`/`R2` naming. Both are locals now and the cell is gone.
     *
     * 2026-09-06, R22's ruling: SEVEN of the sixteen moved, and again not one step did. `MA23`
     * reaches `SBC #36` with the carry CLEAR, because `MAS3` returns the flag its last `ADC` left
     * and the only path that sets it is the saturation the `BCS MA23` above has already sent away.
     * So the planet's radius costs THIRTY-SEVEN and the port had been subtracting thirty-six --
     * an altitude one unit too generous, and a death radius one unit too small, on the seven
     * checkpoints where a planet was close enough for the check to run at all. Nothing measured it
     * until the fixture that closed R22 put a planet in range (§8).
     */
    constexpr Checkpoint RECORDED[] = {
      {0, 0x793d19aaf960deaaull},    // launched from Lave
      {40, 0x7c561464552416eaull},   // coasted
      {100, 0x8baad0a1c0327247ull},  // at full speed
      {200, 0xde794078d3bcc625ull},
      {300, 0x24449bcf0ec9102bull},
      {340, 0xf02931e617d3b123ull},  // the Viper fought
      {342, 0xa1eaa872c5890536ull},  // the docking computer engaged
      {400, 0x773168b7e3089bbcull},
      {500, 0x8a8e79201b21842dull},
      {600, 0x1898d3e20b42ab22ull},
      {700, 0x62b7db8c2dde9aa4ull},
      {800, 0x11ed5553a41da45full},
      {900, 0x4aba4549a9d9dfe9ull},
      {1000, 0x72d94ce3ca9f3d4dull},
      {1100, 0x3ef58b79f6b0084eull},
      {1170, 0xe9a05a7fe9c76544ull}, // docked
    };
    constexpr std::uint32_t RECORDED_STEPS = 1170;
    constexpr Elite::LoopOutcome RECORDED_OUTCOME = Elite::LoopOutcome::Docked;

    struct Trace
    {
      std::vector<Checkpoint> checkpoints;
      std::uint32_t steps = 0;
      Elite::LoopOutcome outcome = Elite::LoopOutcome::Continued;
      bool viperCreated = false;
    };

    /// 6502: what the cold start leaves before a launch -- the default commander with a docking
    /// computer bought for the last phase, the home system's data, and `RESET`.
    void Prepare(FlightPort& _port)
    {
      _port.universe.commander = Elite::DefaultCommander();
      _port.universe.commander.dockingComputer = 0xFFu; // 6502: DKCMP -- so phase four has one to engage

      const Elite::Commander& commander = _port.universe.commander;
      const std::uint8_t homeX = commander.systemX;
      const std::uint8_t homeY = commander.systemY;
      const Elite::NearestSystem home = Elite::FindNearestSystem(commander.galaxySeeds, homeX, homeY, homeX, homeY);

      _port.universe.current.seeds = home.seeds;
      _port.universe.current.economy = home.data.economy;
      _port.universe.current.government = home.data.government;
      _port.universe.current.techLevel = home.data.techLevel;
      _port.universe.view = 1u; // a docked screen, which the launch replaces with the space view

      Elite::ResetGame(_port.universe, _port.ports, _port.docked); // 6502: RESET
    }

    using Perturbation = std::function<void(FlightPort&)>;

    /// The script. `_perturb`, when given, runs once after the launch and before the first frame.
    Trace Fly(FlightPort& _port, const Perturbation& _perturb = {})
    {
      Trace trace;
      std::uint32_t step = 0;
      auto checkpoint = [&]() { trace.checkpoints.push_back(Checkpoint{step, _port.Digest()}); };

      Prepare(_port);
      Elite::SystemSeeds selected{};
      Elite::Launch(_port.universe, _port.ports, nullptr, _port.docked, _port.universe.commander.systemX,
                    _port.universe.commander.systemY, selected); // 6502: TT110
      checkpoint();

      if (_perturb)
      {
        _perturb(_port);
      }

      // Frames with the keys `_keysFor` holds; false once the flight has left the loop.
      auto frames = [&](std::uint32_t _count, const std::function<void(std::uint32_t)>& _keysFor)
      {
        for (std::uint32_t frame = 0; frame < _count; ++frame)
        {
          _port.held.fill(0u);
          _keysFor(frame);
          const Elite::LoopOutcome outcome = _port.Step();
          ++step;
          if (step % CHECKPOINT_EVERY == 0u)
          {
            checkpoint();
          }
          if (outcome != Elite::LoopOutcome::Continued)
          {
            trace.outcome = outcome;
            if (step % CHECKPOINT_EVERY != 0u)
            {
              checkpoint();
            }
            return false;
          }
        }
        if (step % CHECKPOINT_EVERY != 0u)
        {
          checkpoint(); // the turn of the script, unless a hundredth step just took one
        }
        return true;
      };

      // Phase one: coast away from the station.
      if (!frames(40, [](std::uint32_t) {}))
      {
        trace.steps = step;
        return trace;
      }

      // Phase two: full speed, with a roll for the first twenty frames.
      if (!frames(60,
                  [&](std::uint32_t _frame)
                  {
                    _port.held[Elite::KEY_SPEED_UP] = 1u;
                    if (_frame < 20u)
                    {
                      _port.held[Elite::KEY_ROLL_RIGHT] = 1u;
                    }
                  }))
      {
        trace.steps = step;
        return trace;
      }

      // Phase three: a hostile Viper straight ahead, and the laser at it.
      {
        Universe& universe = _port.universe;
        const Elite::NewShip viper = Elite::SpawnShipAhead(universe.bubble, universe.work, Elite::ShipType::Viper, universe.flight.delta,
                                                           universe.bubble.missileTarget, universe.flight.blueprint); // 6502: FRS1
        trace.viperCreated = viper.created;
        if (viper.created)
        {
          Elite::Anger(universe.bubble, universe.flight, viper.slot, Elite::ShipType::Viper); // 6502: ANGRY
        }
      }
      if (!frames(240,
                  [&](std::uint32_t _frame)
                  {
                    _port.held[Elite::KEY_FIRE] = ((_frame % 8u) < 4u) ? 1u : 0u;
                    if (_frame >= 100u && _frame < 140u)
                    {
                      _port.held[Elite::KEY_PITCH_UP] = 1u;
                    }
                  }))
      {
        trace.steps = step;
        return trace;
      }

      // Phase four: the docking computer, and the autopilot until it docks or the cap.
      if (!frames(2, [&](std::uint32_t) { _port.held[Elite::KEY_DOCKING_COMPUTER] = 1u; }))
      {
        trace.steps = step;
        return trace;
      }
      static_cast<void>(frames(AUTOPILOT_LIMIT, [](std::uint32_t) {}));
      trace.steps = step;
      return trace;
    }

    std::wstring Describe(const Trace& _trace)
    {
      std::wostringstream out;
      out << L"\n    constexpr Checkpoint RECORDED[] = {\n";
      for (const Checkpoint& point : _trace.checkpoints)
      {
        out << L"      {" << point.step << L", 0x" << std::hex << std::setw(16) << std::setfill(L'0') << point.digest << std::dec
            << L"ull},\n";
      }
      out << L"    };\n    constexpr std::uint32_t RECORDED_STEPS = " << _trace.steps << L";\n"
          << L"    constexpr Elite::LoopOutcome RECORDED_OUTCOME = Elite::LoopOutcome::"
          << (_trace.outcome == Elite::LoopOutcome::Docked    ? L"Docked"
              : _trace.outcome == Elite::LoopOutcome::Died    ? L"Died"
              : _trace.outcome == Elite::LoopOutcome::Escaped ? L"Escaped"
                                                              : L"Continued")
          << L";\n";
      return out.str();
    }

    bool SameCheckpoints(const std::vector<Checkpoint>& _left, const std::vector<Checkpoint>& _right)
    {
      if (_left.size() != _right.size())
      {
        return false;
      }
      for (std::size_t index = 0; index < _left.size(); ++index)
      {
        if (_left[index].step != _right[index].step || _left[index].digest != _right[index].digest)
        {
          return false;
        }
      }
      return true;
    }

  } // namespace

  TEST_CLASS(TheFlightReplay)
  {
  public:
    /// The scripted flight, digest for digest against the record.
    TEST_METHOD(TheScriptedFlightIsAsRecorded)
    {
      auto port = std::make_unique<FlightPort>();
      const Trace trace = Fly(*port);

      Assert::IsTrue(trace.viperCreated, L"the script's Viper did not fit in the bubble");

      const std::size_t recorded = sizeof(RECORDED) / sizeof(RECORDED[0]);
      const bool unrecorded = (RECORDED_STEPS == 0u);
      bool same = !unrecorded && recorded == trace.checkpoints.size() && RECORDED_STEPS == trace.steps && RECORDED_OUTCOME == trace.outcome;
      std::size_t first = 0;
      for (std::size_t index = 0; same && index < recorded; ++index)
      {
        if (RECORDED[index].step != trace.checkpoints[index].step || RECORDED[index].digest != trace.checkpoints[index].digest)
        {
          same = false;
          first = index;
        }
      }
      if (!same)
      {
        std::wstring why = unrecorded ? L"the replay has no record yet" : L"the flight is not the recorded one";
        if (!unrecorded && recorded == trace.checkpoints.size())
        {
          why += L" -- first differs at step " + std::to_wstring(trace.checkpoints[first].step);
        }
        Assert::Fail((why + L"; the flight as run was:" + Describe(trace)).c_str());
      }
    }

    /// The same script twice in one process gives the same digests, step for step.
    TEST_METHOD(TheReplayIsDeterministic)
    {
      auto first = std::make_unique<FlightPort>();
      auto second = std::make_unique<FlightPort>();
      const Trace one = Fly(*first);
      const Trace two = Fly(*second);

      Assert::AreEqual(one.steps, two.steps, L"the two flights ran a different number of steps");
      Assert::IsTrue(one.outcome == two.outcome, L"the two flights ended differently");
      Assert::IsTrue(SameCheckpoints(one.checkpoints, two.checkpoints), (L"the two flights diverged:" + Describe(two)).c_str());
    }

    /*
     * The harness's own `OracleIsPresent`: one byte changed anywhere in the universe after the
     * launch changes the record. Four bytes in four places -- the planet's position, the generator,
     * the commander's fuel, a speck of stardust -- so that a digest that had quietly stopped
     * covering one of them would be noticed here rather than by a refactor slipping through.
     */
    TEST_METHOD(OneByteAnywhereChangesTheRecord)
    {
      auto baseline = std::make_unique<FlightPort>();
      const Trace unperturbed = Fly(*baseline);

      const std::vector<std::pair<const wchar_t*, Perturbation>> perturbations = {
        {L"the planet's x", [](FlightPort& _port)
         { _port.universe.bubble.blocks[0].x.lo = static_cast<std::uint8_t>(_port.universe.bubble.blocks[0].x.lo ^ 0x01u); }},
        {L"the generator",
         [](FlightPort& _port)
         {
           std::array<std::uint8_t, 4> state = _port.universe.rng.State();
           state[1] = static_cast<std::uint8_t>(state[1] ^ 0x80u);
           _port.universe.rng.SetState(state);
         }},
        {L"the fuel",
         [](FlightPort& _port) { _port.universe.commander.fuel = static_cast<std::uint8_t>(_port.universe.commander.fuel - 1u); }},
        {L"a speck of stardust",
         [](FlightPort& _port) { _port.universe.dust.z[3] = static_cast<std::uint8_t>(_port.universe.dust.z[3] ^ 0x40u); }},
      };

      for (const auto& [name, perturb] : perturbations)
      {
        auto port = std::make_unique<FlightPort>();
        const Trace perturbed = Fly(*port, perturb);
        Assert::IsFalse(SameCheckpoints(unperturbed.checkpoints, perturbed.checkpoints),
                        (std::wstring(L"changing ") + name + L" left every digest as it was").c_str());
      }
    }
  };

} // namespace GameLogicTests
