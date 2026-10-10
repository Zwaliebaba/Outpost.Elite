#include "pch.h"

#include "Firmware.h"
#include "Pc.h"
#include "PcRig.h"
#include "Sha256.h"

#include <string>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint8_t NOP = 0x90;
constexpr std::uint16_t BIOS_TICKS = 0x6C;

// A one-byte program whose entry is _routine, which waits (Pc::Hook): what a test's program does, it does natively, on
// the Pc's Dispatcher.
Machine::LoadedProgram LoadNative(PcRig& _rig, Machine::NativeRoutine _routine, Machine::NativeWait _wait = Machine::NativeWait::Always)
{
  const Machine::LoadedProgram program = _rig.Load(TinyExe({NOP}));
  _rig.Host().Hook(program.loadSegment, 0, "Program", std::move(_routine), {}, Machine::NativeReturn::Near, _wait);
  return program;
}

void EnableInterrupts(Machine::Pc& _pc) noexcept
{
  _pc.Processor().Regs().flags = static_cast<std::uint16_t>(_pc.Processor().Regs().flags | Machine::FLAG_INTERRUPT);
}

// sti; then wait for ever, a wait at a time.
void WaitForEver(Machine::Pc& _pc)
{
  EnableInterrupts(_pc);
  for (;;)
    _pc.Wait();
}

} // namespace

TEST_CLASS(PcTests)
{
public:
  // The whole chain: the PIT's channel 0 at the BIOS's 18.2 Hz raises IRQ0 at cycle 262,144, the PIC grants it, the
  // Dispatcher vectors to the ROM's timer handler, native in the ROM code's place, which counts the tick and sends EOI.
  // The program waits with interrupts on, as sti; jmp $ did when it was interpreted.
  TEST_METHOD(TimerTickReachesTheBiosHandler)
  {
    PcRig rig("TimerTick");
    (void)LoadNative(rig, &WaitForEver);
    const std::uint32_t tickAddress = Machine::Memory::Linear(Machine::Firmware::DATA_SEGMENT, BIOS_TICKS);
    Assert::AreEqual(0u, std::uint32_t{rig.Host().Ram().Read16(tickAddress)}, L"midnight");

    Assert::IsTrue(rig.Host().RunUntil(262'000) == Machine::StopReason::Reached);
    Assert::AreEqual(0u, std::uint32_t{rig.Host().Ram().Read16(tickAddress)}, L"not yet");
    Assert::IsTrue(rig.Host().RunUntil(2 * 262'144 + 1'000) == Machine::StopReason::Reached);
    Assert::AreEqual(2u, std::uint32_t{rig.Host().Ram().Read16(tickAddress)}, L"two ticks, so the first was ended");
  }

  TEST_METHOD(Int20hEndsTheRun)
  {
    PcRig rig("Terminate");
    (void)LoadNative(rig, [](Machine::Pc& _pc) { _pc.CallInterrupt(0x20); }, Machine::NativeWait::Never);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Terminated);
    Assert::IsTrue(rig.Host().Services().Terminated());
  }

  // mov ah,0FFh; int 21h, natively: the INT at offset 2, so IP is 4 when the call is made, as the 8088 leaves it.
  TEST_METHOD(RefusedCallStopsTheRun)
  {
    PcRig rig("Fault");
    const Machine::LoadedProgram program = LoadNative(
      rig,
      [](Machine::Pc& _pc)
      {
        Machine::Registers& regs = _pc.Processor().Regs();
        regs.ax = static_cast<std::uint16_t>(0xFF00 | (regs.ax & 0x00FF));
        regs.ip = 4;
        _pc.CallInterrupt(0x21);
      },
      Machine::NativeWait::Never);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Fault);
    Assert::IsTrue(rig.Host().Services().Fault().has_value());
    const Machine::ServiceFault fault = rig.Host().Services().Fault().value_or(Machine::ServiceFault{});
    Assert::AreEqual(0x21u, std::uint32_t{fault.vector});
    Assert::AreEqual(0xFFu, std::uint32_t{fault.ah});
    Assert::AreEqual(std::uint32_t{program.loadSegment}, std::uint32_t{fault.cs});
    Assert::AreEqual(0x0002u, std::uint32_t{fault.ip}, L"the INT's own address");
  }

  // Paced time (ADR-008): a loop that counts is work, and work takes no time. mov cx,1000; loop $; int 20h, natively, a turn
  // at each LOOP.
  TEST_METHOD(PacedWorkTakesNoTime)
  {
    PcRig rig("PacedWork");
    (void)LoadNative(rig,
                     [](Machine::Pc& _pc)
                     {
                       Machine::Registers& regs = _pc.Processor().Regs();
                       for (regs.cx = 1000; regs.cx != 0;)
                       {
                         regs.cx = static_cast<std::uint16_t>(regs.cx - 1);
                         _pc.LoopTurn();
                       }
                       _pc.CallInterrupt(0x20);
                     });
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock(), L"no time passed");
  }

  // A loop that waits for the BIOS tick count to change changes nothing on its turns, so the clock moves from event to event
  // until the first IRQ0, at cycle 262,144, and the loop ends right there. sti; mov bx,40h; mov ds,bx; mov ax,[6Ch]; wait:
  // cmp ax,[6Ch]; je wait; int 20h, natively, a turn at each JE.
  TEST_METHOD(PacedWaitEndsAtTheEventItWaitsFor)
  {
    PcRig rig("PacedWait");
    (void)LoadNative(rig,
                     [](Machine::Pc& _pc)
                     {
                       Machine::Registers& regs = _pc.Processor().Regs();
                       EnableInterrupts(_pc);
                       regs.bx = Machine::Firmware::DATA_SEGMENT;
                       regs.ds = regs.bx;
                       regs.ax = _pc.Ram().Read16(regs.ds, BIOS_TICKS);
                       while (_pc.Ram().Read16(regs.ds, BIOS_TICKS) == regs.ax)
                         _pc.LoopTurn();
                       _pc.CallInterrupt(0x20);
                     });
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
  }

  // An idle loop with interrupts off waits for ever, as on the real machine; the run reaches its end. cli; jmp $, natively.
  TEST_METHOD(PacedIdleLoopRunsToTheLimit)
  {
    PcRig rig("PacedLimit");
    (void)LoadNative(rig,
                     [](Machine::Pc& _pc)
                     {
                       Machine::Registers& regs = _pc.Processor().Regs();
                       regs.flags = static_cast<std::uint16_t>(regs.flags & ~Machine::FLAG_INTERRUPT);
                       for (;;)
                         _pc.LoopTurn();
                     });
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    Assert::IsTrue(rig.Host().RunUntil(500'000) == Machine::StopReason::Reached);
    Assert::AreEqual(std::uint64_t{500'000}, rig.Host().Clock());
  }

  // A loop that changes something every turn never waits, so paced time would stand still for ever. sti; inc ax; jmp back to
  // the inc, natively.
  TEST_METHOD(PacedSpinIsStopped)
  {
    PcRig rig("PacedSpin");
    (void)LoadNative(rig,
                     [](Machine::Pc& _pc)
                     {
                       Machine::Registers& regs = _pc.Processor().Regs();
                       EnableInterrupts(_pc);
                       for (;;)
                       {
                         ++regs.ax;
                         _pc.LoopTurn();
                       }
                     });
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    rig.Host().SetSpinLimit(10'000);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Spinning);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock());
  }

  // Two runs of the same program to the same cycle end in the same state: the same interrupts taken, the same memory.
  TEST_METHOD(RunsAreIdenticalToTheCycle)
  {
    std::uint64_t clocks[2] = {};
    std::uint64_t interrupts[2] = {};
    std::string memory[2];
    for (int run = 0; run < 2; ++run)
    {
      PcRig rig("Repeat");
      (void)LoadNative(rig, &WaitForEver);
      Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Reached);
      clocks[run] = rig.Host().Clock();
      interrupts[run] = rig.Host().Processor().HardwareInterruptCount();
      memory[run] = Machine::Sha256::ToHex(Machine::Sha256::Of(rig.Host().Ram().Bytes()));
    }
    Assert::AreEqual(clocks[0], clocks[1]);
    Assert::AreEqual(std::uint64_t{3}, interrupts[0], L"three ticks in a million cycles");
    Assert::AreEqual(interrupts[0], interrupts[1]);
    Assert::IsTrue(memory[0] == memory[1], L"the same memory");
  }
};

} // namespace MachineTests
