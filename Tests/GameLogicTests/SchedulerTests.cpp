#include "pch.h"

#include "Scheduler.h"
#include "Timing.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr Elite::KeyEvent A_DOWN{.key = 0x1E, .down = true};
constexpr Elite::KeyEvent A_UP{.key = 0x1E, .down = false};

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

  void SetClock(Elite::Milliseconds _time) noexcept
  {
    m_clock = _time;
  }

  // Moves the clock to _time, as the platform does in a wait, and advances the scheduler.
  void MoveTo(Elite::Milliseconds _time) noexcept
  {
    m_clock = _time;
    m_events.Advance();
  }

  // MoveTo, then whether the timer's interrupt was requested; if so it is taken and ended, as the game's handler does.
  [[nodiscard]] bool TickedAt(Elite::Milliseconds _time) noexcept
  {
    MoveTo(_time);
    if (!m_events.Requested(Elite::Irq::Timer))
    {
      return false;
    }
    (void)m_events.Take();
    m_events.EndOfInterrupt();
    return true;
  }

  // What ReadScanCode does: take the interrupt, take the event, end the interrupt.
  [[nodiscard]] Elite::KeyEvent ServiceKeyboard() noexcept
  {
    (void)m_events.Take();
    const Elite::KeyEvent event = m_events.TakeKey();
    m_events.EndOfInterrupt();
    return event;
  }

private:
  Elite::Milliseconds m_clock = 0;
  Elite::Scheduler m_events;
};

[[nodiscard]] bool Same(const Elite::KeyEvent& _left, const Elite::KeyEvent& _right) noexcept
{
  return _left.key == _right.key && _left.down == _right.down;
}

} // namespace

// What time waits for and the two interrupts the game takes (Scheduler, D87, D88): the millisecond tick, key events one a
// millisecond, and the two requests' rules.
TEST_CLASS(SchedulerTests)
{
public:
  // ── The tick ──

  // At power-on the tick is stopped: nothing is due.
  TEST_METHOD(TickIsStoppedAtPowerOn)
  {
    SchedulerRig rig;
    Assert::IsFalse(rig.Events().TickRunning());
    Assert::AreEqual(Elite::NO_EVENT, rig.Events().NextTickAt());
    Assert::IsFalse(rig.TickedAt(1'000));
  }

  // Started, the tick is due at every whole millisecond after; stopped, at none.
  TEST_METHOD(TickRunsEveryMillisecondOnceStarted)
  {
    SchedulerRig rig;
    rig.SetClock(1'000);
    rig.Events().SetTickRunning(true);
    Assert::AreEqual(Elite::Milliseconds{1'001}, rig.Events().NextTickAt(), L"the first a millisecond on");
    for (Elite::Milliseconds time = 1'001; time <= 1'020; ++time)
    {
      Assert::IsTrue(rig.TickedAt(time));
    }
    rig.Events().SetTickRunning(false);
    Assert::IsFalse(rig.TickedAt(1'021));
    Assert::AreEqual(Elite::NO_EVENT, rig.Events().NextEventAt());
  }

  // ── The keys ──

  // An event queued with the latch empty arrives at the next millisecond, latched, and requests the keyboard's interrupt.
  TEST_METHOD(EventArrivesAtTheNextMillisecondWithItsInterrupt)
  {
    SchedulerRig rig;
    rig.MoveTo(100);
    rig.Events().Inject(A_DOWN);
    Assert::AreEqual(Elite::Milliseconds{101}, rig.Events().NextDeliveryAt(), L"due a millisecond on");
    Assert::IsFalse(rig.Events().LatchFull());

    rig.MoveTo(101);
    Assert::IsTrue(rig.Events().Requested(Elite::Irq::Keyboard));
    Assert::IsTrue(rig.Events().LatchFull());
    Assert::AreEqual(Elite::NO_EVENT, rig.Events().NextDeliveryAt(), L"nothing more is due");
    Assert::IsTrue(Same(A_DOWN, rig.ServiceKeyboard()));
  }

  // The latch holds one event until the handler takes it; the next comes a millisecond after, in order (D88).
  TEST_METHOD(NextEventWaitsForTheLatchToEmpty)
  {
    SchedulerRig rig;
    rig.Events().Inject(A_DOWN);
    rig.Events().Inject(A_UP);
    rig.MoveTo(1);
    Assert::IsTrue(rig.Events().Requested(Elite::Irq::Keyboard));

    rig.MoveTo(50);
    Assert::AreEqual(std::size_t{1}, rig.Events().QueuedEvents(), L"nothing more while the latch is full");
    Assert::IsTrue(Same(A_DOWN, rig.ServiceKeyboard()));
    Assert::IsFalse(rig.Events().LatchFull(), L"taking empties the latch");
    Assert::AreEqual(Elite::Milliseconds{51}, rig.Events().NextDeliveryAt());

    rig.MoveTo(51);
    Assert::IsTrue(rig.Events().Requested(Elite::Irq::Keyboard));
    Assert::IsTrue(Same(A_UP, rig.ServiceKeyboard()));
    Assert::AreEqual(std::size_t{0}, rig.Events().QueuedEvents());
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
