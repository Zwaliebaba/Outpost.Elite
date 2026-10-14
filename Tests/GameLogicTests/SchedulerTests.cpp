#include "pch.h"

#include "Scheduler.h"
#include "Timing.h"

#include <array>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The game's timer: channel 0 in mode 3, divisor 04A9h (InstallTimerInterrupt, 0x00C6).
constexpr std::uint16_t GAME_DIVISOR = 0x04A9;
constexpr Elite::Cycles GAME_PERIOD_CYCLES = GAME_DIVISOR * Elite::CYCLES_PER_PIT_TICK; // 4772

constexpr Elite::Cycles DELAY = Elite::Scheduler::DELIVERY_DELAY_CYCLES;

// A scheduler on its own clock.
class SchedulerRig
{
public:
  SchedulerRig()
    : m_events(m_clock)
  {
  }

  [[nodiscard]] Elite::Scheduler& Events() noexcept
  {
    return m_events;
  }

  void SetClock(Elite::Cycles _cycle) noexcept
  {
    m_clock = _cycle;
  }

  // Moves the clock to _cycle, as the platform does in a wait, and advances the scheduler.
  void MoveTo(Elite::Cycles _cycle) noexcept
  {
    m_clock = _cycle;
    m_events.Advance();
  }

  // MoveTo, then whether the timer's interrupt was requested; if so it is taken and ended, as the game's handler does.
  [[nodiscard]] bool TickedAt(Elite::Cycles _cycle) noexcept
  {
    MoveTo(_cycle);
    if (!m_events.Requested(Elite::Irq::Timer))
    {
      return false;
    }
    (void)m_events.Take();
    m_events.EndOfInterrupt();
    return true;
  }

  // What ReadScanCode (0x7443) does: take the interrupt, read the scan code, pulse the keyboard's hold, end the interrupt.
  [[nodiscard]] std::uint32_t ServiceKeyboard() noexcept
  {
    (void)m_events.Take();
    const std::uint32_t code = m_events.ScanCode();
    m_events.HoldKeyboard(true);
    m_events.HoldKeyboard(false);
    m_events.EndOfInterrupt();
    return code;
  }

private:
  Elite::Cycles m_clock = 0;
  Elite::Scheduler m_events;
};

} // namespace

// What paced time waits for and the two interrupts the game takes (Scheduler, ADR-041): channel 0 of the 8253, the XT keyboard's
// latch and the 8259's two lines, with the rules the game met them by.
TEST_CLASS(SchedulerTests)
{
public:
  // ── The tick ──

  // As the BIOS leaves it: mode 3, divisor 65536, so the first tick is 65536 timer ticks after power-on.
  TEST_METHOD(PowerOnTickRunsAtEighteenHertz)
  {
    SchedulerRig rig;
    const Elite::Cycles first = 65536 * Elite::CYCLES_PER_PIT_TICK;
    Assert::AreEqual(first, rig.Events().NextTickAt(), L"the first is due there");
    Assert::IsFalse(rig.TickedAt(first - 1));
    Assert::IsTrue(rig.TickedAt(first));
    Assert::IsFalse(rig.TickedAt(2 * first - 1));
    Assert::IsTrue(rig.TickedAt(2 * first));
  }

  // The game's divisor: one tick every 1193 timer ticks, 4772 cycles, counted from the timer tick after the count is written.
  TEST_METHOD(GameDivisorTicksEvery4772Cycles)
  {
    SchedulerRig rig;
    rig.SetClock(1000);
    rig.Events().SetTickDivisor(GAME_DIVISOR);

    std::vector<Elite::Cycles> raised;
    for (Elite::Cycles cycle = 1001; cycle <= 1000 + 20 * GAME_PERIOD_CYCLES + 10; ++cycle)
    {
      if (rig.TickedAt(cycle))
      {
        raised.push_back(cycle);
      }
    }
    Assert::AreEqual(std::size_t{20}, raised.size());
    for (std::size_t index = 0; index < raised.size(); ++index)
    {
      // Loaded on timer tick 251 (cycle 1004), so the edges are on timer ticks 251 + 1193k.
      Assert::AreEqual(1004 + (index + 1) * GAME_PERIOD_CYCLES, raised[index]);
    }
  }

  // The clock can stop on any cycle, few of them a multiple of a timer tick. Advancing by such steps must neither lose nor gain
  // time: each tick lands in the step that crosses its edge.
  TEST_METHOD(SubTickRemaindersAreCarriedExactly)
  {
    SchedulerRig rig;
    rig.SetClock(1000);
    rig.Events().SetTickDivisor(GAME_DIVISOR);

    constexpr std::array<Elite::Cycles, 6> STEPS = {7, 13, 3, 23, 2, 9};
    Elite::Cycles cycle = 1000;
    std::size_t raised = 0;
    for (std::size_t step = 0; cycle < 2'000'000; ++step)
    {
      const Elite::Cycles previous = cycle;
      cycle += STEPS[step % STEPS.size()];
      if (rig.TickedAt(cycle))
      {
        ++raised;
        const Elite::Cycles edge = 1004 + raised * GAME_PERIOD_CYCLES;
        Assert::IsTrue(previous < edge && edge <= cycle, L"the tick in the step that crosses its edge");
      }
    }
    Assert::AreEqual(static_cast<std::size_t>((cycle - 1004) / GAME_PERIOD_CYCLES), raised, L"no edge lost or gained");
  }

  // A control word sets a mode 3 output high at once. When it was low, that is a rising edge, and the timer's interrupt is
  // requested.
  TEST_METHOD(ProgrammingALowOutputIsAnEdge)
  {
    SchedulerRig rig;
    const Elite::Cycles lowHalf = 40'000 * Elite::CYCLES_PER_PIT_TICK; // phase 40000 of 65536: low
    Assert::IsFalse(rig.TickedAt(lowHalf));
    Assert::IsFalse(rig.Events().TickOutputAt(lowHalf));
    rig.Events().SetTickDivisor(GAME_DIVISOR);
    Assert::IsTrue(rig.Events().TickOutputAt(lowHalf));
    Assert::IsTrue(rig.TickedAt(lowHalf));
  }

  // ── The keys ──

  TEST_METHOD(DelayIsOneMillisecond)
  {
    Assert::AreEqual(Elite::MicrosecondsToCycles(1'000), DELAY);
    Assert::AreEqual(Elite::Cycles{4772}, DELAY);
  }

  // A code injected with the latch empty arrives DELIVERY_DELAY_CYCLES later, latched, and requests the keyboard's interrupt.
  TEST_METHOD(CodeArrivesAfterTheDelayWithItsInterrupt)
  {
    SchedulerRig rig;
    rig.MoveTo(100);
    rig.Events().Inject(0x1E); // A, make
    Assert::AreEqual(100 + DELAY, rig.Events().NextDeliveryAt(), L"due a delay on");
    rig.MoveTo(100 + DELAY - 1);
    Assert::IsFalse(rig.Events().Requested(Elite::Irq::Keyboard));
    Assert::IsFalse(rig.Events().LatchFull());
    Assert::AreEqual(0x00u, std::uint32_t{rig.Events().ScanCode()}, L"an empty latch reads 0");

    rig.MoveTo(100 + DELAY);
    Assert::IsTrue(rig.Events().Requested(Elite::Irq::Keyboard));
    Assert::IsTrue(rig.Events().LatchFull());
    Assert::AreEqual(0x1Eu, std::uint32_t{rig.Events().ScanCode()});
    Assert::AreEqual(0x1Eu, std::uint32_t{rig.Events().ScanCode()}, L"reading does not empty the latch");
    Assert::AreEqual(Elite::NO_EVENT, rig.Events().NextDeliveryAt(), L"nothing more is due");
  }

  // The latch holds one code until the keyboard is held and let go; the next code comes a delay after.
  TEST_METHOD(NextCodeWaitsForTheAcknowledgePulse)
  {
    SchedulerRig rig;
    rig.Events().Inject(0x1E);
    rig.Events().Inject(0x9E); // A, break
    rig.MoveTo(DELAY);
    Assert::IsTrue(rig.Events().Requested(Elite::Irq::Keyboard));

    rig.MoveTo(50 * DELAY);
    Assert::AreEqual(std::size_t{1}, rig.Events().QueuedCodes(), L"nothing more while the latch is full");
    Assert::AreEqual(0x1Eu, rig.ServiceKeyboard());
    Assert::IsFalse(rig.Events().LatchFull(), L"the pulse empties the latch");
    Assert::IsFalse(rig.Events().Requested(Elite::Irq::Keyboard));

    rig.MoveTo(51 * DELAY - 1);
    Assert::IsFalse(rig.Events().Requested(Elite::Irq::Keyboard));
    rig.MoveTo(51 * DELAY);
    Assert::IsTrue(rig.Events().Requested(Elite::Irq::Keyboard));
    Assert::AreEqual(0x9Eu, rig.ServiceKeyboard());
    Assert::AreEqual(std::size_t{0}, rig.Events().QueuedCodes());
  }

  // While the keyboard is held it delivers nothing.
  TEST_METHOD(HoldingTheKeyboardKeepsItQuiet)
  {
    SchedulerRig rig;
    rig.Events().HoldKeyboard(true);
    rig.Events().Inject(0x1C);
    rig.MoveTo(10 * DELAY);
    Assert::IsFalse(rig.Events().LatchFull());
    rig.Events().HoldKeyboard(false);
    rig.MoveTo(11 * DELAY - 1);
    Assert::IsFalse(rig.Events().LatchFull());
    rig.MoveTo(11 * DELAY);
    Assert::IsTrue(rig.Events().LatchFull());
    Assert::AreEqual(0x1Cu, std::uint32_t{rig.Events().ScanCode()});
  }

  // ── The requests ──

  // Nothing is requested at power-on.
  TEST_METHOD(NothingIsRequestedAtPowerOn)
  {
    SchedulerRig rig;
    Assert::IsFalse(rig.Events().InterruptPending());
    Assert::IsFalse(rig.Events().Requested(Elite::Irq::Timer));
    Assert::IsFalse(rig.Events().Requested(Elite::Irq::Keyboard));
  }

  // The timer outranks the keyboard, and while the timer's is in service the keyboard's waits for its end of interrupt.
  TEST_METHOD(HigherPriorityFirstAndLowerWaitsForTheEnd)
  {
    SchedulerRig rig;
    Elite::Scheduler& events = rig.Events();
    events.Request(Elite::Irq::Keyboard);
    events.Request(Elite::Irq::Timer);
    Assert::IsTrue(events.InterruptPending());
    Assert::IsTrue(events.Take() == Elite::Irq::Timer);
    Assert::IsTrue(events.InService(Elite::Irq::Timer) && !events.InService(Elite::Irq::Keyboard));
    Assert::IsTrue(events.Requested(Elite::Irq::Keyboard) && !events.Requested(Elite::Irq::Timer));
    Assert::IsFalse(events.InterruptPending(), L"the keyboard's is held off while the timer's is in service");

    events.EndOfInterrupt();
    Assert::IsFalse(events.InService(Elite::Irq::Timer));
    Assert::IsTrue(events.InterruptPending());
    Assert::IsTrue(events.Take() == Elite::Irq::Keyboard);
  }

  // The timer's request interrupts the keyboard's in service, and an end of interrupt, non-specific as the game's OUT 20h,20h
  // is, ends the highest in service.
  TEST_METHOD(NestingEndsTheHighestFirst)
  {
    SchedulerRig rig;
    Elite::Scheduler& events = rig.Events();
    events.Request(Elite::Irq::Keyboard);
    Assert::IsTrue(events.Take() == Elite::Irq::Keyboard);
    events.Request(Elite::Irq::Timer);
    Assert::IsTrue(events.InterruptPending(), L"the timer's outranks the keyboard's in service");
    Assert::IsTrue(events.Take() == Elite::Irq::Timer);
    Assert::IsTrue(events.InService(Elite::Irq::Timer) && events.InService(Elite::Irq::Keyboard));

    events.EndOfInterrupt();
    Assert::IsTrue(!events.InService(Elite::Irq::Timer) && events.InService(Elite::Irq::Keyboard), L"the timer's, the highest, ends");
    events.Request(Elite::Irq::Keyboard);
    Assert::IsFalse(events.InterruptPending(), L"the keyboard's again waits for its own end");
    events.EndOfInterrupt();
    Assert::IsFalse(events.InService(Elite::Irq::Keyboard));
    Assert::IsTrue(events.InterruptPending());
  }

  // A second request while the first is still pending is not queued; one after it is taken is.
  TEST_METHOD(SecondRequestWhilePendingIsNotQueued)
  {
    SchedulerRig rig;
    Elite::Scheduler& events = rig.Events();
    events.Request(Elite::Irq::Timer);
    events.Request(Elite::Irq::Timer);
    Assert::IsTrue(events.Take() == Elite::Irq::Timer);
    Assert::IsFalse(events.InterruptPending());
    events.EndOfInterrupt();
    Assert::IsFalse(events.InterruptPending(), L"nothing was queued behind the first");

    events.Request(Elite::Irq::Timer);
    Assert::IsTrue(events.Take() == Elite::Irq::Timer);
    events.Request(Elite::Irq::Timer);
    Assert::IsTrue(events.Requested(Elite::Irq::Timer), L"a request while in service is latched again");
    Assert::IsFalse(events.InterruptPending(), L"but waits for the end of interrupt");
  }
};

} // namespace GameLogicTests
