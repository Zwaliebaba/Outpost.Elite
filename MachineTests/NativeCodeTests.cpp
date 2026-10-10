#include "pch.h"

#include "NativeCode.h"
#include "Pc.h"
#include "PcRig.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t ROUTINE = 0x0010;
constexpr std::uint16_t HELPER = 0x0030;
constexpr std::uint16_t HANDLER = 0x0038;
constexpr std::uint16_t COUNTER = 0x0040;
constexpr std::uint8_t VECTOR = 0x60;
constexpr std::uint16_t COUNTER_START = 0x1234;
constexpr std::uint16_t SPEAKER_PORT = 0x61;

// A program that sets DS to its own segment, calls the routine at 0010h and ends: the routine is what
// _routine holds, the helper at 0030h sets BX to 7777h, the interrupt handler at 0038h sets BX to 5555h,
// and the counter word at 0040h starts at 1234h.
std::vector<std::uint8_t> ProgramAround(std::initializer_list<std::uint8_t> _routine)
{
  std::vector<std::uint8_t> code = {0x0E,             // push cs
                                    0x1F,             // pop ds
                                    0xE8, 0x0B, 0x00, // call 0010h
                                    0xCD, 0x20};      // int 20h
  code.resize(ROUTINE, 0x90);
  code.insert(code.end(), _routine);
  code.resize(HELPER, 0x90);
  code.insert(code.end(), {0xBB, 0x77, 0x77, 0xC3}); // mov bx,7777h; ret
  code.resize(HANDLER, 0x90);
  code.insert(code.end(), {0xBB, 0x55, 0x55, 0xCF}); // mov bx,5555h; iret
  code.resize(COUNTER, 0x90);
  code.insert(code.end(), {COUNTER_START & 0xFF, COUNTER_START >> 8});
  std::vector<std::uint8_t> file = TinyExe({});
  file.insert(file.end(), code.begin(), code.end());
  const std::size_t fileBytes = file.size();
  file[2] = static_cast<std::uint8_t>(fileBytes % 512);
  file[4] = static_cast<std::uint8_t>((fileBytes + 511) / 512);
  return file;
}

// The original routine most tests compare with: the counter goes up by one, AX holds it, DX is 5.
const std::initializer_list<std::uint8_t> COUNT_UP = {0xA1, 0x40, 0x00, // mov ax,[0040h]
                                                      0x40,             // inc ax
                                                      0xA3, 0x40, 0x00, // mov [0040h],ax
                                                      0xBA, 0x05, 0x00, // mov dx,5
                                                      0xC3};            // ret

// An original routine that waits for the BIOS tick count to change, with interrupts on.
const std::initializer_list<std::uint8_t> WAIT_FOR_TICK = {0xFB,                         // sti
                                                           0xB8, 0x40, 0x00,             // mov ax,40h
                                                           0x8E, 0xC0,                   // mov es,ax
                                                           0x26, 0x8B, 0x1E, 0x6C, 0x00, // mov bx,es:[6Ch]
                                                           0x26, 0x3B, 0x1E, 0x6C, 0x00, // wait: cmp bx,es:[6Ch]
                                                           0x74, 0xF9,                   // je wait
                                                           0xC3};                        // ret

// Its native counterpart, which waits through Pc::Wait.
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

// A native counterpart of COUNT_UP that adds _step and leaves _dx in DX.
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

class NativeRig
{
public:
  NativeRig(std::string_view _name, std::initializer_list<std::uint8_t> _routine)
    : m_rig(_name)
  {
    m_program = m_rig.Load(ProgramAround(_routine));
    m_rig.Host().SetTimeMode(Machine::TimeMode::Paced);
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

  // Points the vector table's entry for VECTOR at the handler.
  void InstallHandler()
  {
    m_rig.Host().Ram().Write16(VECTOR * 4u, HANDLER);
    m_rig.Host().Ram().Write16(VECTOR * 4u + 2, m_program.loadSegment);
  }

  [[nodiscard]] std::uint16_t CodeSegment() const noexcept
  {
    return m_program.loadSegment;
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
  // ADR-010: execution that reaches a hooked entry runs the native routine, and the original's code
  // there never runs.
  TEST_METHOD(NativeCodeRunsInsteadOfTheOriginal)
  {
    NativeRig rig("NativeRuns", COUNT_UP);
    rig.Hook(CountUp(100, 5));
    rig.Run();
    Assert::AreEqual(std::uint32_t{COUNTER_START + 100}, std::uint32_t{rig.Counter()});
    Assert::AreEqual(std::uint64_t{1}, rig.Books().calls);
  }

  // Native code calls the original's code, and gets control back when it returns.
  TEST_METHOD(NativeCodeCallsTheOriginal)
  {
    NativeRig rig("NativeCalls", COUNT_UP);
    rig.Hook(
      [](Machine::Pc& _pc)
      {
        _pc.CallNear(HELPER);
        Machine::Registers& regs = _pc.Processor().Regs();
        regs.ax = static_cast<std::uint16_t>(regs.bx + 1);
        _pc.ReturnNear();
      });
    rig.Run();
    Assert::AreEqual(0x7778u, std::uint32_t{rig.Host().Processor().Regs().ax});
    Assert::AreEqual(std::uint32_t{COUNTER_START}, std::uint32_t{rig.Counter()}, L"the original routine did not run");
  }

  // Compared, a routine that agrees with the original runs once in effect: the original's run is
  // undone before the native one.
  TEST_METHOD(AgreeingRoutineIsVerified)
  {
    NativeRig rig("NativeAgrees", COUNT_UP);
    rig.Hook(CountUp(1, 5));
    rig.Host().Native().SetVerifying(true);
    rig.Run();
    Assert::AreEqual(std::uint32_t{COUNTER_START + 1}, std::uint32_t{rig.Counter()});
    Assert::AreEqual(std::uint64_t{1}, rig.Books().verified);
    Assert::IsTrue(rig.Host().Native().Mismatches().empty());
    Assert::IsTrue(rig.Books().executed.Contains(ROUTINE), L"the original's instructions are covered");
  }

  // A routine that disagrees is reported, and the run carries on from the original's outcome.
  TEST_METHOD(DisagreeingRoutineIsReportedAndTheOriginalStands)
  {
    NativeRig rig("NativeDisagrees", COUNT_UP);
    rig.Hook(CountUp(2, 5));
    rig.Host().Native().SetVerifying(true);
    rig.Run();
    Assert::AreEqual(std::uint32_t{COUNTER_START + 1}, std::uint32_t{rig.Counter()}, L"the original's outcome");
    Assert::AreEqual(std::uint64_t{1}, rig.Books().mismatches);
    Assert::AreEqual(std::size_t{1}, rig.Host().Native().Mismatches().size());
    const std::string& difference = rig.Host().Native().Mismatches().front().difference;
    Assert::IsTrue(difference.find("AX 1236, original 1235") != std::string::npos, L"names the register");
    Assert::IsTrue(difference.find("byte") != std::string::npos, L"names the memory");
  }

  // A register the contract says the routine clobbers is not compared; any other is. Either way the run
  // carries on from the original's registers.
  TEST_METHOD(OnlyClobberedRegistersMayDiffer)
  {
    NativeRig clobbered("NativeClobbers", COUNT_UP);
    clobbered.Hook(CountUp(1, 6), Machine::NativeContract{Machine::REGISTER_DX, 0});
    clobbered.Host().Native().SetVerifying(true);
    clobbered.Run();
    Assert::AreEqual(std::uint64_t{1}, clobbered.Books().verified);
    Assert::AreEqual(5u, std::uint32_t{clobbered.Host().Processor().Regs().dx}, L"the original's DX");

    NativeRig kept("NativeKeeps", COUNT_UP);
    kept.Hook(CountUp(1, 6));
    kept.Host().Native().SetVerifying(true);
    kept.Run();
    Assert::AreEqual(std::uint64_t{1}, kept.Books().mismatches);
  }

  // The native run sees the port reads the original made, and its writes are compared rather than
  // made a second time.
  TEST_METHOD(PortAccessesAreReplayed)
  {
    const std::initializer_list<std::uint8_t> gateOn = {0xE4, 0x61, // in al,61h
                                                        0x0C, 0x01, // or al,1
                                                        0xE6, 0x61, // out 61h,al
                                                        0xC3};      // ret
    const auto gate = [](std::uint8_t _bits)
    {
      return [_bits](Machine::Pc& _pc)
      {
        const auto value = static_cast<std::uint8_t>(_pc.Ports().In8(SPEAKER_PORT) | _bits);
        _pc.Ports().Out8(SPEAKER_PORT, value);
        Machine::Registers& regs = _pc.Processor().Regs();
        regs.ax = static_cast<std::uint16_t>((regs.ax & 0xFF00) | value);
        _pc.ReturnNear();
      };
    };
    NativeRig agrees("NativePortsAgree", gateOn);
    agrees.Hook(gate(0x01));
    agrees.Host().Native().SetVerifying(true);
    const std::uint64_t writes = agrees.Host().Ports().WriteCount();
    agrees.Run();
    Assert::AreEqual(std::uint64_t{1}, agrees.Books().verified);
    Assert::AreEqual(1u, std::uint32_t{agrees.Host().Ports().In8(SPEAKER_PORT) & 1u}, L"the gate is on");
    Assert::IsTrue(agrees.Host().Ports().WriteCount() - writes >= 2, L"both runs' writes count for paced time");

    NativeRig differs("NativePortsDiffer", gateOn);
    differs.Hook(gate(0x02));
    differs.Host().Native().SetVerifying(true);
    differs.Run();
    Assert::AreEqual(std::uint64_t{1}, differs.Books().mismatches);
    Assert::IsTrue(differs.Host().Native().Mismatches().front().difference.find("ports") != std::string::npos);
  }

  // A routine whose original calls the BIOS cannot be undone, so it is not compared, and its outcome
  // stands.
  TEST_METHOD(ServiceCallMakesACallUnverifiable)
  {
    NativeRig rig("NativeUnverifiable", {0xB4, 0x00, 0xCD, 0x1A, 0xC3}); // mov ah,0; int 1Ah; ret
    rig.Hook([](Machine::Pc& _pc) { _pc.ReturnNear(); });
    rig.Host().Native().SetVerifying(true);
    rig.Run();
    Assert::AreEqual(std::uint64_t{1}, rig.Books().unverifiable);
    Assert::AreEqual(std::uint64_t{0}, rig.Books().verified);
  }

  // Native code makes an INT: the handler the vector table names runs until its IRET.
  TEST_METHOD(NativeCodeCallsAnInterruptHandler)
  {
    NativeRig rig("NativeInterrupts", COUNT_UP);
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

  // An interrupt handler's entry can be hooked: the native routine leaves with IRET, and a comparison
  // runs the original to its IRET.
  TEST_METHOD(InterruptHandlerEntryIsHookedAndCompared)
  {
    NativeRig rig("NativeHandler", {0xCD, VECTOR, 0xC3}); // int 60h; ret
    rig.InstallHandler();
    rig.Host().Hook(
      rig.CodeSegment(), HANDLER, "Handler",
      [](Machine::Pc& _pc)
      {
        _pc.Processor().Regs().bx = 0x5555;
        _pc.ReturnInterrupt();
      },
      {}, Machine::NativeReturn::Interrupt);
    rig.Host().Native().SetVerifying(true);
    rig.Run();
    const auto& hooks = rig.Host().Native().Hooks();
    const auto found = hooks.find(Machine::Memory::Linear(rig.CodeSegment(), HANDLER));
    Assert::IsTrue(found != hooks.end());
    Assert::AreEqual(std::uint64_t{1}, found->second.verified);
    Assert::AreEqual(0x5555u, std::uint32_t{rig.Host().Processor().Regs().bx});
  }

  // ADR-010 item 8: a native routine that waits stops where a run ends and carries on in the next, on
  // the same cycles as the original: the first timer tick, at cycle 262,144, ends its wait.
  TEST_METHOD(WaitingRoutineStopsAtTheEndOfARunAndCarriesOn)
  {
    NativeRig original("NativeWaitOriginal", WAIT_FOR_TICK);
    original.Run();
    Assert::AreEqual(std::uint64_t{262'144}, original.Host().Clock());

    NativeRig rig("NativeWaits", WAIT_FOR_TICK);
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
    NativeRig rig("NativeAbandoned", WAIT_FOR_TICK);
    rig.Hook(&WaitForTick, {}, Machine::NativeWait::Always);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Reached);
  }

  // With comparison on, a routine that waits as a rule still runs natively and is not compared: it
  // could not be undone, and its original might never return.
  TEST_METHOD(RoutineThatAlwaysWaitsIsNotCompared)
  {
    NativeRig rig("NativeWaitCompared", WAIT_FOR_TICK);
    rig.Hook(&WaitForTick, {}, Machine::NativeWait::Always);
    rig.Host().Native().SetVerifying(true);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Reached);
    rig.Run();
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Books().calls);
    Assert::AreEqual(std::uint64_t{0}, rig.Books().verified + rig.Books().unverifiable);
  }

  // A routine that waits only sometimes runs on the native thread, and a call whose original returns
  // without waiting is compared like any other.
  TEST_METHOD(RoutineThatSometimesWaitsIsComparedWhenItsOriginalDoesNot)
  {
    NativeRig rig("NativeSometimesAgrees", COUNT_UP);
    rig.Hook(CountUp(1, 5), {}, Machine::NativeWait::Sometimes);
    rig.Host().Native().SetVerifying(true);
    rig.Run();
    Assert::AreEqual(std::uint64_t{1}, rig.Books().verified);
    Assert::AreEqual(std::uint32_t{COUNTER_START + 1}, std::uint32_t{rig.Counter()});
  }

  // When its original does wait, the comparison's original run stops at the end of a run and carries
  // on in the next, as native code that waits does; the call is then unverifiable, and stands.
  TEST_METHOD(ComparedOriginalThatWaitsStopsAtTheEndOfARun)
  {
    NativeRig rig("NativeSometimesWaits", WAIT_FOR_TICK);
    rig.Hook(&WaitForTick, {}, Machine::NativeWait::Sometimes);
    rig.Host().Native().SetVerifying(true);
    Assert::IsTrue(rig.Host().RunUntil(100'000) == Machine::StopReason::Reached, L"the run ends inside the original's wait");
    Assert::AreEqual(std::uint64_t{100'000}, rig.Host().Clock());
    rig.Run();
    Assert::AreEqual(std::uint64_t{262'144}, rig.Host().Clock());
    Assert::AreEqual(std::uint64_t{1}, rig.Books().unverifiable);
    Assert::IsTrue(rig.Host().Native().Overran().empty());
  }

  // A routine that drops its own return address returns past its caller, and its comparison ends
  // there: the routine at 0010h calls one at 0017h that pops the return address 0013h and returns to
  // the program, so BX is never set.
  TEST_METHOD(ComparisonEndsWhenTheOriginalReturnsPastItsCaller)
  {
    constexpr std::uint16_t DROPS_RETURN = 0x0017;
    NativeRig rig("NativeReturnsPast", {0xE8, 0x04, 0x00, // call 0017h
                                        0xBB, 0x01, 0x00, // mov bx,1
                                        0xC3,             // ret
                                        0x58,             // pop ax
                                        0xC3});           // ret
    rig.Host().Hook(
      rig.CodeSegment(), DROPS_RETURN, "DropsReturn",
      [](Machine::Pc& _pc)
      {
        Machine::Registers& regs = _pc.Processor().Regs();
        regs.ax = _pc.Ram().Read16(regs.ss, regs.sp);
        regs.sp = static_cast<std::uint16_t>(regs.sp + 2);
        _pc.ReturnNear();
      },
      Machine::NativeContract{Machine::REGISTER_AX, 0});
    rig.Host().Processor().Regs().bx = 0;
    rig.Host().Native().SetVerifying(true);
    rig.Run();
    const auto found = rig.Host().Native().Hooks().find(Machine::Memory::Linear(rig.CodeSegment(), DROPS_RETURN));
    Assert::IsTrue(found != rig.Host().Native().Hooks().end());
    Assert::AreEqual(std::uint64_t{1}, found->second.verified);
    Assert::AreEqual(0u, std::uint32_t{rig.Host().Processor().Regs().bx}, L"the caller's MOV never ran");
  }

  // A routine that waits but is not hooked as one runs where it is called, which cannot stop at the end
  // of a run: the run stops there, and names it, rather than wait for input that only comes between
  // runs. Compared, its original's wait does the same.
  TEST_METHOD(RoutineThatWaitsUnmarkedStopsTheRun)
  {
    NativeRig native("NativeOverruns", WAIT_FOR_TICK);
    native.Hook(&WaitForTick);
    Assert::IsTrue(native.Host().RunUntil(100'000) == Machine::StopReason::Overran);
    Assert::IsTrue(native.Host().Native().Overran() == "Routine", L"names the routine");

    NativeRig compared("NativeOverrunsCompared", WAIT_FOR_TICK);
    compared.Hook(&WaitForTick);
    compared.Host().Native().SetVerifying(true);
    Assert::IsTrue(compared.Host().RunUntil(100'000) == Machine::StopReason::Overran);
    Assert::IsTrue(compared.Host().Native().Overran() == "Routine", L"names the routine");
  }

  TEST_METHOD(TwoRoutinesAtOneEntryAreRefused)
  {
    NativeRig rig("NativeTwice", COUNT_UP);
    rig.Hook(CountUp(1, 5));
    Assert::ExpectException<std::logic_error>([&] { rig.Hook(CountUp(1, 5)); });
  }
};

} // namespace MachineTests
