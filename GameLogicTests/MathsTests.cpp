#include "pch.h"

#include "NativeRoutines.h"
#include "ReferenceRig.h"
#include "Replay.h"

#include <initializer_list>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The inputs one constructed call gives a routine.
struct Inputs
{
  std::uint16_t ax = 0;
  std::uint16_t bx = 0;
  std::uint16_t cx = 0;
  std::uint16_t dx = 0;
};

// The reference booted to its title screen, so that its own interrupt handlers (the divide trap's
// among them) are in place, with the native routines installed and every call compared (ADR-010).
class ComparisonRig
{
public:
  explicit ComparisonRig(std::string_view _name)
    : m_rig(_name)
  {
    Assert::IsTrue(m_rig.Loaded(), L"ELITES.EXE at the repository root");
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps("wait 3", steps, error), L"parses");
    Elite::ReplayPlayer player(m_rig.Host(), m_rig.Program());
    std::string digest;
    Assert::IsTrue(player.Play(steps.front(), digest) == Machine::StopReason::Reached, L"boots");
    Elite::InstallNativeRoutines(m_rig.Host(), m_rig.Program());
    m_rig.Host().Native().SetVerifying(true);
  }

  // Calls the routine at _entry with _inputs, both ways, and puts the machine back as it was.
  void Call(std::uint16_t _entry, const Inputs& _inputs)
  {
    Machine::Registers& regs = m_rig.Host().Processor().Regs();
    const Machine::Registers saved = regs;
    regs.ax = _inputs.ax;
    regs.bx = _inputs.bx;
    regs.cx = _inputs.cx;
    regs.dx = _inputs.dx;
    regs.ds = Elite::DataSegment(m_rig.Program());
    regs.es = regs.ds;
    m_rig.Host().CallNear(_entry);
    regs = saved;
  }

  // Every call of the routine at _entry was compared, and agreed.
  void AssertAllAgreed(std::uint16_t _entry, std::uint64_t _calls)
  {
    const auto& hooks = m_rig.Host().Native().Hooks();
    const auto found = hooks.find(Machine::Memory::Linear(m_rig.Program().loadSegment, _entry));
    Assert::IsTrue(found != hooks.end(), L"the routine is ported");
    const std::vector<Machine::NativeCode::Mismatch>& mismatches = m_rig.Host().Native().Mismatches();
    if (!mismatches.empty())
    {
      const Machine::NativeCode::Mismatch& first = mismatches.front();
      const std::string text = first.routine + " call " + std::to_string(first.call) + ": " + first.difference;
      Assert::Fail(std::wstring(text.begin(), text.end()).c_str());
    }
    Assert::AreEqual(_calls, found->second.calls);
    Assert::AreEqual(_calls, found->second.verified, L"none unverifiable");
  }

private:
  ReferenceRig m_rig;
};

constexpr std::uint16_t ARC_TANGENT_2 = 0x24A5;
constexpr std::uint16_t RATIO_ARC_TANGENT = 0x24F7;
constexpr std::uint16_t ANGLE_WITHIN_TOLERANCE = 0x2CDB;

} // namespace

// Constructed inputs for the ported arithmetic (plan §6.3): the edges the replays may not reach.
TEST_CLASS(MathsTests)
{
public:
  // 0/0, and every ratio of 2 or more, overflow RatioArcTangent's divide: the game's own trap handler
  // saturates the quotient, and the native routine must leave what it leaves.
  TEST_METHOD(RatioArcTangentAgreesThroughTheDivideTrap)
  {
    ComparisonRig rig("RatioArcTangent");
    const std::initializer_list<Inputs> calls = {{0, 0}, {5, 1},           {0xFFFF, 0x7FFF}, {1, 2},
                                                 {0, 1}, {0x7FFF, 0xFFFF}, {0x1234, 0x1235}, {3, 3}};
    for (const Inputs& inputs : calls)
      rig.Call(RATIO_ARC_TANGENT, inputs);
    rig.AssertAllAgreed(RATIO_ARC_TANGENT, calls.size());
  }

  TEST_METHOD(ArcTangent2AgreesInEveryQuadrantAndAtTheExtremes)
  {
    ComparisonRig rig("ArcTangent2");
    const std::initializer_list<Inputs> calls = {{0, 0},        {0x8000, 0}, {0, 0x8000},      {0xFFFF, 0xFFFF}, {100, 0xFF9C},
                                                 {0xFF9C, 100}, {1, 0x7FFF}, {0x8000, 0x8000}, {0x7FFF, 1},      {0x4000, 0x4000}};
    for (const Inputs& inputs : calls)
      rig.Call(ARC_TANGENT_2, inputs);
    rig.AssertAllAgreed(ARC_TANGENT_2, calls.size());
  }

  // AX and CX are angles, BX the tolerance.
  TEST_METHOD(AngleWithinToleranceAgreesAcrossTheWrap)
  {
    ComparisonRig rig("AngleWithinTolerance");
    const std::initializer_list<Inputs> calls = {{.ax = 0x7FF, .bx = 1, .cx = 0x010},    {.ax = 0x001, .bx = 0x20, .cx = 0x7FF},
                                                 {.ax = 0x400, .bx = 0x64, .cx = 0x3FF}, {.ax = 0x3FF, .bx = 0x64, .cx = 0x400},
                                                 {.ax = 0x123, .bx = 0, .cx = 0x123},    {.ax = 0xFFFF, .bx = 2, .cx = 0},
                                                 {.ax = 0x200, .bx = 0x400, .cx = 0x600}};
    for (const Inputs& inputs : calls)
      rig.Call(ANGLE_WITHIN_TOLERANCE, inputs);
    rig.AssertAllAgreed(ANGLE_WITHIN_TOLERANCE, calls.size());
  }
};

} // namespace GameLogicTests
