#include "pch.h"

#include "Cpu.h"
#include "Dispatcher.h"
#include "Firmware.h"
#include "InterruptSource.h"
#include "Memory.h"
#include "Pc.h"
#include "PcRig.h"
#include "PortRouter.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

// The INTR line with one request on it, answered with _vector.
class OneRequest final : public Machine::InterruptSource
{
public:
  explicit OneRequest(std::uint8_t _vector) noexcept
    : m_vector(_vector)
  {
  }

  [[nodiscard]] bool InterruptPending() const noexcept override
  {
    return m_pending;
  }

  [[nodiscard]] std::uint8_t AcknowledgeInterrupt() noexcept override
  {
    m_pending = false;
    return m_vector;
  }

private:
  std::uint8_t m_vector;
  bool m_pending = true;
};

constexpr std::uint8_t VECTOR = 0x40;
constexpr std::uint16_t HANDLER_SEGMENT = 0x3000;
constexpr std::uint16_t HANDLER_OFFSET = 0x0010;
constexpr std::uint16_t CODE_SEGMENT = 0x2000;
constexpr std::uint16_t CODE_OFFSET = 0x0100;
constexpr std::uint16_t STACK_SEGMENT = 0x2800;
constexpr std::uint16_t STACK_POINTER = 0x0200;
constexpr std::uint8_t NOP = 0x90;

// Where an interrupted program was: what the ROM handlers' IRET returns to.
constexpr std::uint16_t INTERRUPTED_SEGMENT = 0x1234;
constexpr std::uint16_t INTERRUPTED_OFFSET = 0x5678;
constexpr std::uint16_t INTERRUPTED_FLAGS = 0xF2C3;

// The same state on either processor: the registers, a stack, and the vector pointing at a hooked handler.
void Prepare(Machine::Processor& _processor, Machine::Memory& _memory, const std::vector<std::uint8_t>& _hooks)
{
  Machine::Registers& regs = _processor.Regs();
  regs.cs = CODE_SEGMENT;
  regs.ip = CODE_OFFSET;
  regs.ss = STACK_SEGMENT;
  regs.sp = STACK_POINTER;
  regs.ax = 0x1111;
  regs.flags = Machine::FLAGS_FIXED_ONES | Machine::FLAG_INTERRUPT | Machine::FLAG_DIRECTION | Machine::FLAG_CARRY;
  _memory.Write16(Machine::Memory::Linear(0, VECTOR * 4), HANDLER_OFFSET);
  _memory.Write16(Machine::Memory::Linear(0, VECTOR * 4 + 2), HANDLER_SEGMENT);
  _memory.Write8(CODE_SEGMENT, CODE_OFFSET, NOP);
  _processor.SetHookMap(&_hooks);
}

[[nodiscard]] std::vector<std::uint8_t> HookedHandler()
{
  std::vector<std::uint8_t> hooks(Machine::Memory::SIZE_BYTES, 0);
  hooks[Machine::Memory::Linear(HANDLER_SEGMENT, HANDLER_OFFSET)] = 1;
  return hooks;
}

// One of the ROM's hardware interrupt handlers, entered as the CPU enters it, from the same state on both
// processors, run to its IRET: interpreted from the ROM's code on one Pc, native on the other.
struct HandlerRun
{
  std::vector<std::uint8_t> memory;
  std::vector<Machine::PortRouter::Access> ports;
  Machine::Registers registers;
  Machine::Cycles instructionCycles = 0;
};

[[nodiscard]] HandlerRun RunRomHandler(Machine::ProcessorFactory _makeProcessor, std::uint16_t _handler, std::uint16_t _ticksLow,
                                       std::uint16_t _ticksHigh)
{
  PcRig rig(_makeProcessor == &Machine::MakeCpu ? "RomHandlerInterpreted" : "RomHandlerNative", _makeProcessor);
  Machine::Pc& pc = rig.Host();
  pc.SetTimeMode(Machine::TimeMode::Paced);
  Machine::Registers& regs = pc.Processor().Regs();
  regs.ss = STACK_SEGMENT;
  regs.sp = STACK_POINTER;
  regs.ax = 0xA5A5;
  regs.ds = 0x5A5A;
  regs.flags = Machine::FLAGS_FIXED_ONES; // as a hardware interrupt's entry leaves them: IF and TF clear
  for (const std::uint16_t word : {INTERRUPTED_FLAGS, INTERRUPTED_SEGMENT, INTERRUPTED_OFFSET})
  {
    regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
    pc.Ram().Write16(regs.ss, regs.sp, word);
  }
  pc.Ram().Write16(Machine::Firmware::DATA_SEGMENT, 0x6C, _ticksLow);
  pc.Ram().Write16(Machine::Firmware::DATA_SEGMENT, 0x6E, _ticksHigh);
  regs.cs = Machine::Firmware::ROM_SEGMENT;
  regs.ip = _handler;

  HandlerRun run;
  pc.Ports().SetLog(&run.ports);
  const Machine::Cycles before = pc.InstructionCycles();
  for (int step = 0; step < 64 && regs.cs != INTERRUPTED_SEGMENT; ++step)
  {
    pc.Step();
  }
  pc.Ports().SetLog(nullptr);
  Assert::AreEqual(std::uint32_t{INTERRUPTED_SEGMENT}, std::uint32_t{regs.cs}, L"the handler returned");
  const std::span<const std::uint8_t> bytes = pc.Ram().Bytes();
  run.memory.assign(bytes.begin(), bytes.end());
  run.registers = regs;
  run.instructionCycles = pc.InstructionCycles() - before;
  return run;
}

} // namespace

// The Dispatcher (ADR-011): a processor that executes none of the program's instructions, and the native
// firmware a Pc on it runs in the ROM's place.
TEST_CLASS(DispatcherTests)
{
public:
  // From the same state, the Dispatcher takes a hardware interrupt exactly as the interpreter does, and both
  // stop at the hooked handler: the same registers, the same frame on the stack, the same cycles.
  TEST_METHOD(TakesAnInterruptAsTheCpuDoes)
  {
    const std::vector<std::uint8_t> hooks = HookedHandler();
    Machine::Memory interpretedMemory;
    Machine::PortRouter ports;
    Machine::Cpu cpu(interpretedMemory, ports);
    OneRequest interpretedLine(VECTOR);
    cpu.SetInterruptSource(&interpretedLine);
    Prepare(cpu, interpretedMemory, hooks);

    Machine::Memory nativeMemory;
    Machine::Dispatcher dispatcher(nativeMemory);
    OneRequest nativeLine(VECTOR);
    dispatcher.SetInterruptSource(&nativeLine);
    Prepare(dispatcher, nativeMemory, hooks);

    Assert::IsTrue(cpu.InterruptDue() && dispatcher.InterruptDue(), L"both see the request");
    const std::uint32_t interpretedCycles = cpu.Step();
    const std::uint32_t nativeCycles = dispatcher.Step();
    Assert::AreEqual(interpretedCycles, nativeCycles, L"the entry's cycles");
    Assert::IsTrue(cpu.AtHook() && dispatcher.AtHook(), L"both stop at the handler");
    Assert::IsTrue(cpu.Regs() == dispatcher.Regs(), L"the same registers");
    Assert::IsTrue(std::ranges::equal(interpretedMemory.Bytes(), nativeMemory.Bytes()), L"the same frame on the stack");
    Assert::AreEqual(std::uint64_t{1}, dispatcher.HardwareInterruptCount());
    Assert::IsFalse(dispatcher.AtUnportedCode());
  }

  // Code no native routine stands in for is not run: the step changes nothing, and a Pc stops there.
  TEST_METHOD(StopsAtCodeNoNativeRoutineStandsIn)
  {
    Machine::Memory memory;
    Machine::Dispatcher dispatcher(memory);
    const std::vector<std::uint8_t> hooks = HookedHandler();
    Prepare(dispatcher, memory, hooks);
    const Machine::Registers before = dispatcher.Regs();
    Assert::AreEqual(0u, dispatcher.Step(), L"no cycles");
    Assert::IsTrue(dispatcher.AtUnportedCode(), L"the NOP is not run");
    Assert::IsTrue(dispatcher.Regs() == before, L"nothing changed");
    Assert::AreEqual(std::uint64_t{0}, dispatcher.InstructionCount());

    PcRig rig("DispatcherUnported", &Machine::MakeDispatcher);
    (void)rig.Load(TinyExe({NOP, NOP}));
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Machine::StopReason::Unported, L"the program's entry is not native");
  }

  // The machine's own code it does run: the ROM's IRET, where an interrupt nothing serves goes, and the INT 20h
  // at the PSP's offset 0, where a program's last RETF lands. A native entry calls int 33h with no mouse
  // driver, then returns far to its PSP, as the reference's Start does.
  TEST_METHOD(RunsTheRomsIretAndThePspsTerminate)
  {
    PcRig rig("DispatcherStubs", &Machine::MakeDispatcher);
    const Machine::LoadedProgram program = rig.Load(TinyExe({NOP}));
    Machine::Pc& pc = rig.Host();
    const std::uint16_t psp = program.pspSegment;
    pc.Hook(
      program.loadSegment, 0, "Entry",
      [psp](Machine::Pc& _pc)
      {
        Machine::Registers& regs = _pc.Processor().Regs();
        const Machine::Registers before = regs;
        _pc.CallInterrupt(0x33);
        // IP is where Pc::CallInterrupt parks it, as on either processor; the rest is as it was.
        Machine::Registers after = regs;
        after.ip = before.ip;
        Assert::IsTrue(after == before, L"the ROM's IRET put everything back");
        for (const std::uint16_t word : {psp, std::uint16_t{0}})
        {
          regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
          _pc.Ram().Write16(regs.ss, regs.sp, word);
        }
        _pc.ReturnFar();
      },
      Machine::NativeContract{}, Machine::NativeReturn::Far);
    Assert::IsTrue(pc.RunUntil(1'000) == Machine::StopReason::Terminated, L"the PSP's INT 20h ends the program");
    Assert::AreEqual(std::uint64_t{2}, pc.Processor().InstructionCount(), L"the IRET and the INT 20h");
  }

  // The native ROM handlers (NativeFirmware.h) against the ROM's code, interpreted, from the same state: the
  // timer handler on each of its four paths (a tick, a tick that carries into the high word, the hour the day
  // ends in, and the day's last tick), and the keyboard handler. Every byte of memory, the stack included,
  // every port access, every register and the instruction clock come out the same.
  TEST_METHOD(NativeFirmwareDoesWhatTheRomDoes)
  {
    struct Case
    {
      std::uint16_t handler;
      std::uint16_t ticksLow;
      std::uint16_t ticksHigh;
    };
    constexpr Case CASES[] = {
      {Machine::Firmware::TIMER_HANDLER_OFFSET, 0x1000, 0x0005},    {Machine::Firmware::TIMER_HANDLER_OFFSET, 0xFFFF, 0x0005},
      {Machine::Firmware::TIMER_HANDLER_OFFSET, 0x0100, 0x0018},    {Machine::Firmware::TIMER_HANDLER_OFFSET, 0x00AF, 0x0018},
      {Machine::Firmware::KEYBOARD_HANDLER_OFFSET, 0x1000, 0x0005},
    };
    for (const Case& test : CASES)
    {
      const HandlerRun interpreted = RunRomHandler(&Machine::MakeCpu, test.handler, test.ticksLow, test.ticksHigh);
      const HandlerRun native = RunRomHandler(&Machine::MakeDispatcher, test.handler, test.ticksLow, test.ticksHigh);
      Assert::IsTrue(interpreted.memory == native.memory, L"every byte");
      Assert::IsTrue(interpreted.ports == native.ports, L"every port access");
      Assert::IsTrue(interpreted.registers == native.registers, L"every register");
      Assert::AreEqual(interpreted.instructionCycles, native.instructionCycles, L"the instruction clock");
    }
  }
};

} // namespace MachineTests
