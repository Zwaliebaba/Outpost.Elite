#include "pch.h"

#include "Cpu.h"
#include "HostServices.h"
#include "InstructionObserver.h"
#include "InterruptSource.h"
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
  [[nodiscard]] bool ServiceInterrupt(Machine::Registers& _regs, std::uint8_t _vector) override
  {
    m_vectors.push_back(_vector);
    if (_vector != 0x21)
    {
      return false;
    }
    _regs.ax = 0x4C00;
    return true;
  }

  [[nodiscard]] const std::vector<std::uint8_t>& Vectors() const noexcept
  {
    return m_vectors;
  }

private:
  std::vector<std::uint8_t> m_vectors;
};

// The INTR line with one request on it at a time, holding the vector it will answer with.
class ScriptedSource final : public Machine::InterruptSource
{
public:
  void Raise(std::uint8_t _vector) noexcept
  {
    m_vector = _vector;
    m_pending = true;
  }

  [[nodiscard]] bool InterruptPending() const noexcept override
  {
    ++m_queries;
    return m_pending;
  }

  [[nodiscard]] std::uint8_t AcknowledgeInterrupt() noexcept override
  {
    m_pending = false;
    ++m_acknowledged;
    return m_vector;
  }

  [[nodiscard]] std::size_t Queries() const noexcept
  {
    return m_queries;
  }

  [[nodiscard]] std::size_t Acknowledged() const noexcept
  {
    return m_acknowledged;
  }

private:
  mutable std::size_t m_queries = 0;
  std::size_t m_acknowledged = 0;
  std::uint8_t m_vector = 0;
  bool m_pending = false;
};

// Keeps CS:IP of every instruction it is told about.
class AddressRecorder final : public Machine::InstructionObserver
{
public:
  void BeforeInstruction(const Machine::Registers& _registers) override
  {
    m_addresses.push_back(Machine::Memory::Linear(_registers.cs, _registers.ip));
  }

  [[nodiscard]] const std::vector<std::uint32_t>& Addresses() const noexcept
  {
    return m_addresses;
  }

private:
  std::vector<std::uint32_t> m_addresses;
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

  // The CPU asks its InterruptSource at an instruction boundary, acknowledges, and vectors through
  // the entry the acknowledge returned; the handler's first instruction runs in the same step.
  TEST_METHOD(HardwareInterruptVectorsThroughTheAcknowledgedEntry)
  {
    Rig rig({0x90, 0x90}); // nop; nop
    rig.Ram().Write16(0x09u * 4, 0x0010);
    rig.Ram().Write16(0x09u * 4 + 2, 0x5000);
    rig.Ram().Write8(0x5000, 0x0010, 0x90);
    ScriptedSource source;
    rig.Processor().SetInterruptSource(&source);

    (void)rig.Processor().Step();
    Assert::AreEqual(std::size_t{0}, source.Acknowledged(), L"nothing pending: nothing acknowledged");
    source.Raise(0x09);
    (void)rig.Processor().Step();

    Assert::AreEqual(std::size_t{1}, source.Acknowledged());
    Assert::AreEqual(0x5000u, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(0x0011u, std::uint32_t{rig.Regs().ip}, L"the handler's first instruction ran");
    Assert::AreEqual(0x0001u, rig.Stack(0), L"returns to the second nop");
    Assert::AreEqual(std::uint32_t{CODE_SEGMENT}, rig.Stack(1));
    Assert::IsFalse((rig.Regs().flags & Machine::FLAG_INTERRUPT) != 0, L"IF is cleared on entry");
  }

  TEST_METHOD(HardwareInterruptWaitsOneInstructionAfterSti)
  {
    Rig rig({0xFB, 0x90, 0x90}); // sti; nop; nop
    rig.Ram().Write16(0x08u * 4, 0x0000);
    rig.Ram().Write16(0x08u * 4 + 2, 0x5000);
    rig.Regs().flags = Machine::FLAGS_FIXED_ONES;
    ScriptedSource source;
    rig.Processor().SetInterruptSource(&source);
    source.Raise(0x08);

    (void)rig.Processor().Step(); // sti
    Assert::AreEqual(std::size_t{0}, source.Queries(), L"IF clear: the source is not even asked");
    (void)rig.Processor().Step(); // the nop after it still runs
    Assert::AreEqual(0x0002u, std::uint32_t{rig.Regs().ip});
    Assert::AreEqual(std::uint32_t{CODE_SEGMENT}, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(std::size_t{0}, source.Acknowledged());

    (void)rig.Processor().Step(); // now the interrupt is taken, and its handler's first instruction runs
    Assert::AreEqual(0x5000u, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(0x0002u, rig.Stack(0), L"returns to the second nop");
  }

  // On the 8088 any MOV or POP to a segment register holds interrupts off for one instruction.
  TEST_METHOD(SegmentLoadShadowsTheNextInstruction)
  {
    Rig rig({0x8E, 0xD0, 0x90, 0x90}); // mov ss,ax; nop; nop
    rig.Regs().ax = STACK_SEGMENT;
    rig.Ram().Write16(0x08u * 4, 0x0000);
    rig.Ram().Write16(0x08u * 4 + 2, 0x5000);
    ScriptedSource source;
    rig.Processor().SetInterruptSource(&source);

    (void)rig.Processor().Step(); // mov ss,ax
    source.Raise(0x08);
    (void)rig.Processor().Step(); // the nop in its shadow
    Assert::AreEqual(std::size_t{0}, source.Acknowledged());
    Assert::AreEqual(0x0003u, std::uint32_t{rig.Regs().ip});

    (void)rig.Processor().Step();
    Assert::AreEqual(std::size_t{1}, source.Acknowledged());
    Assert::AreEqual(0x0003u, rig.Stack(0));
  }

  // HLT waits for an interrupt; with nothing pending it idles without executing.
  TEST_METHOD(HaltedCpuWakesForAnInterrupt)
  {
    Rig rig({0xF4, 0x90}); // hlt; nop
    rig.Ram().Write16(0x08u * 4, 0x0000);
    rig.Ram().Write16(0x08u * 4 + 2, 0x5000);
    ScriptedSource source;
    rig.Processor().SetInterruptSource(&source);

    (void)rig.Processor().Step();
    Assert::IsTrue(rig.Processor().Halted());
    Assert::AreEqual(Machine::Cpu::HALT_IDLE_CYCLES, rig.Processor().Step());
    Assert::AreEqual(0x0001u, std::uint32_t{rig.Regs().ip});

    source.Raise(0x08);
    (void)rig.Processor().Step();
    Assert::IsFalse(rig.Processor().Halted());
    Assert::AreEqual(0x5000u, std::uint32_t{rig.Regs().cs});
    Assert::AreEqual(0x0001u, rig.Stack(0), L"returns past the HLT");
  }

  // The execution map marks where each instruction starts, at its first prefix byte, and nothing else.
  TEST_METHOD(ExecutionMapMarksInstructionStarts)
  {
    Rig rig({0xB8, 0x34, 0x12, 0x26, 0x8B, 0x07, 0x90}); // mov ax,1234h; mov ax,es:[bx]; nop
    std::vector<std::uint8_t> map;
    rig.Processor().SetExecutionMap(&map);
    Assert::AreEqual(static_cast<std::size_t>(Machine::Memory::SIZE_BYTES), map.size());

    for (int step = 0; step < 3; ++step)
    {
      (void)rig.Processor().Step();
    }
    const std::uint32_t base = Machine::Memory::Linear(CODE_SEGMENT, 0);
    for (std::uint32_t offset = 0; offset < 8; ++offset)
    {
      const bool start = offset == 0 || offset == 3 || offset == 6;
      Assert::AreEqual(start ? 1u : 0u, std::uint32_t{map[base + offset]});
    }

    rig.Processor().SetExecutionMap(nullptr);
    rig.Regs().ip = 0;
    map[base] = 0;
    (void)rig.Processor().Step();
    Assert::AreEqual(0u, std::uint32_t{map[base]}, L"no map, no marks");
  }

  // A step that takes an interrupt reports the handler's first instruction, the one it executes,
  // and never the interrupted one, which runs only after the IRET.
  TEST_METHOD(ObserverSeesEachExecutedInstructionOnce)
  {
    Rig rig({0x90, 0x90}); // nop; nop
    rig.Ram().Write16(0x08u * 4, 0x0010);
    rig.Ram().Write16(0x08u * 4 + 2, 0x5000);
    rig.Ram().Write8(0x5000, 0x0010, 0x90); // nop
    rig.Ram().Write8(0x5000, 0x0011, 0xCF); // iret
    ScriptedSource source;
    AddressRecorder recorder;
    rig.Processor().SetInterruptSource(&source);
    rig.Processor().SetInstructionObserver(&recorder);

    (void)rig.Processor().Step();
    source.Raise(0x08);
    for (int step = 0; step < 3; ++step)
    {
      (void)rig.Processor().Step();
    }

    const std::uint32_t code = Machine::Memory::Linear(CODE_SEGMENT, 0);
    const std::uint32_t handler = Machine::Memory::Linear(0x5000, 0x0010);
    const std::vector<std::uint32_t> expected = {code, handler, handler + 1, code + 1};
    Assert::IsTrue(recorder.Addresses() == expected, L"nop, handler nop, iret, second nop");

    rig.Processor().SetInstructionObserver(nullptr);
    (void)rig.Processor().Step();
    Assert::AreEqual(std::size_t{4}, recorder.Addresses().size(), L"no observer, no reports");
  }
};

} // namespace MachineTests
