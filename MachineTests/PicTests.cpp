#include "pch.h"

#include "Cpu.h"
#include "Memory.h"
#include "Pic.h"
#include "PortRouter.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t COMMAND = Machine::Pic::COMMAND_PORT;
constexpr std::uint16_t DATA = Machine::Pic::DATA_PORT;
constexpr std::uint8_t NON_SPECIFIC_EOI = 0x20;

[[nodiscard]] std::uint32_t Acknowledge(Machine::Pic& _pic)
{
  return _pic.AcknowledgeInterrupt();
}

} // namespace

TEST_CLASS(PicTests)
{
public:
  // What the BIOS leaves: vector base 8, and only IRQ0, IRQ1 and IRQ6 unmasked.
  TEST_METHOD(PowerOnStateIsWhatTheBiosLeaves)
  {
    Machine::Pic pic;
    Assert::AreEqual(0x08u, std::uint32_t{pic.VectorBase()});
    Assert::AreEqual(0xBCu, std::uint32_t{pic.In8(DATA)}, L"IMR");
    Assert::AreEqual(0x00u, std::uint32_t{pic.In8(COMMAND)}, L"IRR");
    Assert::IsFalse(pic.InterruptPending());

    for (std::uint8_t line = 0; line < 8; ++line)
    {
      Machine::Pic fresh;
      fresh.RaiseIrq(line);
      const bool unmasked = line == 0 || line == 1 || line == 6;
      Assert::AreEqual(unmasked, fresh.InterruptPending());
    }
  }

  // IRQ0 outranks IRQ1, and while IRQ0 is in service IRQ1 waits for its EOI.
  TEST_METHOD(HigherPriorityFirstAndLowerWaitsForEoi)
  {
    Machine::Pic pic;
    pic.RaiseIrq(1);
    pic.RaiseIrq(0);
    Assert::IsTrue(pic.InterruptPending());
    Assert::AreEqual(0x08u, Acknowledge(pic));
    Assert::AreEqual(0x01u, std::uint32_t{pic.InServiceRegister()});
    Assert::AreEqual(0x02u, std::uint32_t{pic.RequestRegister()});
    Assert::IsFalse(pic.InterruptPending(), L"IRQ1 is held off while IRQ0 is in service");

    pic.Out8(COMMAND, NON_SPECIFIC_EOI);
    Assert::AreEqual(0x00u, std::uint32_t{pic.InServiceRegister()});
    Assert::IsTrue(pic.InterruptPending());
    Assert::AreEqual(0x09u, Acknowledge(pic));
  }

  // A higher-priority request interrupts a lower one in service; a non-specific EOI then ends the
  // higher one, and a specific EOI ends a named level.
  TEST_METHOD(NestingAndSpecificEoi)
  {
    Machine::Pic pic;
    pic.RaiseIrq(1);
    Assert::AreEqual(0x09u, Acknowledge(pic));
    pic.RaiseIrq(0);
    Assert::IsTrue(pic.InterruptPending(), L"IRQ0 outranks IRQ1 in service");
    Assert::AreEqual(0x08u, Acknowledge(pic));
    Assert::AreEqual(0x03u, std::uint32_t{pic.InServiceRegister()});

    pic.Out8(COMMAND, NON_SPECIFIC_EOI);
    Assert::AreEqual(0x02u, std::uint32_t{pic.InServiceRegister()}, L"the non-specific EOI ends IRQ0, the highest");

    pic.RaiseIrq(6);
    Assert::IsFalse(pic.InterruptPending(), L"IRQ6 ranks below IRQ1 in service");
    pic.Out8(COMMAND, 0x61); // specific EOI, level 1
    Assert::AreEqual(0x00u, std::uint32_t{pic.InServiceRegister()});
    Assert::AreEqual(0x0Eu, Acknowledge(pic));
  }

  // A masked request is latched in IRR and is granted as soon as it is unmasked.
  TEST_METHOD(MaskedRequestIsHeldUntilUnmasked)
  {
    Machine::Pic pic;
    pic.RaiseIrq(2);
    Assert::IsFalse(pic.InterruptPending());
    Assert::AreEqual(0x04u, std::uint32_t{pic.RequestRegister()});

    pic.Out8(DATA, 0xB8);
    Assert::AreEqual(0xB8u, std::uint32_t{pic.In8(DATA)});
    Assert::IsTrue(pic.InterruptPending());
    Assert::AreEqual(0x0Au, Acknowledge(pic));
  }

  // A second edge while the first is still pending is not queued; one after acknowledge is.
  TEST_METHOD(SecondEdgeWhilePendingIsNotQueued)
  {
    Machine::Pic pic;
    pic.RaiseIrq(0);
    pic.RaiseIrq(0);
    Assert::AreEqual(0x08u, Acknowledge(pic));
    Assert::IsFalse(pic.InterruptPending());
    pic.Out8(COMMAND, NON_SPECIFIC_EOI);
    Assert::IsFalse(pic.InterruptPending(), L"nothing was queued behind the first edge");

    pic.RaiseIrq(0);
    Assert::AreEqual(0x08u, Acknowledge(pic));
    pic.RaiseIrq(0);
    Assert::AreEqual(0x01u, std::uint32_t{pic.RequestRegister()}, L"an edge while in service is latched again");
    Assert::IsFalse(pic.InterruptPending(), L"but waits for the EOI");
  }

  TEST_METHOD(Ocw3SelectsIrrOrIsrForReads)
  {
    Machine::Pic pic;
    pic.RaiseIrq(1);
    pic.RaiseIrq(0);
    (void)pic.AcknowledgeInterrupt();
    Assert::AreEqual(0x02u, std::uint32_t{pic.In8(COMMAND)}, L"IRR by default");
    pic.Out8(COMMAND, 0x0B);
    Assert::AreEqual(0x01u, std::uint32_t{pic.In8(COMMAND)}, L"ISR after OCW3 0x0B");
    Assert::AreEqual(0x01u, std::uint32_t{pic.In8(COMMAND)}, L"and it stays selected");
    pic.Out8(COMMAND, 0x0A);
    Assert::AreEqual(0x02u, std::uint32_t{pic.In8(COMMAND)}, L"IRR after OCW3 0x0A");
  }

  // ICW1-ICW4 reprogram the controller: ICW1 clears the mask, ICW2 moves the vectors.
  TEST_METHOD(InitializationSequenceReprograms)
  {
    Machine::Pic pic;
    pic.Out8(COMMAND, 0x11); // ICW1: edge, cascade, ICW4 follows
    Assert::AreEqual(0x00u, std::uint32_t{pic.In8(DATA)}, L"ICW1 clears IMR");
    pic.Out8(DATA, 0x77); // ICW2: low three bits are ignored
    pic.Out8(DATA, 0x04); // ICW3
    pic.Out8(DATA, 0x01); // ICW4: 8086 mode
    Assert::AreEqual(0x70u, std::uint32_t{pic.VectorBase()});
    pic.Out8(DATA, 0xFD); // OCW1: only IRQ1
    pic.RaiseIrq(0);
    pic.RaiseIrq(1);
    Assert::AreEqual(0x71u, Acknowledge(pic));

    pic.Out8(COMMAND, 0x12); // ICW1: single, no ICW4
    pic.Out8(DATA, 0x50);
    pic.Out8(DATA, 0xFE); // straight to OCW1
    Assert::AreEqual(0xFEu, std::uint32_t{pic.In8(DATA)});
    pic.RaiseIrq(0);
    Assert::AreEqual(0x50u, Acknowledge(pic));
  }

  TEST_METHOD(AcknowledgeWithNothingPendingIsSpuriousIrq7)
  {
    Machine::Pic pic;
    Assert::AreEqual(0x0Fu, Acknowledge(pic));
    Assert::AreEqual(0x00u, std::uint32_t{pic.InServiceRegister()});
  }

  // Automatic EOI (ICW4 bit 1) leaves nothing in service.
  TEST_METHOD(AutomaticEoiLeavesNothingInService)
  {
    Machine::Pic pic;
    pic.Out8(COMMAND, 0x13);
    pic.Out8(DATA, 0x08);
    pic.Out8(DATA, 0x03);
    pic.Out8(DATA, 0x00);
    pic.RaiseIrq(3);
    Assert::AreEqual(0x0Bu, Acknowledge(pic));
    Assert::AreEqual(0x00u, std::uint32_t{pic.InServiceRegister()});
  }

  // The whole path the game's timer handler takes: IRQ0 vectors through entry 8, the handler sends
  // EOI with out 20h, and a second IRQ0 can then come in.
  TEST_METHOD(CpuTakesIrqAndHandlerEndsItWithEoi)
  {
    Machine::Memory memory;
    Machine::Pic pic;
    Machine::PortRouter ports;
    Assert::IsTrue(ports.Map(0x20, 0x21, pic));
    Machine::Cpu cpu(memory, ports);
    cpu.SetInterruptSource(&pic);

    // Main program at 1000:0000: nop forever. Handler at 2000:0000: mov al,20h; out 20h,al; iret.
    const std::uint8_t program[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    const std::uint8_t handler[] = {0xB0, 0x20, 0xE6, 0x20, 0xCF};
    memory.Load(Machine::Memory::Linear(0x1000, 0), program);
    memory.Load(Machine::Memory::Linear(0x2000, 0), handler);
    memory.Write16(0x08u * 4, 0x0000);
    memory.Write16(0x08u * 4 + 2, 0x2000);
    Machine::Registers& regs = cpu.Regs();
    regs.cs = 0x1000;
    regs.ip = 0;
    regs.ss = 0x3000;
    regs.sp = 0x0100;
    regs.flags = static_cast<std::uint16_t>(Machine::FLAGS_FIXED_ONES | Machine::FLAG_INTERRUPT);

    pic.RaiseIrq(0);
    (void)cpu.Step(); // entry, and the handler's mov al,20h
    Assert::AreEqual(0x2000u, std::uint32_t{regs.cs});
    Assert::AreEqual(0x01u, std::uint32_t{pic.InServiceRegister()});
    pic.RaiseIrq(0);
    (void)cpu.Step(); // out 20h,al
    Assert::AreEqual(0x00u, std::uint32_t{pic.InServiceRegister()}, L"the handler's EOI");
    (void)cpu.Step(); // iret
    Assert::AreEqual(0x1000u, std::uint32_t{regs.cs});
    Assert::AreEqual(0x0000u, std::uint32_t{regs.ip});

    (void)cpu.Step(); // nothing shadows IRET, so the second IRQ0 comes before the next nop
    Assert::AreEqual(0x2000u, std::uint32_t{regs.cs}, L"the second IRQ0 is taken");
    Assert::AreEqual(0x01u, std::uint32_t{pic.InServiceRegister()});
  }
};

} // namespace MachineTests
