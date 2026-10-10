// GameLogicTests/TwinRig.h
#pragma once

#include "ComparisonRig.h"
#include "NativeRoutines.h"
#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDigest.h"

#include <string>
#include <string_view>
#include <vector>

namespace GameLogicTests
{

/// The reference twice, from one state (ADR-010 item 8): one machine interpreted, the other with the
/// native routines in place and every call of a work routine compared. Both boot to the title screen,
/// and then get the same memory and the same steps. A routine that waits cannot be compared call by
/// call, so this is how a test drives one through a state no replay reaches: every digest of the native
/// twin must be the interpreted one's, and so must the state both end in. When the rig is done, what
/// the interpreted twin ran is saved as _name's offsets, and the native twin's comparisons as _name's
/// native report, for Tools/RoutineCoverage.py.
class TwinRig
{
public:
  explicit TwinRig(std::string_view _name)
    : m_name(_name),
      m_original(m_name + "-Original"),
      m_native(m_name + "-Native"),
      m_originalPlayer(m_original.Host(), m_original.Program()),
      m_nativePlayer(m_native.Host(), m_native.Program())
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Assert::IsTrue(m_original.Loaded() && m_native.Loaded(), L"ELITES.EXE at the repository root");
    Elite::InstallNativeRoutines(m_native.Host(), m_native.Program());
    m_native.Host().Native().SetVerifying(true);
    Play("wait 3");
    m_original.Host().Processor().SetExecutionMap(&m_executed);
  }

  TwinRig(const TwinRig&) = delete;
  TwinRig& operator=(const TwinRig&) = delete;
  TwinRig(TwinRig&&) = delete;
  TwinRig& operator=(TwinRig&&) = delete;

  ~TwinRig()
  {
    SaveExecutedOffsets(m_name, m_executed, m_original.Program());
    SaveNativeReport(m_name, m_native.Host().Native());
  }

  /// Calls _change(pc, program) on the interpreted machine and then on the native one: how a test sets
  /// up a state no replay reaches, through Ram() and the data segment (Elite::DataSegment).
  template <typename Change> void Both(Change _change)
  {
    _change(m_original.Host(), m_original.Program());
    _change(m_native.Host(), m_native.Program());
  }

  /// Plays _steps, in Replay.h's syntax, on both machines. Each must reach the end of every step; every
  /// digest step, and the state they end in, must agree; and no compared call may have disagreed.
  void Play(std::string_view _steps)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), Widen(error).c_str());
    for (const Elite::Step& step : steps)
    {
      std::string original;
      std::string native;
      const std::wstring where = Widen(m_name) + L" line " + std::to_wstring(step.line);
      Assert::IsTrue(m_originalPlayer.Play(step, original) == Machine::StopReason::Reached,
                     (where + L": the interpreted run stopped").c_str());
      const Machine::StopReason stopped = m_nativePlayer.Play(step, native);
      if (stopped == Machine::StopReason::Overran)
      {
        Assert::Fail(
          (where + L": native " + Widen(m_native.Host().Native().Overran()) + L" waited, and is not hooked as a routine that waits")
            .c_str());
      }
      Assert::IsTrue(stopped == Machine::StopReason::Reached, (where + L": the native run stopped").c_str());
      Assert::IsTrue(original == native, (where + L": the digests differ").c_str());
    }
    Assert::IsTrue(Elite::GameStateDigest(m_original.Host(), m_original.Program()) ==
                     Elite::GameStateDigest(m_native.Host(), m_native.Program()),
                   (Widen(m_name) + L": the two end in different states").c_str());
    const std::vector<Machine::NativeCode::Mismatch>& mismatches = m_native.Host().Native().Mismatches();
    if (!mismatches.empty())
    {
      const Machine::NativeCode::Mismatch& first = mismatches.front();
      Assert::Fail(Widen(first.routine + " call " + std::to_string(first.call) + ": " + first.difference).c_str());
    }
  }

  [[nodiscard]] Machine::Pc& Original() noexcept
  {
    return m_original.Host();
  }

  [[nodiscard]] Machine::Pc& Native() noexcept
  {
    return m_native.Host();
  }

private:
  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

  std::string m_name;
  // What the interpreted twin runs once booted. Made before the machine that marks it, so that it
  // outlives it.
  std::vector<std::uint8_t> m_executed;
  ReferenceRig m_original;
  ReferenceRig m_native;
  Elite::ReplayPlayer m_originalPlayer;
  Elite::ReplayPlayer m_nativePlayer;
};

} // namespace GameLogicTests
