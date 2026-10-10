// GameLogicTests/TwinRig.h
#pragma once

#include "ComparisonRig.h"
#include "NativeRoutines.h"
#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDigest.h"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace GameLogicTests
{

/// How a TwinRig runs its two machines.
struct TwinOptions
{
  bool compared = true; ///< the native twin's calls of work routines are compared (ADR-010 item 4); if not, it runs on a Dispatcher
  Machine::Dos::DateTime startMoment = Elite::START_MOMENT; ///< DOS's clock at power-on
  bool fromPowerOn = false; ///< the twins start at power-on, recording from there, rather than at the title screen
};

/// The reference twice, from one state (ADR-010 item 8): one machine interpreted, the other with the
/// native routines in place and, unless the options say not, every call of a work routine compared.
/// Both boot to the title screen, or start at power-on, and then get the same memory and the same
/// steps. A routine that waits cannot be compared call by call, so this is how a test drives one
/// through a state no replay reaches: every digest of the native twin must be the interpreted one's,
/// and so must the state both end in. An uncompared native twin runs the native code of a routine
/// that waits only sometimes, or that calls DOS, where a compared one keeps the original's outcome; and it
/// runs on a Dispatcher (ADR-011), which interprets nothing, so it also shows the run reaches no code that
/// no native routine stands in for.
/// When the rig is done, what the interpreted twin ran is saved as _name's offsets, and the native
/// twin's comparisons as _name's native report, for Tools/RoutineCoverage.py.
class TwinRig
{
public:
  explicit TwinRig(std::string_view _name, const TwinOptions& _options = {})
    : m_name(_name),
      m_original(m_name + "-Original", _options.startMoment),
      m_native(m_name + "-Native", _options.startMoment, _options.compared ? &Machine::MakeCpu : &Machine::MakeDispatcher),
      m_originalPlayer(m_original.Host(), m_original.Program()),
      m_nativePlayer(m_native.Host(), m_native.Program())
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Assert::IsTrue(m_original.Loaded() && m_native.Loaded(), L"ELITES.EXE at the repository root");
    Elite::InstallNativeRoutines(m_native.Host(), m_native.Program());
    m_native.Host().Native().SetVerifying(_options.compared);
    if (!_options.fromPowerOn)
    {
      Play("wait 3");
    }
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

  /// Plays _steps, in Replay.h's syntax, on both machines. Each must reach the end of every step but the
  /// last, which must stop both as _end says: StopReason::Terminated for a test that leaves the program.
  /// Every digest step, and the state they end in, must agree; and no compared call may have disagreed.
  void Play(std::string_view _steps, Machine::StopReason _end = Machine::StopReason::Reached)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    std::vector<Elite::Step> steps;
    std::string error;
    Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), Widen(error).c_str());
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
      const Elite::Step& step = steps[index];
      const Machine::StopReason expected = index + 1 == steps.size() ? _end : Machine::StopReason::Reached;
      std::string original;
      std::string native;
      const std::wstring where = Widen(m_name) + L" line " + std::to_wstring(step.line);
      Assert::IsTrue(m_originalPlayer.Play(step, original) == expected, (where + L": the interpreted run stopped otherwise").c_str());
      const Machine::StopReason stopped = m_nativePlayer.Play(step, native);
      if (stopped == Machine::StopReason::Unported)
      {
        const Machine::Registers& at = m_native.Host().Processor().Regs();
        Assert::Fail((where + L": the native twin reached code no native routine stands in for, at " +
                      Widen(std::format("{:04X}:{:04X}", at.cs, at.ip)))
                       .c_str());
      }
      if (stopped == Machine::StopReason::Overran)
      {
        Assert::Fail(
          (where + L": native " + Widen(m_native.Host().Native().Overran()) + L" waited, and is not hooked as a routine that waits")
            .c_str());
      }
      Assert::IsTrue(stopped == expected, (where + L": the native run stopped otherwise").c_str());
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

  /// Where a twin's DOS keeps its files.
  [[nodiscard]] const std::filesystem::path& Files(bool _native) const noexcept
  {
    return _native ? m_native.Files() : m_original.Files();
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
