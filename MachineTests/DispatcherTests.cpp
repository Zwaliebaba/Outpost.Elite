#include "pch.h"

#include "Dispatcher.h"
#include "Firmware.h"
#include "InterruptSource.h"
#include "Memory.h"
#include "Pc.h"
#include "PcRig.h"
#include "PortRouter.h"

#include <cstdint>
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
constexpr std::uint16_t CODE_FLAGS = Machine::FLAGS_FIXED_ONES | Machine::FLAG_INTERRUPT | Machine::FLAG_DIRECTION | Machine::FLAG_CARRY;
constexpr std::uint8_t NOP = 0x90;

// Where an interrupted program was: what the ROM handlers' IRET returns to.
constexpr std::uint16_t INTERRUPTED_SEGMENT = 0x1234;
constexpr std::uint16_t INTERRUPTED_OFFSET = 0x5678;
constexpr std::uint16_t INTERRUPTED_FLAGS = 0xF2C3;

// The registers, a stack, and the vector pointing at a hooked handler.
void Prepare(Machine::Dispatcher& _dispatcher, Machine::Memory& _memory, const std::vector<std::uint8_t>& _hooks)
{
  Machine::Registers& regs = _dispatcher.Regs();
  regs.cs = CODE_SEGMENT;
  regs.ip = CODE_OFFSET;
  regs.ss = STACK_SEGMENT;
  regs.sp = STACK_POINTER;
  regs.ax = 0x1111;
  regs.flags = CODE_FLAGS;
  _memory.Write16(Machine::Memory::Linear(0, VECTOR * 4), HANDLER_OFFSET);
  _memory.Write16(Machine::Memory::Linear(0, VECTOR * 4 + 2), HANDLER_SEGMENT);
  _memory.Write8(CODE_SEGMENT, CODE_OFFSET, NOP);
  _dispatcher.SetHookMap(&_hooks);
}

[[nodiscard]] std::vector<std::uint8_t> HookedHandler()
{
  std::vector<std::uint8_t> hooks(Machine::Memory::SIZE_BYTES, 0);
  hooks[Machine::Memory::Linear(HANDLER_SEGMENT, HANDLER_OFFSET)] = 1;
  return hooks;
}

// A byte of memory a handler changed, and what it left there.
struct ByteChange
{
  std::uint32_t linear;
  std::uint8_t value;

  bool operator==(const ByteChange&) const = default;
};

// What one of the ROM's hardware interrupt handlers did, entered as the 8088 enters it and run to its IRET.
struct HandlerRun
{
  std::vector<ByteChange> memory;
  std::vector<Machine::PortRouter::Access> ports;
  Machine::Registers registers;
  Machine::Cycles instructionCycles = 0;
};

// The ROM's handler at F000:_handler, native on a Pc's Dispatcher (NativeFirmware.h), from the state an interrupt's
// entry leaves: the interrupted program's flags, CS and IP on the stack, IF and TF clear, and the BIOS's tick count at
// _ticksHigh:_ticksLow.
[[nodiscard]] HandlerRun RunRomHandler(std::uint16_t _handler, std::uint16_t _ticksLow, std::uint16_t _ticksHigh)
{
  PcRig rig("RomHandler");
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
  const std::vector<std::uint8_t> before(pc.Ram().Bytes().begin(), pc.Ram().Bytes().end());
  pc.Ports().SetLog(&run.ports);
  const Machine::Cycles cycles = pc.InstructionCycles();
  for (int step = 0; step < 64 && regs.cs != INTERRUPTED_SEGMENT; ++step)
  {
    pc.Step();
  }
  pc.Ports().SetLog(nullptr);
  Assert::AreEqual(std::uint32_t{INTERRUPTED_SEGMENT}, std::uint32_t{regs.cs}, L"the handler returned");
  const std::span<const std::uint8_t> after = pc.Ram().Bytes();
  for (std::uint32_t linear = 0; linear < Machine::Memory::SIZE_BYTES; ++linear)
  {
    if (after[linear] != before[linear])
      run.memory.push_back(ByteChange{linear, after[linear]});
  }
  run.registers = regs;
  run.instructionCycles = pc.InstructionCycles() - cycles;
  return run;
}

} // namespace

// The Dispatcher (ADR-011): the processor that executes none of the program's instructions, and the native firmware a Pc
// runs on it in the ROM's place.
TEST_CLASS(DispatcherTests)
{
public:
  // A hardware interrupt is taken as the 8088 takes one, and the step stops at the hooked handler: the flags, CS and IP
  // pushed, IF and TF cleared, CS:IP from the vector, and the entry's cycles. The interpreter took it the same way, register
  // for register, until D7 deleted it.
  TEST_METHOD(TakesAnInterruptAsThe8088Does)
  {
    const std::vector<std::uint8_t> hooks = HookedHandler();
    Machine::Memory memory;
    Machine::Dispatcher dispatcher(memory);
    OneRequest line(VECTOR);
    dispatcher.SetInterruptSource(&line);
    Prepare(dispatcher, memory, hooks);

    Assert::IsTrue(dispatcher.InterruptDue(), L"the request is seen");
    Assert::AreEqual(Machine::Dispatcher::HARDWARE_INTERRUPT_CYCLES, dispatcher.Step(), L"the entry's cycles");
    Assert::IsTrue(dispatcher.AtHook(), L"stopped at the handler");
    Assert::IsFalse(dispatcher.AtUnportedCode());
    const Machine::Registers& regs = dispatcher.Regs();
    Assert::AreEqual(std::uint32_t{HANDLER_SEGMENT}, std::uint32_t{regs.cs});
    Assert::AreEqual(std::uint32_t{HANDLER_OFFSET}, std::uint32_t{regs.ip});
    Assert::AreEqual(std::uint32_t{STACK_POINTER - 6}, std::uint32_t{regs.sp}, L"three words pushed");
    Assert::AreEqual(std::uint32_t{CODE_FLAGS & ~(Machine::FLAG_INTERRUPT | Machine::FLAG_TRAP)}, std::uint32_t{regs.flags},
                     L"IF and TF cleared");
    Assert::AreEqual(0x1111u, std::uint32_t{regs.ax}, L"nothing else changed");
    Assert::AreEqual(std::uint32_t{CODE_OFFSET}, std::uint32_t{memory.Read16(STACK_SEGMENT, regs.sp)}, L"IP");
    Assert::AreEqual(std::uint32_t{CODE_SEGMENT}, std::uint32_t{memory.Read16(STACK_SEGMENT, static_cast<std::uint16_t>(regs.sp + 2))},
                     L"CS");
    Assert::AreEqual(std::uint32_t{CODE_FLAGS}, std::uint32_t{memory.Read16(STACK_SEGMENT, static_cast<std::uint16_t>(regs.sp + 4))},
                     L"the flags as they were");
    Assert::AreEqual(std::uint64_t{1}, dispatcher.HardwareInterruptCount());
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

    PcRig rig("DispatcherUnported");
    (void)rig.Load(TinyExe({NOP, NOP}));
    Assert::IsTrue(rig.Host().RunUntil(1'000) == Machine::StopReason::Unported, L"the program's entry is not native");
  }

  // The machine's own code it does run: the ROM's IRET, where an interrupt nothing serves goes, and the INT 20h
  // at the PSP's offset 0, where a program's last RETF lands. A native entry calls int 33h with no mouse
  // driver, then returns far to its PSP, as the reference's Start does.
  TEST_METHOD(RunsTheRomsIretAndThePspsTerminate)
  {
    PcRig rig("DispatcherStubs");
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
        // IP is where Pc::CallInterrupt parks it; the rest is as it was.
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

  // The native ROM handlers (NativeFirmware.h) on each of the paths the ROM's code takes: the timer handler's four (a tick, a
  // tick that carries into the high word, the hour the day ends in, and the day's last tick), and the keyboard handler's.
  // Every byte of memory they change, the stack included, every port access, every register and the instruction clock are
  // what the ROM's code, interpreted from the same state, gave before D7 deleted the interpreter.
  TEST_METHOD(NativeFirmwareDoesWhatTheRomDid)
  {
    using Machine::PortRouter;
    constexpr std::uint16_t TIMER = Machine::Firmware::TIMER_HANDLER_OFFSET;
    constexpr std::uint16_t KEYBOARD = Machine::Firmware::KEYBOARD_HANDLER_OFFSET;
    // The tick count in the BIOS data area, and its rollover flag.
    constexpr std::uint32_t TICKS = 0x0046C;
    constexpr std::uint32_t ROLLOVER = 0x00470;
    // The stack below the interrupted program's frame, at 2800:01FA: the int 1Ch frame the timer handler makes, its IP E030
    // and CS F000 and the flags its comparison left, and below the frame what each handler pushed of AX and DS.
    constexpr std::uint32_t STACK = Machine::Memory::Linear(STACK_SEGMENT, 0x01F0);
    const auto timerStack = [](std::uint8_t _flagsLow)
    {
      return std::vector<ByteChange>{{STACK, 0x30},     {STACK + 1, 0xE0}, {STACK + 3, 0xF0}, {STACK + 4, _flagsLow}, {STACK + 5, 0xF2},
                                     {STACK + 6, 0xA5}, {STACK + 7, 0xA5}, {STACK + 8, 0x5A}, {STACK + 9, 0x5A}};
    };
    const auto with = [](std::vector<ByteChange> _first, const std::vector<ByteChange>& _then)
    {
      _first.insert(_first.end(), _then.begin(), _then.end());
      return _first;
    };
    const std::vector<PortRouter::Access> endOfInterrupt = {{0x20, 0x20, true, false}};
    struct Case
    {
      std::uint16_t handler;
      std::uint16_t ticksLow;
      std::uint16_t ticksHigh;
      std::vector<ByteChange> memory;
      std::vector<PortRouter::Access> ports;
      Machine::Cycles instructionCycles;
    };
    const Case cases[] = {
      {TIMER, 0x1000, 0x0005, with({{TICKS, 0x01}}, timerStack(0x97)), endOfInterrupt, 299},
      {TIMER, 0xFFFF, 0x0005, with({{TICKS, 0x00}, {TICKS + 1, 0x00}, {TICKS + 2, 0x06}}, timerStack(0x97)), endOfInterrupt, 316},
      {TIMER, 0x0100, 0x0018, with({{TICKS, 0x01}}, timerStack(0x02)), endOfInterrupt, 323},
      {TIMER, 0x00AF, 0x0018, with({{TICKS, 0x00}, {TICKS + 2, 0x00}, {ROLLOVER, 0x01}}, timerStack(0x46)), endOfInterrupt, 358},
      {KEYBOARD,
       0x1000,
       0x0005,
       {{STACK + 8, 0xA5}, {STACK + 9, 0xA5}},
       {{0x60, 0x00, false, false},
        {0x61, 0x4C, false, false},
        {0x61, 0xCC, true, false},
        {0x61, 0x4C, true, false},
        {0x20, 0x20, true, false}},
       127},
    };
    for (const Case& test : cases)
    {
      const HandlerRun run = RunRomHandler(test.handler, test.ticksLow, test.ticksHigh);
      Assert::IsTrue(run.memory == test.memory, L"every byte");
      Assert::IsTrue(run.ports == test.ports, L"every port access");
      Machine::Registers expected{};
      expected.ax = 0xA5A5;
      expected.ds = 0x5A5A;
      expected.ss = STACK_SEGMENT;
      expected.sp = STACK_POINTER;
      expected.cs = INTERRUPTED_SEGMENT;
      expected.ip = INTERRUPTED_OFFSET;
      expected.flags = INTERRUPTED_FLAGS;
      Assert::IsTrue(run.registers == expected, L"every register");
      Assert::AreEqual(test.instructionCycles, run.instructionCycles, L"the instruction clock");
    }
  }
};

} // namespace MachineTests
