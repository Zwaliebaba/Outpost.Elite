#include "pch.h"

#include "Host.h"
#include "Input.h"
#include "HostRig.h"
#include "StateDigest.h"
#include "Timing.h"

#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The tick started; then wait for ever, each poll finding nothing: what the program's idle loop does.
Elite::Task<void> WaitForEver(Elite::Host& _host)
{
  _host.SetTickRunning(true);
  for (;;)
  {
    co_await _host.Wait();
  }
}

// The tick started; then waits, a poll at a time, until _count ticks have fallen due.
Elite::Task<void> WaitForTicks(HostRig& _rig, std::uint64_t _count)
{
  _rig.Host().SetTickRunning(true);
  while (_rig.Host().TicksRun() < _count)
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

// The host (Host, D33, D90): paced time in milliseconds (D87), the program as a coroutine, and the tick's work and the keys run
// at the game's waits (D91).
TEST_CLASS(HostTests)
{
public:
  // The tick, once started, falls due at every whole millisecond, and the game's tick work runs at each, when the wait goes
  // on: a tick at the run's end is the next run's.
  TEST_METHOD(TickWorkRunsAtEachTick)
  {
    HostRig rig("HostTick");
    std::vector<Elite::Milliseconds> ran;
    rig.Host().SetTickHandler([&rig, &ran] { ran.push_back(rig.Host().Clock()); });
    rig.Start([&rig]() -> Elite::Task<void> { co_await WaitForEver(rig.Host()); });
    Assert::IsTrue(rig.Host().RunUntil(4) == Elite::StopReason::Reached);
    Assert::IsTrue(ran == std::vector<Elite::Milliseconds>{1, 2, 3}, L"at each tick");
    Assert::AreEqual(std::uint64_t{3}, rig.Host().TicksRun());
    Assert::AreEqual(std::uint64_t{3}, rig.Host().TickHandler().calls);
  }

  // Stopped, the tick falls due no more.
  TEST_METHOD(TickStopsWhenTheGameStopsIt)
  {
    HostRig rig("HostTickStops");
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
        co_await WaitForTicks(rig, 5);
        rig.Host().SetTickRunning(false);
        for (;;)
        {
          co_await rig.Host().Wait();
        }
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Reached);
    Assert::AreEqual(std::uint64_t{5}, rig.Host().TicksRun());
  }

  // The program's start returning ends the program.
  TEST_METHOD(ProgramEndsWhenItsStartReturns)
  {
    HostRig rig("HostTerminate");
    rig.Start([]() -> Elite::Task<void> { co_return; });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock(), L"it took no time");
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls);
    Assert::IsTrue(rig.Host().RunUntil(2'000) == Elite::StopReason::Terminated, L"and stays ended");
  }

  // Paced time (ADR-008): a loop that counts is work, and work takes no time: a turn that waits for nothing never idles.
  TEST_METHOD(PacedWorkTakesNoTime)
  {
    HostRig rig("HostPacedWork");
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

  // A loop that waits for ticks finds nothing at each poll, so the clock moves from event to event until the one it waits for,
  // and the loop ends right there: the fifth tick, at 5 ms.
  TEST_METHOD(PacedWaitEndsAtTheEventItWaitsFor)
  {
    HostRig rig("HostPacedWait");
    rig.Start([&rig]() -> Elite::Task<void> { co_await WaitForTicks(rig, 5); });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{5}, rig.Host().Clock());
  }

  // A loop that waits for ever with nothing due, the tick stopped, runs to the run's end.
  TEST_METHOD(PacedIdleLoopRunsToTheLimit)
  {
    HostRig rig("HostPacedLimit");
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
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
    HostRig rig("HostPacedSpin");
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
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

  // Two runs of the same program to the same moment end in the same state: the same ticks and the same game state.
  TEST_METHOD(RunsAreIdenticalToTheMillisecond)
  {
    std::array<std::uint64_t, 2> clocks{};
    std::array<std::uint64_t, 2> ticks{};
    std::array<std::string, 2> digests;
    for (std::size_t run = 0; run < 2; ++run)
    {
      HostRig rig("HostRepeat");
      rig.Start([&rig]() -> Elite::Task<void> { co_await WaitForEver(rig.Host()); });
      Assert::IsTrue(rig.Host().RunUntil(1'001) == Elite::StopReason::Reached);
      clocks[run] = rig.Host().Clock();
      ticks[run] = rig.Host().TicksRun();
      digests[run] = Elite::GameStateDigest(rig.State(), rig.Host().Screen());
    }
    Assert::AreEqual(clocks[0], clocks[1]);
    Assert::AreEqual(std::uint64_t{1'000}, ticks[0], L"a thousand ticks in a second");
    Assert::AreEqual(ticks[0], ticks[1]);
    Assert::IsTrue(digests[0] == digests[1], L"the same game state");
  }

  // D77: the program waits as a coroutine, is suspended where a run ends, and is resumed in the next.
  TEST_METHOD(AWaitStopsAtTheEndOfARunAndCarriesOn)
  {
    HostRig rig("HostWaits");
    rig.Start([&rig]() -> Elite::Task<void> { co_await WaitForTicks(rig, 500); });
    Assert::IsTrue(rig.Host().RunUntil(100) == Elite::StopReason::Reached, L"the run ends inside the wait");
    Assert::AreEqual(std::uint64_t{100}, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls);
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{500}, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls, L"one call, over two runs");
  }

  // A Host destroyed while the program waits destroys its coroutine, and every routine it waits in, and does not hang.
  TEST_METHOD(PowerOffMidWaitDestroysTheProgram)
  {
    HostRig rig("HostAbandoned");
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

  TEST_METHOD(TwoRoutinesForOneJobAreRefused)
  {
    HostRig rig("HostTwice");
    rig.Host().SetTickHandler([] {});
    Assert::ExpectException<std::logic_error>([&rig] { rig.Host().SetTickHandler([] {}); }, L"the tick's");
    rig.Host().SetKeyHandler([](const Elite::KeyEvent&) {});
    Assert::ExpectException<std::logic_error>([&rig] { rig.Host().SetKeyHandler([](const Elite::KeyEvent&) {}); }, L"the keys'");
    rig.Start([]() -> Elite::Task<void> { co_return; });
    Assert::ExpectException<std::logic_error>(
      [&rig]
      {
        rig.Start([]() -> Elite::Task<void> { co_return; });
      },
      L"the program's start");
  }

  // Keys are dropped until the game takes them; then they reach its handler one a millisecond, in order (D88).
  TEST_METHOD(KeysReachTheGameOneAMillisecondInOrder)
  {
    HostRig rig("HostKeys");
    std::vector<std::pair<Elite::Milliseconds, Elite::KeyEvent>> taken;
    rig.Host().SetKeyHandler([&rig, &taken](const Elite::KeyEvent& _event) { taken.emplace_back(rig.Host().Clock(), _event); });
    rig.Start(
      [&rig]() -> Elite::Task<void>
      {
        for (;;)
        {
          co_await rig.Host().Wait();
        }
      });
    rig.Host().Inject(Elite::KeyEvent{.key = 0x10, .down = true});
    Assert::IsTrue(rig.Host().RunUntil(10) == Elite::StopReason::Reached);
    Assert::IsTrue(taken.empty(), L"dropped before the game takes the keys");

    rig.Host().SetKeysToGame(true);
    rig.Host().Inject(Elite::KeyEvent{.key = 0x1E, .down = true});
    rig.Host().Inject(Elite::KeyEvent{.key = 0x1E, .down = false});
    Assert::AreEqual(std::size_t{2}, rig.Host().QueuedKeys());
    Assert::IsTrue(rig.Host().RunUntil(20) == Elite::StopReason::Reached);
    Assert::AreEqual(std::size_t{2}, taken.size());
    Assert::AreEqual(Elite::Milliseconds{11}, taken[0].first, L"the next millisecond");
    Assert::IsTrue(taken[0].second.key == 0x1E && taken[0].second.down, L"the make first");
    Assert::AreEqual(Elite::Milliseconds{12}, taken[1].first, L"then a millisecond on");
    Assert::IsTrue(taken[1].second.key == 0x1E && !taken[1].second.down, L"the break");
    Assert::AreEqual(std::size_t{0}, rig.Host().QueuedKeys());
  }

  // What the game's own test, IsMouseDriverInstalled, finds: whether a mouse is attached.
  TEST_METHOD(TheGamesMouseCheckSeesWhetherAMouseIsAttached)
  {
    HostRig none("HostNoMouse", false);
    Assert::IsFalse(Elite::IsMouseDriverInstalled(none.Host()), L"no mouse");

    HostRig attached("HostMouse", true);
    Assert::IsTrue(Elite::IsMouseDriverInstalled(attached.Host()), L"a mouse");
    attached.Host().MouseDevice().SetPresent(false);
    Assert::IsFalse(Elite::IsMouseDriverInstalled(attached.Host()), L"taken away");
  }

  // The speaker's two bits read back as set and reach the speaker, the tone's gate and its data; the other bits are not the
  // speaker's.
  TEST_METHOD(SpeakerBitsReadBackAndDriveGateAndData)
  {
    HostRig rig("HostSpeakerBits");
    Elite::Host& host = rig.Host();
    Assert::AreEqual(0u, std::uint32_t{host.SpeakerBits()}, L"both clear at power-on");
    const std::size_t events = host.Sound().Events().size();

    host.Idle(10);
    host.SetSpeakerBits(0x4F);
    Assert::AreEqual(0x03u, std::uint32_t{host.SpeakerBits()}, L"the two bits");
    const Elite::Speaker::Event& on = host.Sound().Events().back();
    Assert::AreEqual(host.Clock(), on.at);
    Assert::IsTrue(on.gate && on.data);
    Assert::AreEqual(events + 1, host.Sound().Events().size(), L"the gate and the bits at one moment are one event");

    host.Idle(20);
    host.SetSpeakerBits(0xC3);
    Assert::AreEqual(events + 1, host.Sound().Events().size(), L"the other bits leave the speaker alone");

    host.SetSpeakerBits(0x01);
    Assert::IsTrue(host.Sound().Now().gate);
    Assert::IsFalse(host.Sound().Events().back().data);
  }
};

} // namespace GameLogicTests
