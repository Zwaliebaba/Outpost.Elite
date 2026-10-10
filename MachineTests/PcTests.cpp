#include "pch.h"

#include "DirectoryFileStore.h"
#include "Firmware.h"
#include "Pc.h"
#include "PcRig.h"
#include "ServiceRig.h"
#include "Sha256.h"

#include <initializer_list>
#include <memory>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

TEST_CLASS(PcTests)
{
public:
  // The whole chain: the PIT's channel 0 at the BIOS's 18.2 Hz raises IRQ0 at cycle 262,144, the PIC
  // grants it, the CPU vectors to the ROM's timer handler, which counts the tick and sends EOI.
  TEST_METHOD(TimerTickReachesTheBiosHandler)
  {
    PcRig rig("TimerTick");
    (void)rig.Load(TinyExe({0xFB, 0xEB, 0xFE})); // sti; jmp $
    const std::uint32_t tickAddress = Machine::Memory::Linear(Machine::Firmware::DATA_SEGMENT, 0x6C);
    Assert::AreEqual(0u, std::uint32_t{rig.Host().Ram().Read16(tickAddress)}, L"midnight");

    Assert::IsTrue(rig.Host().RunUntil(262'000) == Machine::StopReason::Reached);
    Assert::AreEqual(0u, std::uint32_t{rig.Host().Ram().Read16(tickAddress)}, L"not yet");
    Assert::IsTrue(rig.Host().RunUntil(2 * 262'144 + 1'000) == Machine::StopReason::Reached);
    Assert::AreEqual(2u, std::uint32_t{rig.Host().Ram().Read16(tickAddress)}, L"two ticks, so the first was ended");
  }

  TEST_METHOD(Int20hEndsTheRun)
  {
    PcRig rig("Terminate");
    (void)rig.Load(TinyExe({0xCD, 0x20})); // int 20h
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Terminated);
    Assert::IsTrue(rig.Host().Services().Terminated());
  }

  TEST_METHOD(RefusedCallStopsTheRun)
  {
    PcRig rig("Fault");
    const Machine::LoadedProgram program = rig.Load(TinyExe({0xB4, 0xFF, 0xCD, 0x21})); // mov ah,0FFh; int 21h
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Fault);
    Assert::IsTrue(rig.Host().Services().Fault().has_value());
    const Machine::ServiceFault fault = rig.Host().Services().Fault().value_or(Machine::ServiceFault{});
    Assert::AreEqual(0x21u, std::uint32_t{fault.vector});
    Assert::AreEqual(0xFFu, std::uint32_t{fault.ah});
    Assert::AreEqual(std::uint32_t{program.loadSegment}, std::uint32_t{fault.cs});
    Assert::AreEqual(0x0002u, std::uint32_t{fault.ip}, L"the INT's own address");
  }

  TEST_METHOD(HaltWithInterruptsOffDeadlocks)
  {
    PcRig rig("Deadlock");
    (void)rig.Load(TinyExe({0xFA, 0xF4})); // cli; hlt
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Deadlocked);
  }

  // ADR-003's boot, on the whole machine: the reference, with the D5 byte, from its entry to the first
  // call of GetKey (CS:7616). It matches DOSBox-X's trace register for register (Tools/CompareTrace.py).
  // These figures pin it: any change to the CPU, a device or a service that alters the boot by one
  // instruction, one cycle or one byte of memory fails here, and has to say why.
  TEST_METHOD(ReferenceBootsToItsFirstKeyRead)
  {
    const std::vector<std::uint8_t> file = ReadReferenceBinary();
    Assert::IsFalse(file.empty(), L"ELITES.EXE at the repository root");
    ScratchDirectory directory("ReferenceBoot");
    Machine::DirectoryFileStore files(directory.Path());
    Machine::Pc::Desc desc;
    desc.startMoment = Machine::Dos::DateTime{1980, 1, 1, 0, 0, 0, 0};
    const auto pc = std::make_unique<Machine::Pc>(files, desc);
    Machine::ExeLoader::Desc load;
    load.pspSegment = 0x0813; // DOSBox-X's, so the traces compare
    Machine::LoadedProgram program;
    Assert::IsTrue(pc->Load(file, load, program) == Machine::LoadError::None);
    Assert::IsTrue(Machine::ExeLoader::PatchByte(pc->Ram(), program, 0x08F4, 0x25E4, 0x00, 0x01), L"D5");

    const Machine::Registers& regs = pc->Processor().Regs();
    while (!(regs.cs == program.loadSegment && regs.ip == 0x7616) && pc->Clock() < 2'000'000)
    {
      pc->Step();
      Assert::IsFalse(pc->Services().Fault().has_value(), L"no call refused");
    }
    Assert::AreEqual(0x7616u, std::uint32_t{regs.ip}, L"GetKey reached");
    Assert::AreEqual(std::uint64_t{39'255}, pc->Processor().InstructionCount());
    Assert::AreEqual(std::uint64_t{761'619}, pc->Clock());
    Assert::AreEqual(0x2Au, std::uint32_t{pc->Video().ModeControl()}, L"mode 4: 320x200 graphics, video on");
    Assert::AreEqual("e4cbdeaf063301c53f6bc54b65484c721ed6fa88b0c591f7098a789110e1f3fc",
                     Machine::Sha256::ToHex(Machine::Sha256::Of(pc->Ram().Bytes())).c_str());
  }

  // Paced time (ADR-008): a loop that counts is work, and work takes no time.
  TEST_METHOD(PacedWorkTakesNoTime)
  {
    PcRig rig("PacedWork");
    (void)rig.Load(TinyExe({0xB9, 0xE8, 0x03, 0xE2, 0xFE, 0xCD, 0x20})); // mov cx,1000; loop $; int 20h
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock(), L"no time passed");
    Assert::IsTrue(rig.Host().InstructionCycles() > 1'000u, L"the instructions still have their cycles");
  }

  // A loop that waits for the BIOS tick count to change changes nothing on its turns, so the clock
  // moves from event to event until the first IRQ0, at cycle 262,144, and the loop ends right there.
  TEST_METHOD(PacedWaitEndsAtTheEventItWaitsFor)
  {
    PcRig rig("PacedWait");
    (void)rig.Load(TinyExe({0xFB,                   // sti
                            0xBB, 0x40, 0x00,       // mov bx,40h
                            0x8E, 0xDB,             // mov ds,bx
                            0xA1, 0x6C, 0x00,       // mov ax,[6Ch]
                            0x3B, 0x06, 0x6C, 0x00, // wait: cmp ax,[6Ch]
                            0x74, 0xFA,             // je wait
                            0xCD, 0x20}));          // int 20h
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
  }

  TEST_METHOD(PacedHaltWaitsForTheNextInterrupt)
  {
    PcRig rig("PacedHalt");
    (void)rig.Load(TinyExe({0xFB, 0xF4, 0xCD, 0x20})); // sti; hlt; int 20h
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Terminated);
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
  }

  // An idle loop with interrupts off waits for ever, as on the real machine; the run reaches its end.
  TEST_METHOD(PacedIdleLoopRunsToTheLimit)
  {
    PcRig rig("PacedLimit");
    (void)rig.Load(TinyExe({0xFA, 0xEB, 0xFE})); // cli; jmp $
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    Assert::IsTrue(rig.Host().RunUntil(500'000) == Machine::StopReason::Reached);
    Assert::AreEqual(std::uint64_t{500'000}, rig.Host().Clock());
  }

  // A loop that changes something every turn never waits, so paced time would stand still for ever.
  TEST_METHOD(PacedSpinIsStopped)
  {
    PcRig rig("PacedSpin");
    (void)rig.Load(TinyExe({0xFB, 0x40, 0xEB, 0xFD})); // sti; inc ax; jmp back to the inc
    rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    rig.Host().SetSpinLimit(10'000);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Spinning);
    Assert::AreEqual(std::uint64_t{0}, rig.Host().Clock());
  }

  TEST_METHOD(RunsAreIdenticalToTheCycle)
  {
    std::uint64_t clocks[2] = {};
    std::uint64_t instructions[2] = {};
    for (int run = 0; run < 2; ++run)
    {
      PcRig rig("Repeat");
      (void)rig.Load(TinyExe({0xFB, 0x40, 0xEB, 0xFD})); // sti; inc ax; jmp back to the inc
      Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Reached);
      clocks[run] = rig.Host().Clock();
      instructions[run] = rig.Host().Processor().InstructionCount();
    }
    Assert::AreEqual(clocks[0], clocks[1]);
    Assert::AreEqual(instructions[0], instructions[1]);
  }
};

} // namespace MachineTests
