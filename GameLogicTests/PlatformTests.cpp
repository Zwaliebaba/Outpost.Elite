#include "pch.h"

#include "Bios.h"
#include "Cga.h"
#include "Hardware.h"
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

// A loop's address, for its turn signature (ADR-015).
constexpr std::uint16_t WAIT_LOOP = 0x0100;

// Where a test puts a string in the data segment.
constexpr std::uint16_t TEXT = 0x0200;

// A handler's address in the program's code, which a vector can name and a native routine stand in for.
constexpr Elite::FarAddress HANDLER{0x2000, 0x0010};

// An address in the program's code that no native routine stands in for.
constexpr Elite::FarAddress UNPORTED{0x2000, 0x0020};

// What the 8088 takes over the ROM's IRET: Intel's 8086 figure and three words popped over the 8-bit bus.
constexpr Elite::Cycles IRET_CYCLES = 24 + 3 * 4;

// sti; then wait for ever, a turn at the loop's backward jump, carrying nothing: what the program's idle loop does.
void WaitForEver(Elite::Hardware& _hardware)
{
  _hardware.EnableInterrupts();
  for (;;)
  {
    _hardware.LoopTurn(WAIT_LOOP, {});
  }
}

// Waits for the BIOS's tick count to change, with interrupts on, a turn at a time carrying the count it started from: sti;
// mov bx,es:[6Ch]; wait: cmp bx,es:[6Ch]; je wait.
void WaitForTick(PlatformRig& _rig)
{
  _rig.Native().EnableInterrupts();
  const std::uint32_t ticks = _rig.Host().BiosServices().Ticks();
  while (_rig.Host().BiosServices().Ticks() == ticks)
  {
    _rig.Native().LoopTurn(WAIT_LOOP, {static_cast<std::uint16_t>(ticks & 0xFFFF)});
  }
}

// _text in the data segment at _offset.
void Put(Elite::GameState& _state, std::uint16_t _offset, std::string_view _text)
{
  for (std::size_t index = 0; index < _text.size(); ++index)
  {
    _state.SetByte(static_cast<std::uint16_t>(_offset + index), static_cast<std::uint8_t>(_text[index]));
  }
}

[[nodiscard]] Elite::ServiceFault FaultOf(const Elite::Platform& _platform)
{
  Assert::IsTrue(_platform.Fault().has_value(), L"a refused call is recorded");
  return _platform.Fault().value_or(Elite::ServiceFault{});
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

// The native platform (Platform, D33): paced time, interrupts, the native thread, the vector table and the ROM's bytes, the
// BIOS's two hardware handlers, and DOS's console. What the emulated PC's tests held of the Pc, its Dispatcher, its native
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
    const std::uint32_t ticks = rig.Host().BiosServices().Ticks();

    Assert::IsTrue(rig.Host().RunUntil(262'000) == Elite::StopReason::Reached);
    Assert::AreEqual(ticks, rig.Host().BiosServices().Ticks(), L"not yet");
    Assert::IsTrue(rig.Host().RunUntil(2 * FIRST_TICK + 1'000) == Elite::StopReason::Reached);
    Assert::AreEqual(ticks + 2, rig.Host().BiosServices().Ticks(), L"two ticks, so the first was ended");
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

  // A run stops at its first refused call, and that is the one reported, even if native code went on and made another.
  TEST_METHOD(TheFirstRefusalIsKept)
  {
    PlatformRig rig("PlatformFirstFault");
    bool unwound = false;
    rig.Start(
      [&rig, &unwound]
      {
        try
        {
          (void)rig.Native().ReadBiosKey(); // the buffer is empty: the ROM would wait for ever
        }
        catch (...)
        {
          unwound = true; // a routine that swallowed the stop, which no native routine does
        }
        rig.Native().SetVideoMode(7);
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Fault);
    Assert::IsTrue(unwound);
    const Elite::ServiceFault fault = FaultOf(rig.Host());
    Assert::IsTrue(fault.kind == Elite::FaultKind::WouldBlock);
    Assert::AreEqual(0x16u, std::uint32_t{fault.vector}, L"the keyboard read, not the mode set");
    Assert::AreEqual(0x00u, std::uint32_t{fault.ah});
  }

  // Paced time (ADR-008): a loop that counts is work, and work takes no time: a turn whose signature changed never idles.
  // mov cx,1000; loop $, a turn at each LOOP carrying CX.
  TEST_METHOD(PacedWorkTakesNoTime)
  {
    PlatformRig rig("PlatformPacedWork");
    rig.Start(
      [&rig]
      {
        for (std::uint16_t count = 1000; count != 0;)
        {
          --count;
          rig.Native().LoopTurn(WAIT_LOOP, {count});
        }
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock(), L"no time passed");
  }

  // A loop that waits for the BIOS's tick count to change changes nothing on its turns, so the clock moves from event to
  // event until the first IRQ 0, at cycle 262,144, and the loop ends right there.
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
          rig.Native().LoopTurn(WAIT_LOOP, {});
        }
      });
    Assert::IsTrue(rig.Host().RunUntil(500'000) == Elite::StopReason::Reached);
    Assert::AreEqual(std::uint64_t{500'000}, rig.Host().Clock());
  }

  // A loop whose turns never repeat never waits, so paced time would stand still for ever: the run stops as Spinning.
  TEST_METHOD(PacedSpinIsStopped)
  {
    PlatformRig rig("PlatformPacedSpin");
    rig.Start(
      [&rig]
      {
        rig.Native().EnableInterrupts();
        for (std::uint16_t turn = 0;; ++turn)
        {
          rig.Native().LoopTurn(WAIT_LOOP, {turn});
        }
      });
    rig.Host().SetSpinLimit(10'000);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Spinning);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock());
  }

  // Two runs of the same program to the same cycle end in the same state: the same interrupts taken, the same counts, the
  // same game state.
  TEST_METHOD(RunsAreIdenticalToTheCycle)
  {
    std::array<std::uint64_t, 2> clocks{};
    std::array<std::uint64_t, 2> interrupts{};
    std::array<std::uint64_t, 2> changes{};
    std::array<std::uint64_t, 2> writes{};
    std::array<std::string, 2> digests;
    for (std::size_t run = 0; run < 2; ++run)
    {
      PlatformRig rig("PlatformRepeat");
      rig.Start([&rig] { WaitForEver(rig.Native()); });
      Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Reached);
      clocks[run] = rig.Host().Clock();
      interrupts[run] = rig.Host().InterruptsTaken();
      changes[run] = rig.Host().MemoryChanges();
      writes[run] = rig.Host().PortWrites();
      digests[run] = Elite::GameStateDigest(rig.State(), rig.Host().Video());
    }
    Assert::AreEqual(clocks[0], clocks[1]);
    Assert::AreEqual(std::uint64_t{3}, interrupts[0], L"three ticks in a million cycles");
    Assert::AreEqual(interrupts[0], interrupts[1]);
    Assert::AreEqual(changes[0], changes[1]);
    Assert::AreEqual(writes[0], writes[1]);
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
    rig.Host().Hook(HANDLER, "Handler", [] {});
    Assert::ExpectException<std::logic_error>([&rig] { rig.Host().Hook(HANDLER, "Again", [] {}); }, L"a handler");
    rig.Start([] {});
    Assert::ExpectException<std::logic_error>([&rig] { rig.Start([] {}); }, L"the program's start");
  }

  // An INT goes to the handler its vector names, natively, with IF and TF cleared, and returns as IRET does, the flags as
  // they were. Setting the vector counts the bytes of the table it changes.
  TEST_METHOD(AnInterruptRunsTheHandlerItsVectorNames)
  {
    PlatformRig rig("PlatformInterrupts");
    bool enabledInside = true;
    bool enabledAfter = false;
    rig.Host().Hook(HANDLER, "Handler", [&rig, &enabledInside] { enabledInside = rig.Host().InterruptsEnabled(); });
    const std::uint64_t changes = rig.Host().MemoryChanges();
    rig.Native().SetInterruptVector(0x60, HANDLER);
    Assert::AreEqual(changes + 3, rig.Host().MemoryChanges(), L"F000:FD60 to 2000:0010: three bytes changed");
    Assert::IsTrue(rig.Native().InterruptVector(0x60) == HANDLER);

    rig.Start(
      [&rig, &enabledAfter]
      {
        rig.Native().EnableInterrupts();
        rig.Host().SoftwareInterrupt(0x60);
        enabledAfter = rig.Host().InterruptsEnabled();
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::IsFalse(enabledInside, L"IF cleared in the handler");
    Assert::IsTrue(enabledAfter, L"and back after its IRET");
    Assert::AreEqual(std::uint64_t{1}, rig.Host().Hooks().at(0x20010).calls);
  }

  // An interrupt whose vector names code no native routine stands in for stops the run there: nothing interprets it.
  TEST_METHOD(AnInterruptToCodeNoRoutineStandsInForStopsTheRun)
  {
    PlatformRig rig("PlatformUnported");
    rig.Start(
      [&rig]
      {
        rig.Native().SetInterruptVector(0x60, UNPORTED);
        rig.Host().SoftwareInterrupt(0x60);
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Unported);
    Assert::IsTrue(rig.Host().UnportedAt().value_or(Elite::FarAddress{}) == UNPORTED, L"where it was sent");
  }

  // The game's own timer handler in vector 8: IRQ 0 is taken as the 8088 takes one, at each rising edge of channel 0 while
  // the program waits, with IF cleared and the entry's cycles on the instruction clock; the handler's end of interrupt lets
  // the next one in.
  TEST_METHOD(HardwareInterruptsAreTakenAsThe8088TakesThem)
  {
    PlatformRig rig("PlatformIrq");
    std::vector<Elite::Cycles> taken;
    bool enabledInside = false;
    rig.Host().Hook(HANDLER, "TimerHandler",
                    [&rig, &taken, &enabledInside]
                    {
                      taken.push_back(rig.Host().Clock());
                      enabledInside = enabledInside || rig.Host().InterruptsEnabled();
                      rig.Native().EndOfInterrupt();
                    });
    const std::uint32_t ticks = rig.Host().BiosServices().Ticks();
    rig.Start(
      [&rig]
      {
        rig.Native().SetInterruptVector(0x08, HANDLER);
        WaitForEver(rig.Native());
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Elite::StopReason::Reached);
    Assert::IsTrue(taken == std::vector<Elite::Cycles>{FIRST_TICK, 2 * FIRST_TICK, 3 * FIRST_TICK}, L"at each edge");
    Assert::IsFalse(enabledInside, L"IF cleared in the handler");
    Assert::AreEqual(std::uint64_t{3}, rig.Host().InterruptsTaken());
    Assert::AreEqual(3 * Elite::Platform::HARDWARE_INTERRUPT_CYCLES, rig.Host().InstructionCycles(), L"each entry's cycles");
    Assert::AreEqual(ticks, rig.Host().BiosServices().Ticks(), L"the BIOS's handler did not run");
    Assert::AreEqual(0u, std::uint32_t{rig.Host().Interrupts().InServiceRegister()}, L"every one ended");
  }

  // Every vector as the BIOS leaves it: each at its own IRET in the ROM, F000:FD00 + n, but the timer's and the keyboard's,
  // which name the BIOS's handlers, and DOS's terminate address. The ROM is an IBM PC's, with no Amstrad's name where Start
  // looks for one.
  TEST_METHOD(TheVectorTableIsAsTheBiosLeavesIt)
  {
    PlatformRig rig("PlatformVectors");
    Elite::Hardware& hardware = rig.Native();
    for (std::uint32_t vector = 0; vector < 256; ++vector)
    {
      const Elite::FarAddress handler = hardware.InterruptVector(static_cast<std::uint8_t>(vector));
      Assert::AreEqual(0xF000u, std::uint32_t{handler.segment});
      if (vector == 0x08 || vector == 0x09 || vector == 0x22)
      {
        continue;
      }
      Assert::AreEqual(0xFD00u + vector, std::uint32_t{handler.offset});
      Assert::AreEqual(0xCFu, std::uint32_t{hardware.HandlerOpcode(handler)}, L"an IRET");
    }
    Assert::AreEqual(0xE000u, std::uint32_t{hardware.InterruptVector(0x08).offset}, L"the BIOS's timer handler");
    Assert::AreEqual(0xE040u, std::uint32_t{hardware.InterruptVector(0x09).offset}, L"the BIOS's keyboard handler");
    Assert::AreEqual(0xE070u, std::uint32_t{hardware.InterruptVector(0x22).offset}, L"the terminate address");

    Assert::AreEqual(0xFFu, std::uint32_t{hardware.RomByte(0xF000, 0xFFFE)}, L"the model byte: an IBM PC");
    // Start's check: repe cmpsb of "Amstrad" against FC00:0016.
    constexpr std::string_view AMSTRAD = "Amstrad";
    bool same = true;
    for (std::size_t index = 0; index < AMSTRAD.size(); ++index)
    {
      same = same && hardware.RomByte(0xFC00, static_cast<std::uint16_t>(0x16 + index)) == static_cast<std::uint8_t>(AMSTRAD[index]);
    }
    Assert::IsFalse(same, L"FC00:0016 must not hold \"Amstrad\"");
    Assert::ExpectException<std::out_of_range>([&hardware] { (void)hardware.RomByte(0x1000, 0); }, L"the ROM is F000:0000 on");
  }

  // What the game's own test, IsMouseDriverInstalled (CS:02D4), reads: the int 33h vector, and the first byte of the code it
  // names, an IRET when no driver is loaded.
  TEST_METHOD(TheGamesMouseCheckSeesWhetherADriverIsLoaded)
  {
    PlatformRig none("PlatformNoMouse", false);
    const Elite::FarAddress absent = none.Native().InterruptVector(0x33);
    Assert::IsTrue(absent == Elite::FarAddress{0xF000, 0xFD33});
    Assert::AreEqual(0xCFu, std::uint32_t{none.Native().HandlerOpcode(absent)}, L"no driver");

    PlatformRig loaded("PlatformMouse", true);
    const Elite::FarAddress present = loaded.Native().InterruptVector(0x33);
    Assert::IsTrue(present == Elite::FarAddress{0xF000, 0xE060});
    Assert::IsTrue(loaded.Native().HandlerOpcode(present) != 0xCF, L"the driver's entry is not an IRET");
  }

  // An interrupt nothing serves goes to the ROM's IRET, which returns at once and costs the 8088 its cycles: int 33h with no
  // mouse driver, as the game makes it.
  TEST_METHOD(TheRomsIretServesAVectorNothingHooks)
  {
    PlatformRig rig("PlatformRomIret");
    rig.Start([&rig] { (void)rig.Native().ReadMouseMotion(); });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Terminated);
    Assert::AreEqual(IRET_CYCLES, rig.Host().InstructionCycles(), L"the IRET's cycles");
  }

  // The BIOS's hardware handlers, native in the ROM's place, on each of the paths the ROM's code takes: the timer handler's
  // four (a tick, a tick that carries into the high word, a tick in the hour the day ends in, and the day's last tick), and
  // the keyboard handler's. The bytes of the data area they change, their port writes and the instruction clock are what the
  // ROM's code, interpreted from the same state, gave before D7 deleted the interpreter; its stack pushes are gone with the
  // processor (D33).
  TEST_METHOD(BiosHandlersDoWhatTheRomDid)
  {
    struct Case
    {
      std::uint32_t ticks;
      std::uint32_t ticksAfter;
      std::uint64_t changes;
      std::uint8_t rolledOver;
      Elite::Cycles instructionCycles;
    };
    const std::array<Case, 4> cases = {{
      {0x00051000, 0x00051001, 1, 0, 299},
      {0x0005FFFF, 0x00060000, 3, 0, 316},
      {0x00180100, 0x00180101, 1, 0, 323},
      {0x001800AF, 0x00000000, 4, 1, 358},
    }};
    for (const Case& test : cases)
    {
      PlatformRig rig("PlatformBiosTimer");
      Elite::Platform& platform = rig.Host();
      platform.BiosServices().SetClock(test.ticks);
      const std::uint64_t changes = platform.MemoryChanges();
      const std::uint64_t writes = platform.PortWrites();
      const Elite::Cycles cycles = platform.InstructionCycles();
      platform.Interrupts().RaiseIrq(0);
      platform.TakeDueInterrupts();
      Assert::AreEqual(test.ticksAfter, platform.BiosServices().Ticks(), L"the count");
      Assert::AreEqual(test.changes, platform.MemoryChanges() - changes, L"every byte of the data area it changed");
      Assert::AreEqual(std::uint64_t{1}, platform.PortWrites() - writes, L"one OUT: the end of interrupt");
      Assert::AreEqual(Elite::Platform::HARDWARE_INTERRUPT_CYCLES + test.instructionCycles, platform.InstructionCycles() - cycles,
                       L"the instruction clock: the entry, then the ROM's code to its IRET");
      Assert::AreEqual(0u, std::uint32_t{platform.Interrupts().InServiceRegister()}, L"ended");
      Assert::AreEqual(std::uint32_t{test.rolledOver}, std::uint32_t{platform.BiosServices().ReadClock().rolledOver}, L"TIMER_OFL");
    }

    // The keyboard handler, with a key latched in the 8255: the scan code read, the key acknowledged on the system control
    // port, and the interrupt ended; the key is not buffered.
    PlatformRig rig("PlatformBiosKeyboard");
    Elite::Platform& platform = rig.Host();
    platform.KeyboardController().Inject(0x1E);
    while (!platform.KeyboardController().LatchFull())
    {
      platform.Idle(Elite::NO_EVENT);
    }
    const std::uint64_t changes = platform.MemoryChanges();
    const std::uint64_t writes = platform.PortWrites();
    const Elite::Cycles cycles = platform.InstructionCycles();
    platform.TakeDueInterrupts();
    Assert::IsFalse(platform.KeyboardController().LatchFull(), L"acknowledged");
    Assert::AreEqual(0x4Cu, std::uint32_t{platform.KeyboardController().SystemControl()}, L"bit 7 high, then low");
    Assert::AreEqual(std::uint64_t{3}, platform.PortWrites() - writes, L"61h twice, then the end of interrupt");
    Assert::AreEqual(std::uint64_t{0}, platform.MemoryChanges() - changes, L"nothing buffered");
    Assert::IsFalse(platform.BiosServices().PeekKey().has_value());
    Assert::AreEqual(Elite::Platform::HARDWARE_INTERRUPT_CYCLES + 127, platform.InstructionCycles() - cycles);
    Assert::AreEqual(0u, std::uint32_t{platform.Interrupts().InServiceRegister()}, L"ended");
  }

  // Start (CS:000E) refuses to run when DOS's major version is 0.
  TEST_METHOD(DosVersionIs330)
  {
    PlatformRig rig("PlatformDosVersion");
    const Elite::DosVersion version = rig.Native().ReadDosVersion(0);
    Assert::AreEqual(3u, std::uint32_t{version.major});
    Assert::AreEqual(30u, std::uint32_t{version.minor});
  }

  // DOS's time of day is the start moment, 12:00:00.00 by default, plus the platform's time: 90.5 s later, 12:01:30.50.
  TEST_METHOD(DosTimeIsTheStartMomentPlusThePlatformsTime)
  {
    PlatformRig rig("PlatformDosTime");
    rig.Start(
      [&rig]
      {
        rig.Native().DisableInterrupts();
        for (;;)
        {
          rig.Native().LoopTurn(WAIT_LOOP, {});
        }
      });
    Assert::IsTrue(rig.Host().RunUntil(431'931'764) == Elite::StopReason::Reached); // 90.5 s at 14.31818 MHz / 3
    const Elite::DosTime time = rig.Native().ReadDosTime();
    Assert::AreEqual(12u, std::uint32_t{time.hours});
    Assert::AreEqual(1u, std::uint32_t{time.minutes});
    Assert::AreEqual(30u, std::uint32_t{time.seconds});
    Assert::AreEqual(50u, std::uint32_t{time.hundredths});
  }

  // ExitToDos (CS:00AD) prints the exit message in mode 2 through DOS's int 21h AH=09h, which writes through the BIOS's
  // teletype at the cursor, keeping the attribute there, and moves the cursor on the CRTC.
  TEST_METHOD(PrintStringWritesAtTheCursorThroughTheBios)
  {
    PlatformRig rig("PlatformPrint");
    Put(rig.State(), TEXT, "AB\r\nC$");
    rig.Host().PrintDosString(rig.State().DataSegment(), TEXT);

    const Elite::GameState& state = rig.State();
    Assert::AreEqual(std::uint32_t{'A'}, std::uint32_t{state.VideoByte(0)});
    Assert::AreEqual(0x07u, std::uint32_t{state.VideoByte(1)}, L"the attribute is kept");
    Assert::AreEqual(std::uint32_t{'B'}, std::uint32_t{state.VideoByte(2)});
    Assert::AreEqual(std::uint32_t{'C'}, std::uint32_t{state.VideoByte(160)}, L"row 1, column 0");
    Assert::AreEqual(1u, std::uint32_t{rig.Host().BiosServices().CursorColumn()});
    const Elite::Cga& cga = rig.Host().Video();
    Assert::AreEqual(0x00u, std::uint32_t{cga.Crtc(Elite::CrtcRegister::CursorAddressHigh)}, L"the CRTC's cursor at 81");
    Assert::AreEqual(0x51u, std::uint32_t{cga.Crtc(Elite::CrtcRegister::CursorAddressLow)});
    Assert::IsFalse(rig.Host().Fault().has_value());
  }

  TEST_METHOD(PrintStringScrollsAtTheBottomAndExpandsTabs)
  {
    PlatformRig rig("PlatformScroll");
    Elite::GameState& state = rig.State();
    state.SetVideoByte(0, 'Q');
    state.SetVideoByte(160, 'R');
    state.SetVideoByte(24 * 160 + 1, 0x1F);
    for (int row = 0; row < 24; ++row)
    {
      Assert::IsTrue(rig.Host().BiosServices().WriteTeletype('\n')); // to row 24, column 0
    }
    Put(state, TEXT, "Z\n\tX$");
    rig.Host().PrintDosString(state.DataSegment(), TEXT);

    Assert::AreEqual(std::uint32_t{'R'}, std::uint32_t{state.VideoByte(0)}, L"row 1 moved to row 0");
    Assert::AreEqual(std::uint32_t{'Z'}, std::uint32_t{state.VideoByte(23 * 160)}, L"row 24 moved to row 23");
    Assert::AreEqual(0x1Fu, std::uint32_t{state.VideoByte(23 * 160 + 1)});
    Assert::AreEqual(std::uint32_t{' '}, std::uint32_t{state.VideoByte(24 * 160)}, L"the new row is blank");
    Assert::AreEqual(0x07u, std::uint32_t{state.VideoByte(24 * 160 + 1)}, L"with the attribute under the cursor");
    // After the line feed the cursor is at row 24, column 1; the tab runs to column 8.
    Assert::AreEqual(std::uint32_t{'X'}, std::uint32_t{state.VideoByte(24 * 160 + 16)});
    Assert::AreEqual(9u, std::uint32_t{rig.Host().BiosServices().CursorColumn()});
  }

  // In a graphics mode the teletype would need the ROM's font, which is not part of the reference (ADR-001 D14): the call is
  // refused and nothing is printed.
  TEST_METHOD(PrintStringInAGraphicsModeIsRefusedAndPrintsNothing)
  {
    PlatformRig rig("PlatformPrintGraphics");
    Put(rig.State(), TEXT, "Hi$");
    rig.Start(
      [&rig]
      {
        rig.Native().SetVideoMode(4);
        rig.Native().PrintDosString(rig.State().DataSegment(), TEXT);
      });
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Elite::StopReason::Fault);
    const Elite::ServiceFault fault = FaultOf(rig.Host());
    Assert::IsTrue(fault.kind == Elite::FaultKind::UnsupportedRequest);
    Assert::AreEqual(0x21u, std::uint32_t{fault.vector});
    Assert::AreEqual(0x09u, std::uint32_t{fault.ah});
    Assert::AreEqual(0u, std::uint32_t{rig.State().VideoByte(0)}, L"mode 4 cleared it, and nothing was printed");
  }
};

} // namespace GameLogicTests
