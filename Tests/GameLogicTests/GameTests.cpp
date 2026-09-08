#include "pch.h"

#include "NullSeams.h"

#include "Charts.h"
#include "Commander.h"
#include "Controls.h"
#include "DockedKeys.h"
#include "Game.h"
#include "MarketScreen.h"
#include "SoundEffects.h"
#include "SystemScreen.h"
#include "Universe.h"

#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * `Elite::Game` -- the top of the program, driven the way the executable drives it (slice M3-c).
 *
 * WHAT THIS IS FOR IS THE SHAPE RATHER THAN THE ARITHMETIC. Every routine `Game` calls is compared
 * against the shipped original by its own suite, over sweeps this could not improve on; what none
 * of them can see is whether the OBJECT that calls them is wired up -- whether `Reset` reaches the
 * cold start, whether a docked pass dispatches a key, whether the key that used to freeze the
 * game is an ordinary key now. That was eight hundred lines in `Outpost/Main.cpp` until M3-c, in the one
 * file no Linux runner compiles, and the answer to "does it still work" was the Windows job and a
 * human looking at a screen.
 *
 * IT NEEDS NO ORACLE, and that is not a gap. There is no 6502 routine called `Game`: the original's
 * top level is `FRCE`'s two jumps and the loop they land in, which this suite drives rather than
 * compares. The comparisons live under it.
 */
namespace GameLogicTests
{

  namespace
  {
    /// The platform, answered with nothing -- which is every seam `Game` takes, `NullSeams` over
    /// all of them. The chip's log is `Game`'s own since M5-e-1.
    struct Bare
    {
      Bare()
        : game(nulls, keys, nulls)
      {
      }

      /*
       * 6502: TITLE runs inside the cold start since M6-0-h-2, and a title screen ends only on a
       * key. RETURN is held throughout: it ends both title screens (and is not "Y", so no disk
       * menu), the flight half does not read it -- the flight keys are `KY1` to `KY7` and `KY12`
       * to `KY20`, and the dispatch compares `thiskey` against keys RETURN is not -- and the
       * docked half reads `NextKey`. A death's `BR1` reaches the title too, so a key held only
       * around `Reset` would leave the flight test below waiting for ever.
       */
      struct TitleKey final : NullSeams
      {
        bool Held(std::size_t _key) override
        {
          return _key == Elite::KEY_CROSSHAIR_FAST;
        }
      };

      Bare(const Bare&) = delete;
      Bare& operator=(const Bare&) = delete;

      // `NoAutopilot` -- `ControlEffects` answered with nothing -- WAS HERE AND IS NOT ANY MORE
      // (M6-0-h-3): `DOCKIT` is a library call from `DOKEY`, and nothing here switches it on.

      NullSeams nulls;
      TitleKey keys;
      Elite::Game game;
    };
  } // namespace

  TEST_CLASS(TheGameObject)
  {
  public:
    /*
     * 6502: TT170 -- the cold start, which ends docked with the status screen on the display.
     *
     * The three assertions are the three things `Reset` is for: the commander is the default one
     * (`NA%`), the game is DOCKED (`QQ12`), and the market has been rolled -- `GenerateMarket` is
     * the call that a start sequence which forgot it would leave as a table of zeroes, and it was
     * a real defect in this port before slice 4c.
     */
    TEST_METHOD(TheColdStartEndsDockedWithAMarket)
    {
      Bare bare;
      bare.game.Reset();

      Assert::IsTrue(bare.game.Docked(), L"TT170 ends by entering the docked half");

      const Elite::Commander& commander = bare.game.State().commander;
      Assert::AreEqual<std::uint32_t>(Elite::DefaultCommander().fuel.tenths, commander.fuel.tenths, L"NA% -- the default commander's fuel");

      std::uint32_t priced = 0;
      for (const std::uint8_t price : bare.game.State().market.price)
      {
        priced += (price != 0u) ? 1u : 0u;
      }
      Assert::IsTrue(priced > 0u, L"the market is rolled on arrival, so it is not a table of zeroes");
    }

    /*
     * 6502: DK4's `CPX #&40` -- INST/DEL, which froze the game until InputTimer.md I-0 (owner
     * ruling 2026-09-08) and is an ordinary key now.
     *
     * The pause screen was the port's `Mode::Paused`, a state the outer loop could enter and, with
     * the resume key unbound, never leave. What is worth asserting after its removal is the other
     * direction: the key still reaches `DK4`'s `STX KL`, and the batch of steps carries on.
     */
    /*
     * 6502: TITLE's `BIT KY7 / BMI TL3` -- the fire key ends the title screen with `JSTK` still set,
     * which on a C64 is the player saying they have a joystick.
     *
     * The port has no stick to read, so `Game` settles the byte after the start sequence: cleared
     * unless the platform says it has one (InputTimer.md §5.1, I-3). Both answers are driven, and
     * the second is what keeps the original's rule reachable for the gamepad slice.
     */
    TEST_METHOD(TheFireKeyOnTheTitleSelectsAJoystickOnlyWhenThePlatformHasOne)
    {
      struct FireKey : NullSeams
      {
        bool Held(std::size_t _key) override
        {
          return _key == Elite::KEY_FIRE;
        }
        bool HasJoystick() noexcept override
        {
          return joystick;
        }
        bool joystick = false;
      };

      {
        FireKey keys;
        NullSeams nulls;
        Elite::Game game(nulls, keys, nulls);
        game.Reset();
        Assert::IsTrue(game.Docked(), L"fire is not Y, so no disk menu, and the cold start still ends docked");
        Assert::AreEqual<std::uint8_t>(0u, game.State().options.joystick, L"6502: JSTK -- cleared, because there is no stick to read");
      }
      {
        FireKey keys;
        keys.joystick = true;
        NullSeams nulls;
        Elite::Game game(nulls, keys, nulls);
        game.Reset();
        Assert::AreEqual<std::uint8_t>(0xFFu, game.State().options.joystick, L"6502: JSTK -- and the original's answer when there is");
      }
    }

    TEST_METHOD(TheOldPauseKeyIsAnOrdinaryKey)
    {
      Bare bare;
      bare.game.Reset();

      constexpr std::uint8_t INST_DEL = 0x40; // 6502: the key `DK4` compared against
      Assert::IsTrue(bare.game.Step(INST_DEL), L"INST/DEL no longer ends the batch of steps");
      Assert::AreEqual<std::uint8_t>(INST_DEL, bare.game.State().keys[0], L"6502: STX KL -- the key that arrived, in byte 0 of the logger");
      Assert::IsTrue(bare.game.ModeNow() == Elite::Game::Mode::Docked, L"and the game is where it was");
    }

    /*
     * 6502: MLOOP's tail -- a docked pass with no key at all, which is most of them.
     *
     * IT MUST NOT BE A NO-OP, and that is the assertion: `TT102` runs every pass and a key nothing
     * matches falls through `HME1` into `TT107`, which is how the hyperspace countdown ticks
     * (§6.159). A docked pass that dispatched only on a key press left a countdown sitting at 15.
     */
    TEST_METHOD(ADockedPassWithNoKeyStillRunsTheCountdown)
    {
      Bare bare;
      bare.game.Reset();

      // 6502: QQ22 -- the countdown `hyp` starts, set here rather than by pressing "H" so that the
      // assertion is about the PASS rather than about the dispatch that begins one.
      bare.game.State().status.hyperspaceCountdown = 15u;
      bare.game.State().status.hyperspaceCounter = 1u;

      static_cast<void>(bare.game.StepDocked(0u));

      Assert::AreEqual<std::uint32_t>(14u, bare.game.State().status.hyperspaceCountdown, L"a pass with no key still ticks TT107's counter");
    }

    /*
     * 6502: MLOOP part 5 on a docked pass -- the Trumbles breed and the delay is asked for
     * (InputTimer.md T-2).
     *
     * `RunLoopTail` is compared against `MLOOP` on docked views with Trumbles aboard in
     * `GameLoopTests`; what nothing compared until T-2 was whether a DOCKED PASS reached it, and
     * it did not: the port ran the two countdowns and nothing below them, so a commander who
     * docked with a Trumble had a hold that stopped breeding at the airlock. The syncs the pass
     * asks for are the gate `LDA QQ11 / AND PATG / LSR A / BCS plus13` -- bit 0 of the view ANDed
     * with the option -- so a chart, whose view byte is even, waits even with the names on.
     */
    TEST_METHOD(ADockedPassBreedsTheTrumblesAndAsksForItsSyncs)
    {
      Bare bare;
      bare.game.Reset();
      Elite::Universe& universe = bare.game.State();

      universe.commander.tribbles.lo = 200u;
      universe.commander.tribbles.hi = 1u;
      universe.options.authorNames = 0u;

      // 6502: RAND -- a generator with something in it. A bare fixture's is four zeroes, and DORND
      // over zeroes with the carry clear rolls zero for ever, which is a state the machine is never
      // in: the loader leaves RAND with whatever the disk read left there.
      universe.rng.SetState({0x21u, 0x84u, 0x5Fu, 0xC0u});

      std::uint32_t syncs = 0;
      for (std::uint32_t pass = 0; pass < 64u; ++pass)
      {
        syncs += bare.game.StepDocked(0u);
      }
      Assert::AreEqual<std::uint32_t>(128u, syncs, L"6502: LDY #2 / JSR DELAY -- two syncs a pass with the names off");
      Assert::IsTrue(universe.commander.tribbles.hi > 1u || universe.commander.tribbles.lo != 200u,
                     L"6502: DORND / CMP #220 / ADC #0 -- sixty-four passes breed at least one Trumble");

      /*
       * 6502: AND PATG / LSR A / BCS plus13 -- bit 0 of the VIEW byte ANDed with the option. Only one
       * docked view has an odd byte, the Data on System screen at 1, so the names lift the wait
       * there and nowhere else: the status screen is 8, the charts 64 and 128, and all of them wait
       * with the names on. Read off the constants rather than assumed; the first draft of this test
       * had the status screen lifting it.
       */
      universe.options.authorNames = 0xFFu;
      universe.view = Elite::DATA_ON_SYSTEM_VIEW;
      Assert::AreEqual<std::uint32_t>(0u, bare.game.StepDocked(0u), L"names on, on the one odd view: no wait");

      universe.view = Elite::INVENTORY_VIEW;
      Assert::AreEqual<std::uint32_t>(2u, bare.game.StepDocked(0u), L"names on, on an even view: the wait stays");
      universe.view = Elite::LONG_RANGE_CHART_VIEW;
      Assert::AreEqual<std::uint32_t>(2u, bare.game.StepDocked(0u), L"names on, on a chart: the wait stays");
      universe.options.authorNames = 0u;
    }

    /*
     * The whole loop, driven as `Run` drives it: a batch of docked passes and then a batch of
     * flight passes, with nothing behind any of the seams.
     *
     * What this catches is the class of defect the executable used to hide -- an object left
     * unattached, a reference bound to the wrong universe, a dispatch that recurses. It runs the
     * passes rather than asserting about them, and a hang or a crash is the failure.
     */
    TEST_METHOD(TheLoopRunsBothHalvesWithoutAPlatform)
    {
      Bare bare;
      bare.game.Reset();

      for (std::uint32_t pass = 0; pass < 64u; ++pass)
      {
        static_cast<void>(bare.game.StepDocked(0u));
      }
      Assert::IsTrue(bare.game.Docked(), L"nothing in a keyless docked pass launches the ship");

      /*
       * 6502: QQ12 -- launched, set DIRECTLY rather than by pressing "1", so that the flight half
       * can be driven without a docked screen having to agree to it first. `QQ11` is left as the
       * docked screen left it, which is why nothing here asserts about the view: this method is
       * about the passes running, and `ChangeView` has its own suite.
       */
      bare.game.State().dockedFlag = 0u;

      std::uint32_t stepped = 0;
      for (std::uint32_t pass = 0; pass < 64u; ++pass)
      {
        if (!bare.game.Step(0u))
        {
          break;
        }
        ++stepped;
      }

      Assert::IsTrue(stepped > 0u, L"the flight half runs");

      /*
       * AND THE BATCH ENDS BY DOCKING, which is the game being right rather than the test being
       * lucky. `RES2` leaves the station in the bubble and this launched by writing `QQ12` instead
       * of by flying out of the slot, so the ship is inside the docking bay: `M%` answers `Docked`
       * within a few dozen frames and `Leave` performs the arrival. That path runs `DOENTRY`, the
       * missions and the status screen, which is most of what `Perform` is -- so a `Game` whose
       * dispatch were unwired would still be flying here.
       */
      Assert::IsTrue(bare.game.Docked(), L"M% answered Docked and Leave performed the arrival");
    }
  };

} // namespace GameLogicTests
