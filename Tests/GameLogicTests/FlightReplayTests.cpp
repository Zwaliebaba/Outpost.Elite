#include "pch.h"

#include "FlightPort.h"
#include "FlightUniverse.h"

#include "Commander.h"
#include "DockedKeys.h"
#include "Flight.h"
#include "FlightLoop.h"
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
 * WHAT THIS PINS THAT NOTHING ELSE DOES. Every routine in the flight was compared against the
 * shipped game somewhere in this suite, one call at a time, and a refactor that kept every one of
 * those green could still change how they compose: the order two side effects happen in, a value
 * one routine leaves for the next (`XX0`, `K3+1`), a seam answered with a different argument. The
 * replay sees the composition, and it sees it without the original -- which is why it outlived it
 * (Modernize.md M6-b-5) and what the modernisation's every slice is measured by (Risk R14).
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
      std::uint64_t state; ///< `Game::StateHash()`, library-native (M5-e-3) -- the one that outlived the labels
    };

    /// Digests are taken this often, and at every turn of the script besides.
    constexpr std::uint32_t CHECKPOINT_EVERY = 100;

    /// How long the autopilot is given. `DOCKIT` takes the ship in well inside this in the script
    /// as recorded; the cap exists so that a broken autopilot fails rather than runs for ever.
    constexpr std::uint32_t AUTOPILOT_LIMIT = 4000;

    /// The record. Empty is a failure, so a tree can never carry an unpinned replay.
    /*
     * RE-TAKEN EIGHT TIMES. Five were Modernize.md rule 1's second case (the port was wrong); the
     * fifth, M5-a-6, the seventh, M6-0-d, and the eighth, M6-0-a, are the FIRST case (the digest was
     * deliberately widened). The count said "twice" until 2026-09-07 and had never counted the
     * owner's fix below.
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
     *
     * 2026-09-06, the death-sequence fix: the record moved from step 200 on, because no explosion
     * cloud had ever been seeded and the script's first kill seeds one now.
     *
     * 2026-09-07, the replay drives `Elite::Game`: every digest moved and NOT ONE STEP DID -- 1,170
     * steps ending `Docked`, before and after. Two defects in this fixture, both of them the fixture
     * differing from the app rather than the game differing from the original. `FlightPort` built
     * `Ports` itself, so the value tokens and the control codes were deferred where the app runs
     * them, and it never ran `NA%`: the scripted flight has been flown by a commander of all zeros,
     * with no fuel, no laser and a galaxy seed of zero. And it held a second `QQ12` beside
     * `Universe::dockedFlag` -- `Launch` cleared one and the flight loop's arrival wrote the other.
     * The last of the three is why the first measurement of this change read 2,589 steps ending
     * `Died` (§8, and the entry that corrects it).
     *
     * 2026-09-07, M5-a-6, and the only one so far under rule 1's FIRST case: every digest moved,
     * not one step did, and no line of `GameLogic/` changed at all -- the image got wider. ADR-007
     * §5 had named seven bytes the digest could not see; five of them can have cells and now do
     * (`safehouse`, `QQ8`, `JSTGY`, `JSTE`, `MUTOKOLD`). The other two were never one gap with
     * these five: `soundDisabled` was a SECOND `DNOIZ` and M5-a-5 deleted it, and `crosshairStep`
     * is what `TT17` leaves in X and Y -- REGISTERS, with no address for `Where` to look up and so
     * nothing a cell can name. Everything else in the suite stayed green through the widening,
     * which is the evidence that the five new cells agree with the oracle wherever `CompareState`
     * already looks; only the record needed re-taking.
     *
     * 2026-09-07, M6-0-a: the label column again, the state column not at all. The interpreter
     * banks the I/O page now, so the VIC-II's sprite registers stopped being `XX21`'s bytes and
     * the seventeen sprite cells joined the image unconditionally -- a fixture used to have to
     * CLAIM them. Rule 1's first case, and the state column is the witness as before.
     *
     * 2026-09-07, M6-0-d: the label column moves on every checkpoint and THE STATE COLUMN DOES
     * NOT -- which is the state column doing the job it was built for. Nine planet-and-sun cells
     * joined the image (`SUNX`, `Yx2M1`, `K5`, `K6`, `STP`, `FLAG`, `PLTOG`, `V`), so the label hash
     * reads more bytes than it did; `Game::StateHash` already folded all of them, and its sixteen
     * digests are the ones above to the bit, so the flight is unchanged and the widening is the
     * whole of the move. Rule 1's first case, proven by the second column rather than argued.
     *
     * 2026-09-07, M5-e-3: the record gains a SECOND COLUMN and the first does not move. Each
     * checkpoint carries `Game::StateHash()` beside the label digest -- the library-native fold
     * over `Universe` (`Elite::HashState`), which needs no oracle and is the hash that outlives
     * M6-f. Not a re-take: the label column is the one above, to the bit; the state column is new,
     * taken from the same flight, and `StateHashTests` holds the fold to the label table's cells.
     *
     * 2026-09-07, M6-0-b: NOT A RE-TAKE. Two records were added beside this one -- a flight into
     * the planet and one out by the escape pod, below -- and this one did not move by a bit, which
     * is the whole point of adding rather than changing.
     *
     * 2026-09-07, M5-e-2, `Game` owns the universe: every digest moved and not one step did, and
     * this time the proof is in the fixture rather than the suite. Nine of the image's cells --
     * `QQ17` and `DTW1`-`DTW8` -- read the two printers, which are not in `Elite::Universe`, and
     * `Hash(universe)` read the test WRAPPER's printers: idle objects the wrapper's constructor set
     * and nothing in the flight ever drove, while `Game` printed every message with its own. The
     * second-`QQ12` shape again, a level up. The digest reads `Game`'s printers now
     * (`Hash(universe, game.Recursive(), game.Characters())`), and hashing a fresh idle wrapper's
     * printers in their place reproduces the previous sixteen digests to the bit -- so the
     * flight is the flight it was, and what moved is nine bytes that had been constants.
     */
    constexpr Checkpoint RECORDED[] = {
      {0, 0x2ab36cb10fc3d025ull},     // launched from Lave
      {40, 0x41c73e56e5b6dec0ull},    // coasted
      {100, 0x52b4ee3425808769ull},   // at full speed
      {200, 0x38f68d860fee04daull},
      {300, 0x8cacc02d70b22da1ull},
      {340, 0xf1365571ec21753bull},   // the Viper fought
      {342, 0xe40530ef96dd4a8aull},   // the docking computer engaged
      {400, 0x7cc5be3c31affc57ull},
      {500, 0x650920d3bf96ad4dull},
      {600, 0x6ab3ccd16ef631ccull},
      {700, 0x8e7b06a8fc599754ull},
      {800, 0x5625267a616ccbd3ull},
      {900, 0xece8c828544e5bc6ull},
      {1000, 0xb8e25422b42eb7b9ull},
      {1100, 0xb2527b82bca1dbf0ull},
      {1170, 0xf42b8700b049cf7cull},  // docked
    };
    constexpr std::uint32_t RECORDED_STEPS = 1170;
    constexpr Elite::LoopOutcome RECORDED_OUTCOME = Elite::LoopOutcome::Docked;

    /*
     * THE TWO ENDINGS THE FIRST RECORD COULD NOT HAVE (M6-0-b). §4.10 asks the replay to cover
     * "launch, flight, combat, docking, death and the escape pod", and the flight above covers
     * four. These two scripts end the other two ways, and each is its own record: the same launch,
     * then straight into the planet at full speed until `MA23` says the altitude is gone -- with
     * `DEATH`'s sixty-five frames of wreckage digested at their first, middle and last, through
     * the presenter the death sequence paces itself by -- or the escape pod pulled after the same
     * roll the docked flight makes, and `ESCAPE`'s Cobra spawned and the arrival flown through.
     * Both run through `Game::Leave`, so what is digested at the end is the docked game the
     * ending leaves behind: for a death that is `RES2` and the two title screens `BR1` shows on
     * the way to the bay, ended here by RETURN held through the ramming (M6-0-h-2).
     */
    constexpr Checkpoint RECORDED_DEATH[] = {
      {0, 0x2ab36cb10fc3d025ull},
      {40, 0x41c73e56e5b6dec0ull},
      {100, 0xcf0708d959c8d9b5ull},
      {200, 0xf31650189d79a522ull},
      {300, 0xb5c764b1f87030ccull},
      {400, 0xc5a2ac751108c15eull},
      {500, 0xfa51e22968f351d1ull},
      {600, 0xa97903db4ccd7f65ull},
      {700, 0x7a7ec0b76786e8f1ull},
      {800, 0x770e957149997397ull},
      {900, 0xed9888c5284035edull},
      {1000, 0xe90ae1c8215cb3d2ull},
      {1078, 0x0a832382a56065d8ull}, // DEATH's first frame of wreckage
      {1078, 0xec34cc852da779bbull}, // its thirty-third
      {1078, 0x6668376ec5651e8full}, // its sixty-fifth and last
      {1079, 0xeb31e3057f1eb194ull}, // and the docked game BR1 leaves
    };
    constexpr std::uint32_t RECORDED_DEATH_STEPS = 1079;
    constexpr std::uint32_t RECORDED_DEATH_FRAMES = 65; ///< `DEATH`'s `LASCT` loop, counted at the presenter

    constexpr Checkpoint RECORDED_ESCAPE[] = {
      {0, 0xd7815787c1624578ull},
      {40, 0x990865d9800c90e9ull},
      {100, 0xa28af286e648c5a4ull},
      {102, 0xd860de950ede3562ull}, // ESCAPE, and the arrival it flies through
    };
    constexpr std::uint32_t RECORDED_ESCAPE_STEPS = 102;

    /// How long a ramming is given before the script gives up on the planet.
    constexpr std::uint32_t RAMMING_LIMIT = 3000;

    /*
     * The picture's own record, one entry per checkpoint of the scripted flight -- see
     * `ThePictureIsAsRecorded`. Recorded 2026-09-09 on the tree as RN-0 found it, by running the
     * flight and printing what it drew; every value here came out of the port and none was chosen.
     */
    constexpr std::uint64_t RECORDED_PICTURE[] = {
      0xDCFA8C65CCB1CEACull, 0xAAFD70C67DFABF0Full, 0xEA56D42A32E51CD7ull, 0x015A06C177737503ull,
      0xE821E6604E3CF8EEull, 0xC2BEDF3AB25D91E4ull, 0x596B0A3997B19656ull, 0x9EBD54B99ADBEDA8ull,
      0x1E2143C755120AC6ull, 0xFD1F2BA5FFF4B690ull, 0xF837D8E84327CD22ull, 0x94729D971173D01Aull,
      0xA302EF23E2247B98ull, 0xD1151AC33278ABD6ull, 0xA788104F5F517874ull, 0x0A1B2189DFA7975Dull,
    };

    struct Trace
    {
      std::vector<Checkpoint> checkpoints;
      std::uint32_t steps = 0;
      Elite::LoopOutcome outcome = Elite::LoopOutcome::Continued;
      bool viperCreated = false;
      std::uint32_t deathFrames = 0; ///< how many frames `DEATH` showed, zero unless the flight died

      /*
       * What the 640x400 surface RESOLVED TO at each of the checkpoints above (Rendering.md RN-0).
       *
       * The digests beside it are the game's; this one is the picture's, and the two answer
       * different questions. `HashState` walks past the picture on purpose (ADR-008 §4), so the
       * replay tables cannot see a change to what a person is shown -- which was fine while the
       * picture was only ever drawn, and stops being fine the moment a slice reorganises WHEN it is
       * drawn. RN-0 and RN-1 do exactly that, and Rendering.md's risk RN-a is that a region nobody
       * noticed was persistent blanks afterwards with no test looking at it.
       *
       * So this is that test. It is filled on every flight and asserted by one
       * (`ThePictureIsAsRecorded`); the other three ignore it, because a state digest that moved
       * would fail them first and a picture digest is the harder thing to read.
       */
      std::vector<std::uint64_t> pictures;
    };

    /// How a scripted flight ends (M6-0-b): the docking computer, the planet, or the escape pod.
    enum class Ending : std::uint8_t
    {
      Docked,
      Died,
      Escaped,
    };

    /*
     * 6502: DEATH's `.D2 JSR M% / DEC LASCT / BNE D2` -- the sixty-five frames of wreckage, paced
     * through `HoldFlightFrame` and nothing else, so a presenter watching that call sees each one.
     * Three of them are digested: the first, the middle and the last.
     */
    struct DeathWatch final : Elite::Presenter
    {
      std::function<void()> onFrame;
      std::uint32_t frames = 0;

      void WaitFrames(std::uint8_t) override {}
      void Present() override {}
      void HoldTitleFrame(std::uint8_t) override {}
      void HoldFlightFrame(std::uint8_t) override
      {
        ++frames;
        if (frames == 1u || frames == 33u || frames == 65u)
        {
          onFrame();
        }
      }
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

      Elite::ResetGame(_port.universe, _port.Ports()); // 6502: RESET
    }

    using Perturbation = std::function<void(FlightPort&)>;

    /// The script. `_perturb`, when given, runs once after the launch and before the first frame.
    Trace Fly(FlightPort& _port, const Perturbation& _perturb = {}, Ending _ending = Ending::Docked)
    {
      Trace trace;
      std::uint32_t step = 0;
      auto checkpoint = [&]() {
        trace.checkpoints.push_back(Checkpoint{step, _port.StateDigest()});
        trace.pictures.push_back(_port.universe.picture.Hash(_port.universe.canvas));
      };

      Prepare(_port);
      if (_ending == Ending::Escaped)
      {
        _port.universe.commander.escapePod = 0xFFu; // 6502: ESCP -- so there is a pod to pull
      }
      Elite::SystemSeeds selected{};
      Elite::Launch(_port.universe, _port.Ports(), _port.universe.commander.systemX,
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

      if (_ending == Ending::Died)
      {
        /*
         * Straight at the planet, which the launch put dead ahead, at full speed until `MA23`
         * kills us. RETURN is held throughout because the death runs off into `BR1`'s two title
         * screens inside `Game::Leave` (M6-0-h-2), and a title screen ends only on a key; the flight
         * half does not read RETURN.
         */
        DeathWatch watch;
        watch.onFrame = checkpoint;
        _port.watching = &watch;
        static_cast<void>(frames(RAMMING_LIMIT,
                                 [&](std::uint32_t)
                                 {
                                   _port.held[Elite::KEY_SPEED_UP] = 1u;
                                   _port.held[Elite::KEY_CROSSHAIR_FAST] = 1u;
                                 }));
        _port.watching = nullptr;
        trace.deathFrames = watch.frames;
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

      if (_ending == Ending::Escaped)
      {
        // 6502: KY13 with ESCP set -- part 3's escape-pod test, and `JMP ESCAPE` out of the loop.
        static_cast<void>(frames(4, [&](std::uint32_t) { _port.held[Elite::KEY_ESCAPE_POD] = 1u; }));
        trace.steps = step;
        return trace;
      }

      // Phase three: a hostile Viper straight ahead, and the laser at it.
      {
        Elite::Universe& universe = _port.universe;
        const Elite::NewShip viper = Elite::SpawnShipAhead(universe.bubble, universe.work, Elite::ShipType::Viper, universe.flight.speed,
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

    std::wstring Describe(const Trace& _trace, const wchar_t* _name = L"RECORDED")
    {
      std::wostringstream out;
      out << L"\n    constexpr Checkpoint " << _name << L"[] = {\n";
      for (const Checkpoint& point : _trace.checkpoints)
      {
        out << L"      {" << point.step << L", 0x" << std::hex << std::setw(16) << std::setfill(L'0') << point.state << std::dec
            << L"ull},\n";
      }
      out << L"    };\n    constexpr std::uint32_t " << _name << L"_STEPS = " << _trace.steps << L";\n"
          << L"    constexpr Elite::LoopOutcome " << _name << L"_OUTCOME = Elite::LoopOutcome::"
          << (_trace.outcome == Elite::LoopOutcome::Docked    ? L"Docked"
              : _trace.outcome == Elite::LoopOutcome::Died    ? L"Died"
              : _trace.outcome == Elite::LoopOutcome::Escaped ? L"Escaped"
                                                              : L"Continued")
          << L";\n";
      if (_trace.deathFrames != 0u)
      {
        out << L"    constexpr std::uint32_t " << _name << L"_FRAMES = " << _trace.deathFrames << L";\n";
      }
      return out.str();
    }

    /// A flight against its record, in the form the failure message prints so that a re-take is a paste.
    void AssertAsRecorded(const Trace& _trace, const Checkpoint* _recorded, std::size_t _count, std::uint32_t _steps,
                          Elite::LoopOutcome _outcome, const wchar_t* _name)
    {
      const bool unrecorded = (_steps == 0u);
      bool same = !unrecorded && _count == _trace.checkpoints.size() && _steps == _trace.steps && _outcome == _trace.outcome;
      std::size_t first = 0;
      for (std::size_t index = 0; same && index < _count; ++index)
      {
        if (_recorded[index].step != _trace.checkpoints[index].step || _recorded[index].state != _trace.checkpoints[index].state)
        {
          same = false;
          first = index;
        }
      }
      if (!same)
      {
        std::wstring why = unrecorded ? L"the replay has no record yet" : L"the flight is not the recorded one";
        if (!unrecorded && _count == _trace.checkpoints.size())
        {
          why += L" -- first differs at step " + std::to_wstring(_trace.checkpoints[first].step);
        }
        Assert::Fail((why + L"; the flight as run was:" + Describe(_trace, _name)).c_str());
      }
    }

    bool SameCheckpoints(const std::vector<Checkpoint>& _left, const std::vector<Checkpoint>& _right)
    {
      if (_left.size() != _right.size())
      {
        return false;
      }
      for (std::size_t index = 0; index < _left.size(); ++index)
      {
        if (_left[index].step != _right[index].step || _left[index].state != _right[index].state)
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
      AssertAsRecorded(trace, RECORDED, sizeof(RECORDED) / sizeof(RECORDED[0]), RECORDED_STEPS, RECORDED_OUTCOME, L"RECORDED");
    }

    /// The same launch flown into the planet, and `DEATH` digested at three of its frames (M6-0-b).
    TEST_METHOD(TheDeathIsAsRecorded)
    {
      auto port = std::make_unique<FlightPort>();
      const Trace trace = Fly(*port, {}, Ending::Died);
      Assert::IsTrue(trace.outcome == Elite::LoopOutcome::Died, (L"the ramming did not end in DEATH:" + Describe(trace, L"RECORDED_DEATH")).c_str());
      Assert::AreEqual<std::uint32_t>(65u, trace.deathFrames, L"6502: LASCT -- DEATH shows sixty-five frames of wreckage");
      AssertAsRecorded(trace, RECORDED_DEATH, sizeof(RECORDED_DEATH) / sizeof(RECORDED_DEATH[0]), RECORDED_DEATH_STEPS,
                       Elite::LoopOutcome::Died, L"RECORDED_DEATH");
      Assert::AreEqual(RECORDED_DEATH_FRAMES, trace.deathFrames, L"the wreckage ran for as many frames as recorded");
    }

    /// The same launch and roll, then the escape pod, and `ESCAPE`'s arrival flown through (M6-0-b).
    TEST_METHOD(TheEscapePodIsAsRecorded)
    {
      auto port = std::make_unique<FlightPort>();
      const Trace trace = Fly(*port, {}, Ending::Escaped);
      Assert::IsTrue(trace.outcome == Elite::LoopOutcome::Escaped, (L"the pod was not pulled:" + Describe(trace, L"RECORDED_ESCAPE")).c_str());
      AssertAsRecorded(trace, RECORDED_ESCAPE, sizeof(RECORDED_ESCAPE) / sizeof(RECORDED_ESCAPE[0]), RECORDED_ESCAPE_STEPS,
                       Elite::LoopOutcome::Escaped, L"RECORDED_ESCAPE");
    }

    /*
     * WHAT A PERSON IS SHOWN, digested at every checkpoint of the scripted flight
     * (Design/Rendering.md RN-0, the gate its slice table asks for).
     *
     * THE REPLAY TABLES ABOVE CANNOT SEE THE PICTURE. `HashState` walks past it deliberately
     * (ADR-008 §4) so that improving the rendering does not re-record five tables of game state --
     * a decision that is right and that leaves the 640x400 surface with no whole-frame regression
     * test at all. Every other test of it is per routine: a glyph at a cell, a bar at a value, a
     * line swept over its inputs. Nothing until now asserted a WHOLE FRAME of it, let alone a
     * hundred frames of a real flight.
     *
     * That gap is exactly what RN-0 and RN-1 walk into. They do not change what is drawn; they
     * change WHEN, moving the persistent regions onto a second surface and clearing the frame each
     * pass. Rendering.md's risk RN-a names the failure: a region nobody noticed was persistent
     * blanks afterwards, and it looks like nothing at all until somebody launches the game.
     *
     * So the numbers below are a BEFORE, recorded on the tree as it stood when RN-0 opened, and the
     * slice's acceptance is that they do not move. They are not fidelity -- no oracle ever saw this
     * surface (ADR-008 §3) -- they are identity across a refactor, which is the only thing a
     * picture digest can be.
     */
    TEST_METHOD(ThePictureIsAsRecorded)
    {
      auto port = std::make_unique<FlightPort>();
      const Trace trace = Fly(*port);

      // The table, printed on every run so that re-recording is a copy rather than a transcription.
      {
        std::wstringstream table;
        table << L"RECORDED_PICTURE[] = {";
        for (const std::uint64_t digest : trace.pictures)
        {
          table << L"0x" << std::hex << std::uppercase << digest << L"ull, ";
        }
        table << L"};\n";
        Logger::WriteMessage(table.str().c_str());
      }

      Assert::AreEqual(sizeof(RECORDED_PICTURE) / sizeof(RECORDED_PICTURE[0]), trace.pictures.size(),
                       L"the flight took a different number of checkpoints, so the picture table cannot line up");

      for (std::size_t at = 0; at < trace.pictures.size(); ++at)
      {
        if (trace.pictures[at] != RECORDED_PICTURE[at])
        {
          std::wstringstream message;
          message << L"the picture changed at checkpoint " << at << L" (step " << trace.checkpoints[at].step << L"): recorded 0x"
                  << std::hex << std::uppercase << RECORDED_PICTURE[at] << L", drew 0x" << trace.pictures[at]
                  << L". Nothing about the GAME moved -- the state digests are asserted separately and pass -- so this is what a"
                  << L" person is shown changing. If a slice meant to change it, re-record with the reason in the journal"
                  << L" (Rendering.md RN-0).";
          Assert::Fail(message.str().c_str());
        }
      }
    }

    /*
     * All three flights again with the TWINS SWITCHED OFF, and not one digest may move
     * (Design/Resolution.md section 8.4).
     *
     * THIS IS RULE T1'S MEASUREMENT, AND IT DID NOT EXIST UNTIL 2026-09-09. T1 says the faithful
     * routine decides and the twin only computes where -- so a twin may consume nothing the game
     * would notice: no random number, no heap pointer, no canvas byte. ADR-008 section 3 published
     * that as PROVED by exactly this test, and the test had never been written; the define the
     * design named for it, `ELITE_SCREEN_SHADOW`, appears nowhere but in the sentence describing
     * it. Design/Rendering.md section 11.3 is the finding, and this is its repair.
     *
     * A RUNTIME SWITCH RATHER THAN THE COMPILE-TIME ONE THE DESIGN ASKED FOR, which is the single
     * place this departs from section 8.4 and is recorded there as CORRECTED. A define needs a
     * second compilation of `GameLogic` and a second test binary beside it; `Picture::SetDrawing`
     * needs neither and reaches every twin through the one guard they all share, `DrawingTwins`.
     * What a define would additionally have caught is a twin reading something BEFORE its guard --
     * so where the guard sits is what a reviewer checks, and every one of the forty-nine wraps the
     * whole of its twin's work.
     *
     * THE PLANE ASSERTIONS ARE WHAT STOP THIS PASSING VACUOUSLY, and they are not decoration: a
     * `SetDrawing` that did nothing would leave every digest exactly where it was and the test
     * would be green for precisely the wrong reason. So the run also proves the switch bit -- the
     * bitmap plane is untouched with the twins off, and written with them on.
     */
    TEST_METHOD(TheReplayIsTheSameWithNoTwins)
    {
      const auto anythingDrawn = [](const Elite::Picture& _picture) {
        for (const std::uint8_t byte : _picture.Bitmap())
        {
          if (byte != 0u)
          {
            return true;
          }
        }
        return false;
      };

      auto quiet = std::make_unique<FlightPort>();
      quiet->universe.picture.SetDrawing(false);
      const Trace quietFlight = Fly(*quiet);
      AssertAsRecorded(quietFlight, RECORDED, sizeof(RECORDED) / sizeof(RECORDED[0]), RECORDED_STEPS, RECORDED_OUTCOME,
                       L"RECORDED with no twins");
      Assert::IsFalse(anythingDrawn(quiet->universe.picture), L"a twin drew on the picture with the twins switched off");

      auto quietDeath = std::make_unique<FlightPort>();
      quietDeath->universe.picture.SetDrawing(false);
      const Trace death = Fly(*quietDeath, {}, Ending::Died);
      AssertAsRecorded(death, RECORDED_DEATH, sizeof(RECORDED_DEATH) / sizeof(RECORDED_DEATH[0]), RECORDED_DEATH_STEPS,
                       Elite::LoopOutcome::Died, L"RECORDED_DEATH with no twins");
      Assert::AreEqual(RECORDED_DEATH_FRAMES, death.deathFrames, L"the wreckage ran for as many frames with no twins");

      auto quietPod = std::make_unique<FlightPort>();
      quietPod->universe.picture.SetDrawing(false);
      const Trace pod = Fly(*quietPod, {}, Ending::Escaped);
      AssertAsRecorded(pod, RECORDED_ESCAPE, sizeof(RECORDED_ESCAPE) / sizeof(RECORDED_ESCAPE[0]), RECORDED_ESCAPE_STEPS,
                       Elite::LoopOutcome::Escaped, L"RECORDED_ESCAPE with no twins");

      // The control. Without this the three runs above prove only that a disabled surface stays
      // blank, which would also be true if the twins had never been wired up at all.
      auto drawn = std::make_unique<FlightPort>();
      const Trace control = Fly(*drawn);
      Assert::IsTrue(anythingDrawn(drawn->universe.picture),
                     L"the twins drew nothing even switched ON, so this test proves nothing about them");
      Assert::IsTrue(control.checkpoints.size() == quietFlight.checkpoints.size(),
                     L"the two runs took a different number of checkpoints");
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
         [](FlightPort& _port) { _port.universe.commander.fuel.tenths = static_cast<std::uint8_t>(_port.universe.commander.fuel.tenths - 1u); }},
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
