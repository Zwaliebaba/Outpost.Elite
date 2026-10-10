#include "pch.h"

#include "NativeCode.h"
#include "Pc.h"
#include "PcRig.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t MAIN = 0x0000;
constexpr std::uint16_t FINISH = 0x0005;
constexpr std::uint16_t ROUTINE = 0x0010;
constexpr std::uint16_t HANDLER = 0x0038;
constexpr std::uint16_t COUNTER = 0x0040;
constexpr std::uint8_t VECTOR = 0x60;
constexpr std::uint16_t COUNTER_START = 0x1234;

// A program whose counter word at 0040h starts at 1234h. Its code is native, so none of its bytes but the counter's matter.
std::vector<std::uint8_t> Program()
{
  std::vector<std::uint8_t> code(COUNTER, 0x90);
  code.insert(code.end(), {COUNTER_START & 0xFF, COUNTER_START >> 8});
  std::vector<std::uint8_t> file = TinyExe({});
  file.insert(file.end(), code.begin(), code.end());
  const std::size_t fileBytes = file.size();
  file[2] = static_cast<std::uint8_t>(fileBytes % 512);
  file[4] = static_cast<std::uint8_t>((fileBytes + 511) / 512);
  return file;
}

// The program's entry: PUSH CS; POP DS; CALL 0010h, natively. It calls the routine as the CALL does, the return address
// FINISH on the stack, and the Dispatcher goes on at the routine.
void Main(Machine::Pc& _pc)
{
  Machine::Registers& regs = _pc.Processor().Regs();
  regs.ds = regs.cs;
  regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
  _pc.Ram().Write16(regs.ss, regs.sp, FINISH);
  regs.ip = ROUTINE;
}

// Where the routine returns to: INT 20h, natively, which ends the program.
void Finish(Machine::Pc& _pc)
{
  _pc.CallInterrupt(0x20);
}

// A routine that waits for the BIOS tick count to change, with interrupts on, through Pc::Wait.
void WaitForTick(Machine::Pc& _pc)
{
  Machine::Registers& regs = _pc.Processor().Regs();
  regs.flags = static_cast<std::uint16_t>(regs.flags | Machine::FLAG_INTERRUPT);
  regs.ax = 0x40;
  regs.es = regs.ax;
  regs.bx = _pc.Ram().Read16(regs.es, 0x6C);
  while (_pc.Ram().Read16(regs.es, 0x6C) == regs.bx)
    _pc.Wait();
  _pc.ReturnNear();
}

// The same wait turn by turn: Pc::LoopTurn where the original's JE jumped back, so paced time sees the same turns it saw in
// the original's loop (sti; mov ax,40h; mov es,ax; mov bx,es:[6Ch]; wait: cmp bx,es:[6Ch]; je wait; ret).
void WaitForTickByTurns(Machine::Pc& _pc)
{
  Machine::Registers& regs = _pc.Processor().Regs();
  regs.flags = static_cast<std::uint16_t>(regs.flags | Machine::FLAG_INTERRUPT);
  regs.ax = 0x40;
  regs.es = regs.ax;
  regs.bx = _pc.Ram().Read16(regs.es, 0x6C);
  while (_pc.Ram().Read16(regs.es, 0x6C) == regs.bx)
    _pc.LoopTurn();
  _pc.ReturnNear();
}

// The same wait de-assembled (ADR-015): no registers, and a turn signature where the original's JE jumped back, the
// loop's offset and the tick count it carries in BX.
void WaitForTickBySignature(Machine::Pc& _pc)
{
  constexpr std::uint16_t WAIT_LOOP = ROUTINE + 0x0B; // wait: cmp bx,es:[6Ch]
  Machine::Registers& regs = _pc.Processor().Regs();
  regs.flags = static_cast<std::uint16_t>(regs.flags | Machine::FLAG_INTERRUPT);
  const std::uint16_t ticks = _pc.Ram().Read16(0x40, 0x6C);
  while (_pc.Ram().Read16(0x40, 0x6C) == ticks)
  {
    const std::array signature{WAIT_LOOP, ticks};
    _pc.LoopTurn(signature);
  }
  _pc.ReturnNear();
}

// A routine that adds _step to the counter, leaves it in AX and _dx in DX.
Machine::NativeRoutine CountUp(std::uint16_t _step, std::uint16_t _dx)
{
  return [_step, _dx](Machine::Pc& _pc)
  {
    Machine::Registers& regs = _pc.Processor().Regs();
    const std::uint16_t value = static_cast<std::uint16_t>(_pc.Ram().Read16(regs.ds, COUNTER) + _step);
    _pc.Ram().Write16(regs.ds, COUNTER, value);
    regs.ax = value;
    regs.dx = _dx;
    _pc.ReturnNear();
  };
}

// Program() in paced time, its entry and its end hooked: what a test hooks at ROUTINE runs as the routine the entry calls.
class NativeRig
{
public:
  explicit NativeRig(std::string_view _name)
    : m_rig(_name)
  {
    m_program = m_rig.Load(Program());
    m_rig.Host().SetTimeMode(Machine::TimeMode::Paced);
    m_rig.Host().Hook(m_program.loadSegment, MAIN, "Main", &Main, {});
    m_rig.Host().Hook(m_program.loadSegment, FINISH, "Finish", &Finish, {});
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_rig.Host();
  }

  void Hook(Machine::NativeRoutine _routine, const Machine::NativeContract& _contract = {},
            Machine::NativeWait _wait = Machine::NativeWait::Never)
  {
    m_rig.Host().Hook(m_program.loadSegment, ROUTINE, "Routine", std::move(_routine), _contract, Machine::NativeReturn::Near, _wait);
  }

  // Points the vector table's entry for VECTOR at a handler, hooked: MOV BX,5555h; IRET, natively.
  void InstallHandler()
  {
    m_rig.Host().Ram().Write16(VECTOR * 4u, HANDLER);
    m_rig.Host().Ram().Write16(VECTOR * 4u + 2, m_program.loadSegment);
    m_rig.Host().Hook(
      m_program.loadSegment, HANDLER, "Handler",
      [](Machine::Pc& _pc)
      {
        _pc.Processor().Regs().bx = 0x5555;
        _pc.ReturnInterrupt();
      },
      {}, Machine::NativeReturn::Interrupt);
  }

  void Run()
  {
    Assert::IsTrue(m_rig.Host().RunUntil(1'000'000) == Machine::StopReason::Terminated, L"the program ends");
  }

  [[nodiscard]] std::uint16_t Counter()
  {
    return m_rig.Host().Ram().Read16(m_program.loadSegment, COUNTER);
  }

  [[nodiscard]] const Machine::NativeCode::Hook& Books()
  {
    const auto found = m_rig.Host().Native().Hooks().find(Machine::Memory::Linear(m_program.loadSegment, ROUTINE));
    Assert::IsTrue(found != m_rig.Host().Native().Hooks().end(), L"the routine is hooked");
    return found->second;
  }

private:
  PcRig m_rig;
  Machine::LoadedProgram m_program;
};

} // namespace

TEST_CLASS(NativeCodeTests)
{
public:
  // ADR-010: execution that reaches a hooked entry runs the native routine, which leaves as the original did.
  TEST_METHOD(NativeCodeRunsAtAHookedEntry)
  {
    NativeRig rig("NativeRuns");
    rig.Hook(CountUp(100, 5));
    rig.Run();
    Assert::AreEqual(std::uint32_t{COUNTER_START + 100}, std::uint32_t{rig.Counter()});
    Assert::AreEqual(5u, std::uint32_t{rig.Host().Processor().Regs().dx});
    Assert::AreEqual(std::uint64_t{1}, rig.Books().calls);
  }

  // Native code makes an INT: the handler the vector table names runs until its IRET, natively.
  TEST_METHOD(NativeCodeCallsAnInterruptHandler)
  {
    NativeRig rig("NativeInterrupts");
    rig.InstallHandler();
    rig.Hook(
      [](Machine::Pc& _pc)
      {
        _pc.CallInterrupt(VECTOR);
        Machine::Registers& regs = _pc.Processor().Regs();
        regs.ax = regs.bx;
        _pc.ReturnNear();
      });
    rig.Run();
    Assert::AreEqual(0x5555u, std::uint32_t{rig.Host().Processor().Regs().ax});
  }

  // ADR-010 item 8: a native routine that waits stops where a run ends and carries on in the next: the first timer tick,
  // at cycle 262,144, ends its wait, as it ended the original's.
  TEST_METHOD(WaitingRoutineStopsAtTheEndOfARunAndCarriesOn)
  {
    NativeRig rig("NativeWaits");
    rig.Hook(&WaitForTick, {}, Machine::NativeWait::Always);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Reached, L"the run ends inside the wait");
    Assert::AreEqual(std::uint64_t{100'000}, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Books().calls);
    rig.Run();
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
    Assert::IsTrue(rig.Host().Native().Overran().empty());
  }

  // A machine destroyed while a native routine waits unwinds it, and does not hang.
  TEST_METHOD(MachineDestroyedMidWaitUnwindsTheNativeThread)
  {
    NativeRig rig("NativeAbandoned");
    rig.Hook(&WaitForTick, {}, Machine::NativeWait::Always);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Reached);
  }

  // A routine that waits only sometimes runs on the native thread, and a call that does not wait is over when it returns.
  TEST_METHOD(RoutineThatSometimesWaitsRunsWhenItDoesNotWait)
  {
    NativeRig rig("NativeSometimesWork");
    rig.Hook(CountUp(1, 5), {}, Machine::NativeWait::Sometimes);
    rig.Run();
    Assert::AreEqual(std::uint32_t{COUNTER_START + 1}, std::uint32_t{rig.Counter()});
    Assert::AreEqual(std::uint64_t{1}, rig.Books().calls);
  }

  // When it does wait, it stops at the end of a run and carries on in the next, as a routine that always waits does.
  TEST_METHOD(RoutineThatSometimesWaitsStopsAtTheEndOfARun)
  {
    NativeRig rig("NativeSometimesWaits");
    rig.Hook(&WaitForTick, {}, Machine::NativeWait::Sometimes);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Reached, L"the run ends inside the wait");
    Assert::AreEqual(std::uint64_t{100'000}, rig.Host().Clock());
    rig.Run();
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
    Assert::IsTrue(rig.Host().Native().Overran().empty());
  }

  // A routine that waits but is not hooked as one runs where it is called, which cannot stop at the end of a run: the run
  // stops there, and names it, rather than wait for input that only comes between runs.
  TEST_METHOD(RoutineThatWaitsUnmarkedStopsTheRun)
  {
    NativeRig rig("NativeOverruns");
    rig.Hook(&WaitForTick);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Overran);
    Assert::IsTrue(rig.Host().Native().Overran() == "Routine", L"names the routine");
  }

  // A wait ported turn by turn idles on the turns the original's did: the run ends inside it, and the first timer tick
  // ends it on the original's cycle.
  TEST_METHOD(LoopTurnWaitsWhereTheOriginalDoes)
  {
    NativeRig rig("NativeLoopTurn");
    rig.Hook(&WaitForTickByTurns, {}, Machine::NativeWait::Always);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Reached, L"the run ends inside the wait");
    Assert::AreEqual(std::uint64_t{100'000}, rig.Host().Clock());
    rig.Run();
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
  }

  // The same wait de-assembled, with a turn signature in place of the registers, idles on the same turns: the run ends
  // inside it, and the first timer tick ends it on the original's cycle.
  TEST_METHOD(SignatureTurnWaitsWhereTheOriginalDoes)
  {
    NativeRig rig("NativeSignatureTurn");
    rig.Hook(&WaitForTickBySignature, {}, Machine::NativeWait::Always);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Reached, L"the run ends inside the wait");
    Assert::AreEqual(std::uint64_t{100'000}, rig.Host().Clock());
    rig.Run();
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
  }

  // A signature that never repeats never idles, and stops the run as the original's would.
  TEST_METHOD(SignatureTurnThatNeverRepeatsSpins)
  {
    NativeRig rig("NativeSignatureTurnSpins");
    rig.Hook(
      [](Machine::Pc& _pc)
      {
        for (std::uint16_t turn = 0;; ++turn)
        {
          const std::array signature{std::uint16_t{ROUTINE + 0x0B}, turn};
          _pc.LoopTurn(signature);
        }
      },
      {}, Machine::NativeWait::Always);
    rig.Host().SetSpinLimit(1000);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Spinning);
  }

  // A loop whose turns never repeat never idles, and stops the run as the original's would.
  TEST_METHOD(LoopTurnThatNeverRepeatsSpins)
  {
    NativeRig rig("NativeLoopTurnSpins");
    rig.Hook(
      [](Machine::Pc& _pc)
      {
        Machine::Registers& regs = _pc.Processor().Regs();
        for (;;)
        {
          ++regs.ax;
          _pc.LoopTurn();
        }
      },
      {}, Machine::NativeWait::Always);
    rig.Host().SetSpinLimit(1000);
    Assert::IsTrue(rig.Host().RunUntil(1'000'000) == Machine::StopReason::Spinning);
  }

  TEST_METHOD(TwoRoutinesAtOneEntryAreRefused)
  {
    NativeRig rig("NativeTwice");
    rig.Hook(CountUp(1, 5));
    Assert::ExpectException<std::logic_error>([&] { rig.Hook(CountUp(1, 5)); });
  }
};

} // namespace MachineTests
