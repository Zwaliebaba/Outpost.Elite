#include "pch.h"

#include "TimeOfDay.h"
#include "Cga.h"
#include "Hardware.h"
#include "Input.h"
#include "Platform.h"
#include "PlatformRig.h"
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

// The first IRQ 0 after power-on, at the BIOS's 18.2 Hz.
constexpr Elite::Cycles FIRST_TICK = 65536 * Elite::CYCLES_PER_PIT_TICK; // 262,144

// What the 8088 takes over the ROM's IRET: Intel's 8086 figure and three words popped over the 8-bit bus.
constexpr Elite::Cycles IRET_CYCLES = 24 + 3 * 4;

// sti; then wait for ever, each poll finding nothing: what the program's idle loop does.
void WaitForEver(Elite::Hardware& _hardware)
{
  _hardware.EnableInterrupts();
  for (;;)
  {
    _hardware.Wait();
  }
}

// Waits for the BIOS's tick count to change, with interrupts on, a poll at a time: sti; mov bx,es:[6Ch]; wait: cmp bx,es:[6Ch];
// je wait.
void WaitForTick(PlatformRig& _rig)
{
  _rig.Native().EnableInterrupts();
  const std::uint32_t ticks = _rig.Host().Day().Ticks();
  while (_rig.Host().Day().Ticks() == ticks)
  {
    _rig.Native().Wait();
  }
}

// Sets its flag when it is destroyed: whether what the native thread was running was unwound.
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

// The native platform (Platform, D33): paced time, interrupts, the native thread, the handlers by identity and the ROM's two
// answers, the BIOS's two hardware handlers, and DOS's console. What the emulated PC's tests held of the Pc, its Dispatcher, its native
// code and its firmware, held of the platform that replaced them.
TEST_CLASS(PlatformTests)
{
public:
  // The whole chain: the timer's channel 0 at the BIOS's 18.2 Hz raises IRQ 0 at cycle 262,144, the controller grants it,
  // and the vector names the BIOS's handler, which counts the tick and ends the interrupt. The program waits with
  // interrupts on.
  TEST_METHOD(TimerTickReachesTheBiosHandler)
  {
    PlatformRig rig("PlatformTimerTick");
    rig.Start([&rig] { WaitForEver(rig.Native()); });
    const std::uint32_t ticks = rig.Host().Day().Ticks();

    Assert::IsTrue(rig.Host().RunUntil(262'000) == Elite::StopReason::Reached);
    Assert::AreEqual(ticks, rig.Host().Day().Ticks(), L"not yet");
    Assert::IsTrue(rig.Host().RunUntil(2 * FIRST_TICK + 1'000) == Elite::StopReason::Reached);
    Assert::AreEqual(ticks + 2, rig.Host().Day().Ticks(), L"two ticks, so the first was ended");
    Assert::AreEqual(std::uint64_t{2}, rig.Host().InterruptsTaken());
  }

  // The program's start returning is its far return to the PSP, which is DOS's int 20h: the program ends.
  TEST_METHOD(ProgramEndsWhenItsStartReturns)
  {
    PlatformRig rig("PlatformTerminate");
    rig.Start([] {});
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock(), L"it took no time");
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls);
    Assert::IsTrue(rig.Host().RunUntil(2'000'000) == Elite::StopReason::Terminated, L"and stays ended");
  }

  // Paced time (ADR-008): a loop that counts is work, and work takes no time: a turn that waits for nothing never idles.
  // mov cx,1000; loop $, each LOOP a spin.
  TEST_METHOD(PacedWorkTakesNoTime)
  {
    PlatformRig rig("PlatformPacedWork");
    rig.Start(
      [&rig]
      {
        for (std::uint16_t count = 1000; count != 0;)
        {
          --count;
          rig.Native().Spin();
        }
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock(), L"no time passed");
  }

  // A loop that waits for the BIOS's tick count to change finds nothing at each poll, so the clock moves from event to event
  // until the first IRQ 0, at cycle 262,144, and the loop ends right there.
  TEST_METHOD(PacedWaitEndsAtTheEventItWaitsFor)
  {
    PlatformRig rig("PlatformPacedWait");
    rig.Start([&rig] { WaitForTick(rig); });
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(FIRST_TICK, rig.Host().Clock());
  }

  // An idle loop with interrupts off waits for ever, as on the real machine; the run reaches its end. cli; jmp $.
  TEST_METHOD(PacedIdleLoopRunsToTheLimit)
  {
    PlatformRig rig("PlatformPacedLimit");
    rig.Start(
      [&rig]
      {
        rig.Native().DisableInterrupts();
        for (;;)
        {
          rig.Native().Wait();
        }
      });
    Assert::IsTrue(rig.Host().RunUntil(500'000) == Elite::StopReason::Reached);
    Assert::AreEqual(std::uint64_t{500'000}, rig.Host().Clock());
  }

  // A loop that never waits would hold paced time still for ever: the run stops as Spinning.
  TEST_METHOD(PacedSpinIsStopped)
  {
    PlatformRig rig("PlatformPacedSpin");
    rig.Start(
      [&rig]
      {
        rig.Native().EnableInterrupts();
        for (;;)
        {
          rig.Native().Spin();
        }
      });
    rig.Host().SetSpinLimit(10'000);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Spinning);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock());
  }

  // Two runs of the same program to the same cycle end in the same state: the same interrupts taken and the same game state.
  TEST_METHOD(RunsAreIdenticalToTheCycle)
  {
    std::array<std::uint64_t, 2> clocks{};
    std::array<std::uint64_t, 2> interrupts{};
    std::array<std::string, 2> digests;
    for (std::size_t run = 0; run < 2; ++run)
    {
      PlatformRig rig("PlatformRepeat");
      rig.Start([&rig] { WaitForEver(rig.Native()); });
      Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Reached);
      clocks[run] = rig.Host().Clock();
      interrupts[run] = rig.Host().InterruptsTaken();
      digests[run] = Elite::GameStateDigest(rig.State(), rig.Host().Video());
    }
    Assert::AreEqual(clocks[0], clocks[1]);
    Assert::AreEqual(std::uint64_t{3}, interrupts[0], L"three ticks in a million cycles");
    Assert::AreEqual(interrupts[0], interrupts[1]);
    Assert::IsTrue(digests[0] == digests[1], L"the same game state");
  }

  // ADR-010 item 8: the program waits on the native thread, stops where a run ends, and carries on in the next: the first
  // timer tick, at cycle 262,144, ends its wait, as it ended the original's.
  TEST_METHOD(AWaitStopsAtTheEndOfARunAndCarriesOn)
  {
    PlatformRig rig("PlatformWaits");
    rig.Start([&rig] { WaitForTick(rig); });
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Elite::StopReason::Reached, L"the run ends inside the wait");
    Assert::AreEqual(std::uint64_t{100'000}, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(FIRST_TICK, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Entry().calls, L"one call, over two runs");
  }

  // A platform destroyed while the program waits unwinds what the native thread is running, and does not hang.
  TEST_METHOD(PowerOffMidWaitUnwindsTheNativeThread)
  {
    PlatformRig rig("PlatformAbandoned");
    bool unwound = false;
    rig.Start(
      [&rig, &unwound]
      {
        const UnwindWitness witness(&unwound);
        WaitForEver(rig.Native());
      });
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Elite::StopReason::Reached);
    Assert::IsFalse(unwound, L"still waiting");
    rig.PowerOff();
    Assert::IsTrue(unwound, L"unwound");
  }

  TEST_METHOD(TwoRoutinesAtOneEntryAreRefused)
  {
    PlatformRig rig("PlatformTwice");
    rig.Host().SetGameHandler(Elite::Irq::Timer, [] {});
    Assert::ExpectException<std::logic_error>([&rig] { rig.Host().SetGameHandler(Elite::Irq::Timer, [] {}); }, L"a handler");
    rig.Start([] {});
    Assert::ExpectException<std::logic_error>([&rig] { rig.Start([] {}); }, L"the program's start");
  }

  // A vector that names code no routine stands in for, as int 8 does once the game writes its segment again while the BIOS's
  // handler is there: the first interrupt taken there is refused.
  TEST_METHOD(AStrayHandlerIsRefused)
  {
    PlatformRig rig("PlatformStray");
    rig.Start(
      [&rig]
      {
        rig.Native().SetHandler(Elite::Irq::Timer, Elite::IrqHandler::Stray);
        WaitForEver(rig.Native());
      });
    Assert::ExpectException<std::logic_error>([&rig] { (void)rig.Host().RunUntil(1'000'000); }, L"at the first IRQ 0");
  }

  // The game's own timer handler in vector 8: IRQ 0 is taken as the 8088 takes one, at each rising edge of channel 0 while
  // the program waits, with IF cleared and the entry's cycles on the instruction clock; the handler's end of interrupt lets
  // the next one in.
  TEST_METHOD(HardwareInterruptsAreTakenAsThe8088TakesThem)
  {
    PlatformRig rig("PlatformIrq");
    std::vector<Elite::Cycles> taken;
    bool enabledInside = false;
    rig.Host().SetGameHandler(Elite::Irq::Timer,
                              [&rig, &taken, &enabledInside]
                              {
                                taken.push_back(rig.Host().Clock());
                                enabledInside = enabledInside || rig.Host().InterruptsEnabled();
                                rig.Native().EndOfInterrupt();
                              });
    const std::uint32_t ticks = rig.Host().Day().Ticks();
    rig.Start(
      [&rig]
      {
        rig.Native().SetHandler(Elite::Irq::Timer, Elite::IrqHandler::Game);
        WaitForEver(rig.Native());
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Reached);
    Assert::IsTrue(taken == std::vector<Elite::Cycles>{FIRST_TICK, 2 * FIRST_TICK, 3 * FIRST_TICK}, L"at each edge");
    Assert::IsFalse(enabledInside, L"IF cleared in the handler");
    Assert::AreEqual(std::uint64_t{3}, rig.Host().InterruptsTaken());
    Assert::AreEqual(ticks, rig.Host().Day().Ticks(), L"the BIOS's handler did not run");
    Assert::IsFalse(rig.Host().Events().InService(Elite::Irq::Timer), L"every one ended");
    Assert::AreEqual(std::uint64_t{3}, rig.Host().GameHandler(Elite::Irq::Timer).calls);
  }

  // The BIOS's handlers serve the timer's and the keyboard's interrupts at power-on, as its vectors leave them.
  TEST_METHOD(TheBiosServesBothInterruptsAtPowerOn)
  {
    PlatformRig rig("PlatformVectors");
    Assert::IsTrue(rig.Native().Handler(Elite::Irq::Timer) == Elite::IrqHandler::Bios, L"the BIOS's timer handler");
    Assert::IsTrue(rig.Native().Handler(Elite::Irq::Keyboard) == Elite::IrqHandler::Bios, L"the BIOS's keyboard handler");
  }

  // What the game's own test, IsMouseDriverInstalled (CS:02D4), finds: whether a driver is loaded, as int 33h's vector says.
  TEST_METHOD(TheGamesMouseCheckSeesWhetherADriverIsLoaded)
  {
    PlatformRig none("PlatformNoMouse", false);
    Assert::IsFalse(none.Native().MouseDriverLoaded(), L"no driver");
    Assert::IsFalse(Elite::IsMouseDriverInstalled(none.Native()).installed);

    PlatformRig loaded("PlatformMouse", true);
    Assert::IsTrue(loaded.Native().MouseDriverLoaded(), L"a driver");
    Assert::IsTrue(Elite::IsMouseDriverInstalled(loaded.Native()).installed);
  }

  // An interrupt nothing serves goes to the ROM's IRET, which returns at once: int 33h with no mouse driver, as the game makes
  // it, ends the run where it would have.
  TEST_METHOD(TheRomsIretServesAVectorNothingHooks)
  {
    PlatformRig rig("PlatformRomIret");
    rig.Start([&rig] { (void)rig.Native().ReadMouseMotion(); });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
  }

  // The BIOS's hardware handlers, native in the ROM's place, on each of the paths the ROM's code takes: the timer handler's
  // four (a tick, a tick that carries into the high word, a tick in the hour the day ends in, and the day's last tick), and
  // the keyboard handler's. What they leave is what the ROM's code, interpreted from the same state, gave before D7 deleted the
  // interpreter; its stack pushes are gone with the processor (D33), and the instruction clock with the joystick (D81).
  TEST_METHOD(BiosHandlersDoWhatTheRomDid)
  {
    struct Case
    {
      std::uint32_t ticks;
      std::uint32_t ticksAfter;
      std::uint8_t rolledOver;
    };
    const std::array<Case, 4> cases = {{
      {0x00051000, 0x00051001, 0},
      {0x0005FFFF, 0x00060000, 0},
      {0x00180100, 0x00180101, 0},
      {0x001800AF, 0x00000000, 1},
    }};
    for (const Case& test : cases)
    {
      PlatformRig rig("PlatformBiosTimer");
      Elite::Platform& platform = rig.Host();
      platform.Day().SetClock(test.ticks);
      platform.Events().Request(Elite::Irq::Timer);
      platform.TakeDueInterrupts();
      Assert::AreEqual(test.ticksAfter, platform.Day().Ticks(), L"the count");
      Assert::IsFalse(platform.Events().InService(Elite::Irq::Timer) || platform.Events().InService(Elite::Irq::Keyboard), L"ended");
      Assert::AreEqual(std::uint32_t{test.rolledOver}, std::uint32_t{platform.Day().ReadClock().rolledOver}, L"TIMER_OFL");
    }

    // The keyboard handler, with a key latched: the scan code read, the key acknowledged on the system control
    // port, and the interrupt ended; the key is not buffered.
    PlatformRig rig("PlatformBiosKeyboard");
    Elite::Platform& platform = rig.Host();
    platform.Events().Inject(0x1E);
    while (!platform.Events().LatchFull())
    {
      platform.Idle(Elite::NO_EVENT);
    }
    const std::string digest = Elite::GameStateDigest(rig.State(), platform.Video());
    platform.TakeDueInterrupts();
    Assert::IsFalse(platform.Events().LatchFull(), L"acknowledged");
    Assert::AreEqual(0x4Cu, std::uint32_t{platform.SystemControl()}, L"bit 7 high, then low");
    Assert::IsTrue(Elite::GameStateDigest(rig.State(), platform.Video()) == digest, L"nothing buffered");
    Assert::IsFalse(platform.Events().InService(Elite::Irq::Timer) || platform.Events().InService(Elite::Irq::Keyboard), L"ended");
  }

  // The system control port reads back what was written; bits 0-1 reach the speaker, the tone's gate and its data, and bit 7
  // holds the keyboard without touching either.
  TEST_METHOD(SystemControlReadsBackAndDrivesGateAndSpeaker)
  {
    PlatformRig rig("PlatformSystemControl");
    Elite::Platform& platform = rig.Host();
    Assert::AreEqual(0x4Cu, std::uint32_t{platform.SystemControl()}, L"power-on value");
    const std::size_t events = platform.Sound().Events().size();

    platform.Idle(1000);
    platform.SetSystemControl(0x4F);
    Assert::AreEqual(0x4Fu, std::uint32_t{platform.SystemControl()});
    Assert::IsTrue(platform.Sound().Now().tone.gate);
    const Elite::Speaker::Event& on = platform.Sound().Events().back();
    Assert::AreEqual(platform.Clock(), on.at);
    Assert::IsTrue(on.gate && on.data);
    Assert::AreEqual(events + 1, platform.Sound().Events().size(), L"the gate and the bits at one cycle are one event");

    platform.Idle(2000);
    platform.SetSystemControl(0xCF);
    platform.SetSystemControl(0x4F);
    Assert::AreEqual(events + 1, platform.Sound().Events().size(), L"the acknowledge pulse leaves the speaker alone");

    platform.SetSystemControl(0x4D);
    Assert::IsTrue(platform.Sound().Now().tone.gate);
    Assert::IsFalse(platform.Sound().Events().back().data);
  }

  // ExitToDos (CS:00AD) prints the exit message in mode 2 through DOS's int 21h AH=09h, which writes through the BIOS's
  // teletype at the cursor, keeping the attribute there, and moves the cursor on the CRTC.
  TEST_METHOD(PrintStringWritesAtTheCursorThroughTheBios)
  {
    PlatformRig rig("PlatformPrint");
    rig.Native().PrintDosString("ABC$");

    const Elite::GameState& state = rig.State();
    Assert::AreEqual(std::uint32_t{'A'}, std::uint32_t{state.VideoByte(0)});
    Assert::AreEqual(0x07u, std::uint32_t{state.VideoByte(1)}, L"the attribute is kept");
    Assert::AreEqual(std::uint32_t{'B'}, std::uint32_t{state.VideoByte(2)});
    Assert::AreEqual(std::uint32_t{'C'}, std::uint32_t{state.VideoByte(4)});
    const Elite::Cga& cga = rig.Host().Video();
    Assert::AreEqual(0x00u, std::uint32_t{cga.Crtc(Elite::CrtcRegister::CursorAddressHigh)}, L"the CRTC's cursor at 3");
    Assert::AreEqual(0x03u, std::uint32_t{cga.Crtc(Elite::CrtcRegister::CursorAddressLow)});
  }
};

} // namespace GameLogicTests
