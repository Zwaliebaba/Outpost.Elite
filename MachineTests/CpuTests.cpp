#include "pch.h"

#include "Cpu.h"
#include "HostServices.h"
#include "Memory.h"
#include "PortBus.h"

#include <initializer_list>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t CODE_SEGMENT = 0x1000;
constexpr std::uint16_t STACK_SEGMENT = 0x2000;
constexpr std::uint16_t STACK_TOP = 0x0100;

class SilentBus final : public Machine::PortBus
{
public:
  [[nodiscard]] std::uint8_t In8(std::uint16_t /*_port*/) override
  {
    return 0xFF;
  }

  void Out8(std::uint16_t /*_port*/, std::uint8_t /*_value*/) override {}
};

// Services INT 21h by putting a marker in AX, and declines every other vector.
class RecordingHost final : public Machine::HostServices
{
public:
  [[nodiscard]] bool ServiceInterrupt(Machine::Cpu& _cpu, std::uint8_t _vector) override
  {
    m_vectors.push_back(_vector);
    if (_vector != 0x21)
    {
      return false;
    }
    _cpu.Regs().ax = 0x4C00;
    return true;
  }

  [[nodiscard]] const std::vector<std::uint8_t>& Vectors() const noexcept
  {
    return m_vectors;
  }

private:
  std::vector<std::uint8_t> m_vectors;
};

// A machine with code at CODE_SEGMENT:0000 and an empty stack at STACK_SEGMENT:STACK_TOP.
class Rig
{
public:
  Rig(std::initializer_list<std::uint8_t> _code)
    : m_cpu(m_memory, m_ports)
  {
    std::uint16_t offset = 0;
    for (const std::uint8_t value : _code)
    {
      m_memory.Write8(CODE_SEGMENT, offset++, value);
    }
    Machine::Registers& regs = m_cpu.Regs();
    regs.cs = CODE_SEGMENT;
    regs.ip = 0;
    regs.ss = STACK_SEGMENT;
    regs.sp = STACK_TOP;
    regs.flags = static_cast<std::uint16_t>(Machine::FLAGS_FIXED_ONES | Machine::FLAG_INTERRUPT);
  }

  [[nodiscard]] Machine::Memory& Ram() noexcept
  {
    return m_memory;
  }

  [[nodiscard]] Machine::Cpu& Processor() noexcept
  {
    return m_cpu;
  }

  [[nodiscard]] Machine::Registers& Regs() noexcept
  {
    return m_cpu.Regs();
  }

  // The word at SS:SP + _depth * 2.
  [[nodiscard]] std::uint32_t Stack(std::uint16_t _depth) const noexcept
  {
    const std::uint16_t offset = static_cast<std::uint16_t>(m_cpu.Regs().sp + _depth * 2);
    return m_memory.Read16(m_cpu.Regs().ss, offset);
  }

private:
  Machine::Memory m_memory;
  SilentBus m_ports;
  Machine::Cpu m_cpu;
};

} // namespace

TEST_CLASS(CpuTests)
{
public:
  // Plan §7.1: the game's int 0 handler saturates quotients, and it reads the return address to
  // find the faulting DIV. The 8088 pushes the address of the NEXT instruction; later CPUs push the
  // DIV itself. Divide 0x1000 by 2: the quotient does not fit in AL.
  TEST_METHOD(DivideOverflowVectorsThroughEntryZeroWithNextInstructionAddress)
  {
    Rig rig({0xF6, 0x77, 0x10}); // div byte [bx+10h]: three bytes, so a 286-style return would differ
    rig.Ram().Write16(0x0000u, 0x5678);
    rig.Ram().Write16(0x0002u, 0x1234);
    Machine::Registers& regs = rig.Regs();
    regs.ds = 0x3000;
    regs.bx = 0x0020;
    rig.Ram().Write8(0x3000, 0x0030, 0x02);
    regs.ax = 0x1000;

    (void)rig.Processor().Step();

    Assert::AreEqual(0x1234u, std::uint32_t{regs.cs});
    Assert::AreEqual(0x5678u, std::uint32_t{regs.ip});
    Assert::AreEqual(static_cast<std::uint32_t>(STACK_TOP - 6), std::uint32_t{regs.sp});
    Assert::AreEqual(0x0003u, rig.Stack(0), L"return IP: the instruction after the DIV");
    Assert::AreEqual(std::uint32_t{CODE_SEGMENT}, rig.Stack(1), L"return CS");
    // The flags of the microcode's first step, AH - divisor = 10h - 2: AF set, the rest clear.
    Assert::AreEqual(0xF212u, rig.Stack(2), L"pushed flags");
    Assert::IsFalse((regs.flags & Machine::FLAG_INTERRUPT) != 0, L"IF is cleared on entry");
    Assert::AreEqual(0x1000u, std::uint32_t{regs.ax}, L"AX is left alone");
  }

  TEST_METHOD(SignedDivideOfMinus128Faults)
  {
    // idiv bl with AX = -256 and BL = 2: -128 fits in a byte, but the 8088 still faults on it.
    Rig rig({0xF6, 0xFB});
    rig.Ram().Write16(0x0000u, 0x0000);
    rig.Ram().Write16(0x0002u, 0x4000);
    rig.Regs().ax = 0xFF00;
    rig.Regs().bx = 0x0002;

    (void)rig.Processor().Step();

    Assert::AreEqual(0x4000u, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(0x0002u, rig.Stack(0));
  }

  TEST_METHOD(RepMovsbCopiesTheWholeBlockInOneStep)
  {
    Rig rig({0xF3, 0xA4}); // rep movsb
    Machine::Registers& regs = rig.Regs();
    regs.ds = 0x3000;
    regs.si = 0x0010;
    regs.es = 0x4000;
    regs.di = 0x0020;
    regs.cx = 5;
    for (std::uint16_t index = 0; index < 5; ++index)
    {
      rig.Ram().Write8(0x3000, static_cast<std::uint16_t>(0x0010 + index), static_cast<std::uint8_t>('A' + index));
    }

    (void)rig.Processor().Step();

    for (std::uint16_t index = 0; index < 5; ++index)
    {
      Assert::AreEqual(static_cast<std::uint32_t>('A' + index),
                       std::uint32_t{rig.Ram().Read8(0x4000, static_cast<std::uint16_t>(0x0020 + index))});
    }
    Assert::AreEqual(0x0000u, std::uint32_t{rig.Ram().Read8(0x4000, 0x0025)}, L"nothing past the block");
    Assert::AreEqual(0u, std::uint32_t{regs.cx});
    Assert::AreEqual(0x0015u, std::uint32_t{regs.si});
    Assert::AreEqual(0x0025u, std::uint32_t{regs.di});
    Assert::AreEqual(0x0002u, std::uint32_t{regs.ip});
    Assert::AreEqual(std::uint64_t{1}, rig.Processor().InstructionCount());
  }

  TEST_METHOD(HostServicesInterceptsAnInt)
  {
    Rig rig({0xCD, 0x21, 0xCD, 0x10}); // int 21h; int 10h
    rig.Ram().Write16(0x10u * 4, 0x0040);
    rig.Ram().Write16(0x10u * 4 + 2, 0xF000);
    RecordingHost host;
    rig.Processor().SetHostServices(&host);

    (void)rig.Processor().Step();

    Assert::AreEqual(0x4C00u, std::uint32_t{rig.Regs().ax}, L"the service ran");
    Assert::AreEqual(std::uint32_t{CODE_SEGMENT}, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(0x0002u, std::uint32_t{rig.Regs().ip}, L"execution continues after the INT");
    Assert::AreEqual(std::uint32_t{STACK_TOP}, std::uint32_t{rig.Regs().sp}, L"nothing was pushed");

    // A vector the host declines goes through the interrupt table as usual.
    (void)rig.Processor().Step();

    Assert::AreEqual(0xF000u, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(0x0040u, std::uint32_t{rig.Regs().ip});
    Assert::AreEqual(0x0004u, rig.Stack(0));
    Assert::AreEqual(std::size_t{2}, host.Vectors().size());
  }

  TEST_METHOD(HardwareInterruptWaitsOneInstructionAfterSti)
  {
    Rig rig({0xFB, 0x90, 0x90}); // sti; nop; nop
    rig.Ram().Write16(0x08u * 4, 0x0000);
    rig.Ram().Write16(0x08u * 4 + 2, 0x5000);
    rig.Regs().flags = Machine::FLAGS_FIXED_ONES;
    rig.Processor().RequestInterrupt(0x08);

    (void)rig.Processor().Step(); // sti
    (void)rig.Processor().Step(); // the nop after it still runs
    Assert::AreEqual(0x0002u, std::uint32_t{rig.Regs().ip});
    Assert::AreEqual(std::uint32_t{CODE_SEGMENT}, std::uint32_t{rig.Regs().cs});

    (void)rig.Processor().Step(); // now the interrupt is taken, and its handler's first instruction runs
    Assert::AreEqual(0x5000u, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(0x0002u, rig.Stack(0), L"returns to the second nop");
  }
};

} // namespace MachineTests
