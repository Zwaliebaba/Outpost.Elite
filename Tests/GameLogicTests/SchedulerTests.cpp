#include "pch.h"

#include "Presentation.h"
#include "Scheduler.h"

#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * The scheduler: one supply of cycles, two counters, and the clamp
 * (Design/Platform.md §3.2, slice T-1; Design/Platform-Build.md §2.1).
 *
 * WHAT THIS PINS THAT NOTHING ELSE CAN. The four `double` accumulators it replaces were spread
 * across `Main.cpp` and `Shell.cpp`, and two of the four lived inside loops that call `Present` --
 * so the only machine that could run them was a Windows one with a display attached, and the only
 * way to see a backlog rule misbehave was to play the game on a fast panel and notice. Every rule
 * is in this object now and every rule is integer, so both CI legs check them.
 *
 * IT IS NOT A FIDELITY TEST AND SAYS SO. There is no original to compare against: the C64 had no
 * scheduler, it had a main loop that ran flat out and three routines that waited for the raster
 * (§6.17). What the numbers below assert is that the port spends the machine's time the way the
 * cost model says the machine spent it -- which is a claim about this design, measured, and
 * `ShellTests` is where the cost model's own rows are pinned to the measurements they came from.
 */
namespace GameLogicTests
{

  namespace
  {
    /// A flight frame with the bubble empty and with it full -- the two ends of the cost model.
    constexpr std::uint32_t EMPTY_BUBBLE = 47'784;
    constexpr std::uint32_t FULL_BUBBLE = 293'354;

    /// The docked pass that waits for nothing, which is the cheapest step in the game: the Data on
    /// System screen with the author names on, where `QQ11 AND PATG` comes out odd and `DELAY` is
    /// skipped (InputTimer.md T-0). It is the case the catch-up budget exists to keep fast.
    constexpr std::uint32_t CHEAP_PASS = 4'472;
  } // namespace

  TEST_CLASS(TheScheduler)
  {
  public:
    /*
     * Nothing in, nothing out -- and the negative case beside it, because a clock that goes
     * backwards is a real thing on a machine that has just resumed and running the accumulators
     * backwards would be worse than ignoring it.
     */
    TEST_METHOD(NoTimeNoSteps)
    {
      Outpost::Scheduler scheduler{Outpost::MachineTiming::Ntsc()};

      scheduler.Feed(0, true);
      Assert::AreEqual(0, scheduler.TakeSteps(EMPTY_BUBBLE), L"no time, no steps");
      Assert::AreEqual(0, scheduler.TakeBlanks(), L"and no blanks");
      Assert::AreEqual(0u, scheduler.TakeStalls(), L"and no stall");

      scheduler.Feed(-5'000'000, true);
      Assert::AreEqual(0, scheduler.TakeSteps(EMPTY_BUBBLE), L"a clock that went backwards adds nothing");
      Assert::AreEqual(std::int64_t{0}, scheduler.PendingStepCycles(), L"and does not unwind what was there");

      // And a cost of zero answers zero rather than dividing by it.
      scheduler.Feed(1'000'000, true);
      Assert::AreEqual(0, scheduler.TakeSteps(0u), L"a step that costs nothing is not a step");
    }

    /*
     * The remainder carries, which is the whole reason there is an accumulator rather than a test
     * against the elapsed time of one turn.
     */
    TEST_METHOD(AStepLandsOnTheSecondCallAndTheRemainderCarries)
    {
      Outpost::Scheduler scheduler{Outpost::MachineTiming::Ntsc()};

      scheduler.Feed(EMPTY_BUBBLE * 6 / 10, true);
      Assert::AreEqual(0, scheduler.TakeSteps(EMPTY_BUBBLE), L"six tenths of a step is not a step");

      scheduler.Feed(EMPTY_BUBBLE * 6 / 10, true);
      Assert::AreEqual(1, scheduler.TakeSteps(EMPTY_BUBBLE), L"and the remainder carries into the next turn");

      // A fifth of a step over, give or take the two truncations above.
      Assert::IsTrue(scheduler.PendingStepCycles() > 0 && scheduler.PendingStepCycles() < EMPTY_BUBBLE,
                     L"leaving part of a step behind, and less than a whole one");
    }

    /*
     * THE CLAMP IS BY TIME AND NOT BY COUNT, which is the rule `PlanSteps` did not have and the
     * reason this test is not the one Design/Platform-Build.md §2.1 named.
     *
     * The plan's row asked for `ThreePeriodsAreThreeSteps`, which is `PlanSteps`' behaviour: four
     * steps per call whatever a step costs. That is exactly what the new clamp refuses -- four
     * flight frames of a full bubble is 1.17 seconds of game time run between two presents -- so
     * the test that says what the object does is this one, and the departure is journaled.
     *
     * Both halves matter and they pull opposite ways. A step dearer than the budget must still be
     * taken, or a fight would stop the game; a step far cheaper than the budget must be allowed to
     * catch up, or the one docked screen that runs at 228 passes a second would run at the
     * display's rate instead.
     */
    TEST_METHOD(TheCatchUpIsBoundedByTimeAndTheFirstStepIsAlwaysTaken)
    {
      const Outpost::MachineTiming ntsc = Outpost::MachineTiming::Ntsc();
      const std::int64_t budget = static_cast<std::int64_t>(Outpost::MAX_CATCH_UP_FRAMES) * ntsc.cyclesPerFrame;

      // A cheap step catches up many at a time, and stops inside the budget.
      {
        Outpost::Scheduler scheduler{ntsc};
        scheduler.Feed(CHEAP_PASS * 40, true);
        const int steps = scheduler.TakeSteps(CHEAP_PASS);

        Assert::IsTrue(steps > 1, (L"a cheap pass catches up, and took " + std::to_wstring(steps)).c_str());
        Assert::IsTrue(static_cast<std::int64_t>(steps - 1) * CHEAP_PASS <= budget, L"the catch-up stays inside the budget");
        Assert::IsTrue(static_cast<std::int64_t>(steps + 1) * CHEAP_PASS > budget, L"and takes as much of it as fits");
        Assert::AreEqual(15, steps, L"which is fifteen passes of 4,472 cycles in four frames of 17,095");
      }

      // A step dearer than the whole budget is taken anyway -- once.
      {
        Outpost::Scheduler scheduler{ntsc};
        Assert::IsTrue(static_cast<std::int64_t>(FULL_BUBBLE) > budget, L"a full bubble's frame is dearer than the budget");

        scheduler.Feed(FULL_BUBBLE * 4, true);
        Assert::AreEqual(1, scheduler.TakeSteps(FULL_BUBBLE), L"one frame, not four: a stall costs a frame and never a burst");
      }
    }

    /*
     * The backlog that outlives the clamp is dropped AND SAID.
     *
     * ADR-005 §3 asks for a stall to be logged and `Main.cpp` read the flag and discarded it, which
     * is the state this replaces: `(void)plan.stalled`. The counter is what the title bar shows in
     * a debug build.
     */
    TEST_METHOD(ALongGapYieldsOneStepAndAStall)
    {
      Outpost::Scheduler scheduler{Outpost::MachineTiming::Ntsc()};

      // Thirty seconds: a breakpoint, or a lid.
      scheduler.Feed(30 * static_cast<std::int64_t>(Outpost::MachineTiming::Ntsc().clockHz), true);

      Assert::AreEqual(1, scheduler.TakeSteps(FULL_BUBBLE), L"one frame out of thirty seconds");
      Assert::AreEqual(1u, scheduler.TakeStalls(), L"and it says the rest was dropped");
      Assert::AreEqual(0u, scheduler.TakeStalls(), L"reading the count clears it");
      Assert::AreEqual(std::int64_t{0}, scheduler.PendingStepCycles(), L"the backlog is dropped, not carried");

      scheduler.Feed(0, true);
      Assert::AreEqual(0, scheduler.TakeSteps(FULL_BUBBLE), L"so the next turn starts clean");

      // The blanks are dropped with it, and no second stall is counted for the same gap.
      {
        Outpost::Scheduler blanks{Outpost::MachineTiming::Ntsc()};
        blanks.Feed(30 * static_cast<std::int64_t>(Outpost::MachineTiming::Ntsc().clockHz), true);
        Assert::AreEqual(Outpost::MAX_CATCH_UP_FRAMES, blanks.TakeBlanks(), L"four blanks at most out of thirty seconds");
        Assert::AreEqual(std::int64_t{0}, blanks.PendingBlankCycles(), L"and the rest dropped, so a DELAY after a stall still waits");
      }
    }

    /*
     * `DELAY` counts the MACHINE's vertical syncs and not the player's monitor, which is the defect
     * this slice exists to remove (Design/Archive/InputTimer.md T-1).
     *
     * On the tree this replaces, `WaitFrames(n)` was n presents: `dn2`'s fifty-frame pause was
     * 0.83 seconds on a 60 Hz panel, 0.35 on a 144 Hz one and 0.30 on a 165 Hz one. Here the turns
     * are deliberately uneven -- a panel that jitters, a turn that took two -- and the count comes
     * out the same either way, because it is the elapsed CYCLES that are counted and not the calls.
     */
    TEST_METHOD(FiftyBlanksAreFiveSixthsOfASecondAtNtscAndOneAtPal)
    {
      for (const Outpost::MachineTiming timing : {Outpost::MachineTiming::Ntsc(), Outpost::MachineTiming::Pal()})
      {
        Outpost::Scheduler scheduler{timing};

        // One second of the machine's own time, delivered in sixty uneven turns -- none of them a
        // whole blank, some of them two.
        const std::int64_t second = static_cast<std::int64_t>(timing.clockHz);
        std::int64_t fed = 0;
        int blanks = 0;
        for (int turn = 0; turn < 60; ++turn)
        {
          const std::int64_t jitter = ((turn % 5) - 2) * (second / 600);
          std::int64_t piece = (second / 60) + jitter;
          if (turn == 59)
          {
            piece = second - fed; // the last turn makes the total exactly one second
          }
          fed += piece;
          scheduler.Feed(piece, true);
          blanks += scheduler.TakeBlanks();
        }

        Assert::AreEqual(second, fed, L"the turns add up to one second of the machine");

        // 59.83 blanks a second on NTSC, 50.12 on PAL -- the seconds' worth, to the blank.
        const int expected = static_cast<int>(second / timing.cyclesPerFrame);
        Assert::IsTrue(blanks == expected || blanks == expected - 1,
                       (L"a second is about " + std::to_wstring(expected) + L" blanks, not " + std::to_wstring(blanks)).c_str());
      }

      // And the headline the other way round: fifty blanks is five sixths of a second on the
      // machine this port is, and a whole one on the machine it is not.
      const Outpost::MachineTiming ntsc = Outpost::MachineTiming::Ntsc();
      const Outpost::MachineTiming pal = Outpost::MachineTiming::Pal();
      Assert::AreEqual(0.8358, 50.0 * ntsc.cyclesPerFrame / ntsc.clockHz, 0.001, L"fifty blanks, NTSC");
      Assert::AreEqual(0.9976, 50.0 * pal.cyclesPerFrame / pal.clockHz, 0.001, L"fifty blanks, PAL");
    }

    /*
     * The auto-pause: an inactive window plans nothing and banks nothing
     * (Design/Platform.md §3.2, Design/Archive/InputTimer.md §5.9 item 2).
     *
     * The banking half is the half worth a test. A scheduler that merely stopped STEPPING while the
     * window was away would come back holding a minute of cycles and run the clamp out over the
     * next few turns; this one comes back where it was, on a whole step.
     */
    TEST_METHOD(AnInactiveTurnPlansNothingAndBanksNothing)
    {
      Outpost::Scheduler scheduler{Outpost::MachineTiming::Ntsc()};

      // Half a step in hand, and then the player alt-tabs away for a minute.
      scheduler.Feed(EMPTY_BUBBLE / 2, true);
      scheduler.Feed(60 * static_cast<std::int64_t>(Outpost::MachineTiming::Ntsc().clockHz), false);

      Assert::AreEqual(0, scheduler.TakeSteps(EMPTY_BUBBLE), L"an inactive window steps nothing");
      Assert::AreEqual(0, scheduler.TakeBlanks(), L"and waits nothing");
      Assert::AreEqual(0u, scheduler.TakeStalls(), L"a pause is not a stall");
      Assert::AreEqual(std::int64_t{0}, scheduler.PendingStepCycles(), L"and the half step in hand went with it");

      // Back, and the next whole step's worth of attention is a whole step.
      scheduler.Feed(EMPTY_BUBBLE, true);
      Assert::AreEqual(1, scheduler.TakeSteps(EMPTY_BUBBLE), L"and it resumes on a whole step");
    }

    /*
     * The integer curves are the `double` ones, to a cycle.
     *
     * THIS TEST IS DELIBERATELY TEMPORARY and goes with `TitleTurnSeconds`, `FlightFrameSeconds`
     * and `DockedPassSeconds` in the commit that switches the loop over. It exists for one turn of
     * the crank: to prove that what replaced the arithmetic computes the same numbers, over the
     * whole input space of both curves rather than at the rows.
     */
    TEST_METHOD(TheCycleTablesAgreeWithTheSecondsTheyReplace)
    {
      for (int ships = 0; ships <= 255; ++ships)
      {
        const auto count = static_cast<std::uint8_t>(ships);
        const double seconds = Outpost::FlightFrameSeconds(count);
        const double asCycles = seconds * Outpost::NTSC_CLOCK_HZ;
        const double drift = static_cast<double>(Outpost::FlightFrameCycles(count)) - asCycles;
        Assert::IsTrue(drift > -1.0 && drift < 1.0, (L"the flight curve, at " + std::to_wstring(ships) + L" ships").c_str());
      }

      for (int distance = 0; distance <= 255; ++distance)
      {
        const auto high = static_cast<std::uint8_t>(distance);
        const double seconds = Outpost::TitleTurnSeconds(high);
        const double asCycles = seconds * Outpost::NTSC_CLOCK_HZ;
        const double drift = static_cast<double>(Outpost::TitleTurnCycles(high)) - asCycles;
        Assert::IsTrue(drift > -1.0 && drift < 1.0, (L"the title curve, at " + std::to_wstring(distance)).c_str());
      }

      // The docked pass, both answers `RunLoopTail` gives, on the machine the seconds form assumed.
      for (const std::uint8_t syncs : {std::uint8_t{0}, Outpost::DOCKED_PASS_SYNCS})
      {
        const double asCycles = Outpost::DockedPassSeconds(syncs) * Outpost::NTSC_CLOCK_HZ;
        const double drift = static_cast<double>(Outpost::DockedPassCycles(syncs, Outpost::MachineTiming::Ntsc())) - asCycles;
        Assert::IsTrue(drift > -1.0 && drift < 1.0, L"the docked pass");
      }

      /*
       * AND THE ONE PLACE THE TWO DO NOT AGREE, which is the defect the cycle form fixes rather
       * than a disagreement to reconcile: the seconds form priced a docked pass's syncs at the NTSC
       * frame whatever machine was selected, because it had no machine to ask. On PAL a sync is
       * 19,656 cycles rather than 17,095, so the pass is longer -- which is what `wscan.asm` says
       * and what `DockedPassSeconds` could not express.
       */
      Assert::IsTrue(Outpost::DockedPassCycles(Outpost::DOCKED_PASS_SYNCS, Outpost::MachineTiming::Pal()) >
                       Outpost::DockedPassCycles(Outpost::DOCKED_PASS_SYNCS, Outpost::MachineTiming::Ntsc()),
                     L"a PAL sync is longer than an NTSC one, which the seconds form could not say");
    }

    /*
     * The property the single-call assertions cannot see: over a long run the steps taken track the
     * time that passed, and the accumulator does not drift.
     *
     * `ShellTests::TheStepPlannerNeverSkipsOrDoublesSilently` carried this for `PlanSteps` and it
     * moves here with the object. A hundred seconds at sixty turns a second, against a step that is
     * not a whole number of turns, is where a remainder that was being thrown away would show.
     */
    TEST_METHOD(ASteadyRunNeitherGainsNorLosesSteps)
    {
      const Outpost::MachineTiming ntsc = Outpost::MachineTiming::Ntsc();
      Outpost::Scheduler scheduler{ntsc};

      // Sixty turns a second for a hundred seconds, against the empty bubble's 21.4 frames a second.
      const std::int64_t perTurn = static_cast<std::int64_t>(ntsc.clockHz) / 60;
      constexpr int TURNS = 6000;

      int steps = 0;
      for (int turn = 0; turn < TURNS; ++turn)
      {
        scheduler.Feed(perTurn, true);
        steps += scheduler.TakeSteps(EMPTY_BUBBLE);
      }

      Assert::AreEqual(0u, scheduler.TakeStalls(), L"a steady sixty turns a second never stalls at twenty-one steps");

      const int expected = static_cast<int>(static_cast<std::int64_t>(TURNS) * perTurn / EMPTY_BUBBLE);
      Assert::IsTrue(steps == expected || steps == expected - 1,
                     (L"a hundred seconds should be about " + std::to_wstring(expected) + L" steps, not " + std::to_wstring(steps)).c_str());
    }
  };

} // namespace GameLogicTests
