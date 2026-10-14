#include "pch.h"

#include "Cga.h"
#include "Host.h"
#include "Input.h"
#include "HostRig.h"
#include "StateDigest.h"
#include "Timing.h"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// sti, the tick started; then wait for ever, each poll finding nothing: what the program's idle loop does.
Elite::Task<void> WaitForEver(Elite::Host& _host)
{
  _host.SetTickRunning(true);
  _host.EnableInterrupts();
  for (;;)
  {
    co_await _host.Wait();
  }
}

// The tick started; then waits, a poll at a time with interrupts on, until _count interrupts have been taken.
Elite::Task<void> WaitForInterrupts(HostRig& _rig, std::uint64_t _count)
{
  _rig.Host().SetTickRunning(true);
  _rig.Host().EnableInterrupts();
  while (_rig.Host().InterruptsTaken() < _count)
  {
    co_await _rig.Host().Wait();
  }
}

// Sets its flag when it is destroyed: whether the program's coroutine was destroyed with what it held.
class UnwindWitness
{
public:
  explicit UnwindWitness(bool* _unwound) noexcept
    : m_unwound(_unwound)
  {
  }

  UnwindWitness(const UnwindWitness&) = delete;
  UnwindWitness& operator=(const UnwindWitness&) = delete;

  ~UnwindWitness()
  {
    *m_unwound = true;
  }

private:
  bool* m_unwound;
};

} // namespace

// The host (Host, D33, D90): paced time in milliseconds (D87), interrupts, the program as a coroutine, the handlers by
// identity, and the host's defaults where the game has none.
TEST_CLASS(HostTests)
{
public:
  // The whole chain: the tick, once started, raises the timer's interrupt every millisecond, the controller grants it, and with
  // none of the game's handlers in place the default ends it. The program waits with interrupts on.
  TEST_METHOD(TickReachesTheDefaultHandler)
  {
    HostRig rig("PlatformTimerTick");
    rig.Start([&rig]() -> Elite::Task<void> { co_await WaitForEver(rig.Host()); });
    Assert::IsTrue(rig.Host().RunUntil(11) == Elite::StopReason::Reached, L"a tick at the run's end is the next run's");
    Assert::AreEqual(std::uint64_t{10}, rig.Host().InterruptsTaken(), L"one a millisecond, each ended");
    Assert::IsFalse(rig.Host().Events().InService(Elite::Irq::Timer));
  }

  // The program's start returning is its far return to the PSP, which is DOS's int 20h: the program ends.
  TEST_METHOD(ProgramEndsWhenItsStartReturns)
  {
    HostRig rig("PlatformTerminate");
    rig.Start([]() -> Elite::Task<void> { co_return; });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock(), L"it took no time");
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls);
    Assert::IsTrue(rig.Host().RunUntil(2'000) == Elite::StopReason::Terminated, L"and stays ended");
  }

  // Paced time (ADR-008): a loop that counts is work, and work takes no time: a turn that waits for nothing never idles.
  TEST_METHOD(PacedWorkTakesNoTime)
  {
    HostRig rig("PlatformPacedWork");
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
        for (std::uint16_t count = 1000; count != 0;)
        {
          --count;
          rig.Host().Spin();
        }
        co_return;
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock(), L"no time passed");
  }

  // A loop that waits for interrupts finds nothing at each poll, so the clock moves from event to event until the one it
  // waits for, and the loop ends right there: the fifth tick, at 5 ms.
  TEST_METHOD(PacedWaitEndsAtTheEventItWaitsFor)
  {
    HostRig rig("PlatformPacedWait");
    rig.Start([&rig]() -> Elite::Task<void> { co_await WaitForInterrupts(rig, 5); });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{5}, rig.Host().Clock());
  }

  // An idle loop with interrupts off waits for ever, as on the real machine; the run reaches its end. cli; jmp $.
  TEST_METHOD(PacedIdleLoopRunsToTheLimit)
  {
    HostRig rig("PlatformPacedLimit");
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
        rig.Host().DisableInterrupts();
        for (;;)
        {
          co_await rig.Host().Wait();
        }
      });
    Assert::IsTrue(rig.Host().RunUntil(500) == Elite::StopReason::Reached);
    Assert::AreEqual(std::uint64_t{500}, rig.Host().Clock());
  }

  // A loop that never waits would hold paced time still for ever: the run stops as Spinning.
  TEST_METHOD(PacedSpinIsStopped)
  {
    HostRig rig("PlatformPacedSpin");
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
        rig.Host().EnableInterrupts();
        for (;;)
        {
          rig.Host().Spin();
        }
        co_return;
      });
    rig.Host().SetSpinLimit(10'000);
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Spinning);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock());
  }

  // Two runs of the same program to the same moment end in the same state: the same interrupts taken and the same game state.
  TEST_METHOD(RunsAreIdenticalToTheMillisecond)
  {
    std::array<std::uint64_t, 2> clocks{};
    std::array<std::uint64_t, 2> interrupts{};
    std::array<std::string, 2> digests;
    for (std::size_t run = 0; run < 2; ++run)
    {
      HostRig rig("PlatformRepeat");
      rig.Start([&rig]() -> Elite::Task<void> { co_await WaitForEver(rig.Host()); });
      Assert::IsTrue(rig.Host().RunUntil(1'001) == Elite::StopReason::Reached);
      clocks[run] = rig.Host().Clock();
      interrupts[run] = rig.Host().InterruptsTaken();
      digests[run] = Elite::GameStateDigest(rig.State(), rig.Host().Video());
    }
    Assert::AreEqual(clocks[0], clocks[1]);
    Assert::AreEqual(std::uint64_t{1'000}, interrupts[0], L"a thousand ticks in a second");
    Assert::AreEqual(interrupts[0], interrupts[1]);
    Assert::IsTrue(digests[0] == digests[1], L"the same game state");
  }

  // D77: the program waits as a coroutine, is suspended where a run ends, and is resumed in the next.
  TEST_METHOD(AWaitStopsAtTheEndOfARunAndCarriesOn)
  {
    HostRig rig("PlatformWaits");
    rig.Start([&rig]() -> Elite::Task<void> { co_await WaitForInterrupts(rig, 500); });
    Assert::IsTrue(rig.Host().RunUntil(100) == Elite::StopReason::Reached, L"the run ends inside the wait");
    Assert::AreEqual(std::uint64_t{100}, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls);
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{500}, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls, L"one call, over two runs");
  }

  // A platform destroyed while the program waits destroys its coroutine, and every routine it waits in, and does not hang.
  TEST_METHOD(PowerOffMidWaitDestroysTheProgram)
  {
    HostRig rig("PlatformAbandoned");
    bool unwound = false;
    rig.Start(
      [&rig, &unwound]() -> Elite::Task<void>
      {
        const UnwindWitness witness(&unwound);
        co_await WaitForEver(rig.Host());
      });
    Assert::IsTrue(rig.Host().RunUntil(100) == Elite::StopReason::Reached);
    Assert::IsFalse(unwound, L"still waiting");
    rig.PowerOff();
    Assert::IsTrue(unwound, L"unwound");
  }

  TEST_METHOD(TwoRoutinesAtOneEntryAreRefused)
  {
    HostRig rig("PlatformTwice");
    rig.Host().SetGameHandler(Elite::Irq::Timer, [] {});
    Assert::ExpectException<std::logic_error>([&rig] { rig.Host().SetGameHandler(Elite::Irq::Timer, [] {}); }, L"a handler");
    rig.Start([]() -> Elite::Task<void> { co_return; });
    Assert::ExpectException<std::logic_error>(
      [&rig]
      {
        rig.Start([]() -> Elite::Task<void> { co_return; });
      },
      L"the program's start");
  }

  // A handler that names code no routine stands in for: the first interrupt taken there is refused.
  TEST_METHOD(AStrayHandlerIsRefused)
  {
    HostRig rig("PlatformStray");
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
        rig.Host().SetHandler(Elite::Irq::Timer, Elite::IrqHandler::Stray);
        co_await WaitForEver(rig.Host());
      });
    Assert::ExpectException<std::logic_error>([&rig] { (void)rig.Host().RunUntil(1'000); }, L"at the first tick");
  }

  // The game's own tick handler: the timer's interrupt is taken at each tick while the program waits, with interrupts off
  // inside; the handler's end of interrupt lets the next one in.
  TEST_METHOD(InterruptsAreTakenAtEachTick)
  {
    HostRig rig("PlatformIrq");
    std::vector<Elite::Milliseconds> taken;
    bool enabledInside = false;
    rig.Host().SetGameHandler(Elite::Irq::Timer,
                              [&rig, &taken, &enabledInside]
                              {
                                taken.push_back(rig.Host().Clock());
                                enabledInside = enabledInside || rig.Host().InterruptsEnabled();
                                rig.Host().EndOfInterrupt();
                              });
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
        rig.Host().SetHandler(Elite::Irq::Timer, Elite::IrqHandler::Game);
        co_await WaitForEver(rig.Host());
      });
    Assert::IsTrue(rig.Host().RunUntil(4) == Elite::StopReason::Reached);
    Assert::IsTrue(taken == std::vector<Elite::Milliseconds>{1, 2, 3}, L"at each tick");
    Assert::IsFalse(enabledInside, L"interrupts off in the handler");
    Assert::AreEqual(std::uint64_t{3}, rig.Host().InterruptsTaken());
    Assert::IsFalse(rig.Host().Events().InService(Elite::Irq::Timer), L"every one ended");
    Assert::AreEqual(std::uint64_t{3}, rig.Host().GameHandler(Elite::Irq::Timer).calls);
  }

  // The host's defaults serve the timer's and the keyboard's interrupts at power-on.
  TEST_METHOD(TheDefaultsServeBothInterruptsAtPowerOn)
  {
    HostRig rig("PlatformVectors");
    Assert::IsTrue(rig.Host().Handler(Elite::Irq::Timer) == Elite::IrqHandler::Bios, L"the timer's");
    Assert::IsTrue(rig.Host().Handler(Elite::Irq::Keyboard) == Elite::IrqHandler::Bios, L"the keyboard's");
  }

  // What the game's own test, IsMouseDriverInstalled (CS:02D4), finds: whether a mouse is attached.
  TEST_METHOD(TheGamesMouseCheckSeesWhetherAMouseIsAttached)
  {
    HostRig none("PlatformNoMouse", false);
    Assert::IsFalse(Elite::IsMouseDriverInstalled(none.Host()), L"no mouse");

    HostRig attached("PlatformMouse", true);
    Assert::IsTrue(Elite::IsMouseDriverInstalled(attached.Host()), L"a mouse");
    attached.Host().MouseDevice().SetPresent(false);
    Assert::IsFalse(Elite::IsMouseDriverInstalled(attached.Host()), L"taken away");
  }

  // A key event before the game's keyboard handler is in place is taken and dropped, and the interrupt ended: nothing reaches
  // the game's state, and the next event can arrive.
  TEST_METHOD(TheDefaultKeyboardHandlerDropsTheKey)
  {
    HostRig rig("PlatformDefaultKeyboard");
    Elite::Host& platform = rig.Host();
    platform.Events().Inject(Elite::KeyEvent{.key = 0x1E, .down = true});
    while (!platform.Events().LatchFull())
    {
      platform.Idle(Elite::NO_EVENT);
    }
    const std::string digest = Elite::GameStateDigest(rig.State(), platform.Video());
    platform.TakeDueInterrupts();
    Assert::IsFalse(platform.Events().LatchFull(), L"taken");
    Assert::IsTrue(Elite::GameStateDigest(rig.State(), platform.Video()) == digest, L"nothing buffered");
    Assert::IsFalse(platform.Events().InService(Elite::Irq::Timer) || platform.Events().InService(Elite::Irq::Keyboard), L"ended");
  }

  // The system control port reads back what was written; bits 0-1 reach the speaker, the tone's gate and its data.
  TEST_METHOD(SystemControlReadsBackAndDrivesGateAndSpeaker)
  {
    HostRig rig("PlatformSystemControl");
    Elite::Host& platform = rig.Host();
    Assert::AreEqual(0x4Cu, std::uint32_t{platform.SystemControl()}, L"power-on value");
    const std::size_t events = platform.Sound().Events().size();

    platform.Idle(10);
    platform.SetSystemControl(0x4F);
    Assert::AreEqual(0x4Fu, std::uint32_t{platform.SystemControl()});
    Assert::IsTrue(platform.Sound().Now().tone.gate);
    const Elite::Speaker::Event& on = platform.Sound().Events().back();
    Assert::AreEqual(platform.Clock(), on.at);
    Assert::IsTrue(on.gate && on.data);
    Assert::AreEqual(events + 1, platform.Sound().Events().size(), L"the gate and the bits at one moment are one event");

    platform.Idle(20);
    platform.SetSystemControl(0xCF);
    platform.SetSystemControl(0x4F);
    Assert::AreEqual(events + 1, platform.Sound().Events().size(), L"bit 7 leaves the speaker alone");

    platform.SetSystemControl(0x4D);
    Assert::IsTrue(platform.Sound().Now().tone.gate);
    Assert::IsFalse(platform.Sound().Events().back().data);
  }
};

} // namespace GameLogicTests
