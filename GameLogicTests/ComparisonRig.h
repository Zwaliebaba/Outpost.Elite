// GameLogicTests/ComparisonRig.h
#pragma once

#include "NativeRoutines.h"
#include "ReferenceRig.h"
#include "Replay.h"

#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace GameLogicTests
{

/// Where compared runs leave their native reports: the corpus's and each constructed test's.
/// Tools/RoutineCoverage.py reads every report there (ADR-010 item 5).
inline std::filesystem::path NativeReportDirectory()
{
  return std::filesystem::temp_directory_path() / "OutpostEliteNativeReports";
}

/// Writes _native's report as NativeReportDirectory()/_name.tsv. One that cannot be written is left
/// out, and RoutineCoverage then reports what only it covered as not covered.
inline void SaveNativeReport(std::string_view _name, const Machine::NativeCode& _native) noexcept
{
  try
  {
    const std::filesystem::path directory = NativeReportDirectory();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::ofstream report(directory / (std::string(_name) + ".tsv"), std::ios::trunc);
    Elite::WriteNativeReport(_native, report);
  }
  catch (...)
  {
    return;
  }
}

/// The registers one constructed call gives a routine. DS and ES are the reference's data segment.
struct Inputs
{
  std::uint16_t ax = 0;
  std::uint16_t bx = 0;
  std::uint16_t cx = 0;
  std::uint16_t dx = 0;
  std::uint16_t si = 0;
  std::uint16_t di = 0;
  std::uint16_t bp = 0;
};

/// The reference booted to its title screen, so that its own interrupt handlers (the divide trap's
/// among them) are in place, with the native routines installed and every call compared (ADR-010):
/// how a test gives a ported routine the inputs the replays do not reach (plan §6.3). Set up memory
/// through Host() first where a routine reads it. What the comparisons ran is saved as _name's native
/// report when the rig is done.
class ComparisonRig
{
public:
  explicit ComparisonRig(std::string_view _name)
    : m_name(_name),
      m_rig(_name)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
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

  ComparisonRig(const ComparisonRig&) = delete;
  ComparisonRig& operator=(const ComparisonRig&) = delete;
  ComparisonRig(ComparisonRig&&) = delete;
  ComparisonRig& operator=(ComparisonRig&&) = delete;

  ~ComparisonRig()
  {
    SaveNativeReport(m_name, m_rig.Host().Native());
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_rig.Host();
  }

  [[nodiscard]] const Machine::LoadedProgram& Program() const noexcept
  {
    return m_rig.Program();
  }

  /// Calls the routine at _entry with _inputs, both ways, and puts the registers back as they were.
  void Call(std::uint16_t _entry, const Inputs& _inputs)
  {
    Machine::Registers& regs = m_rig.Host().Processor().Regs();
    const Machine::Registers saved = regs;
    regs.ax = _inputs.ax;
    regs.bx = _inputs.bx;
    regs.cx = _inputs.cx;
    regs.dx = _inputs.dx;
    regs.si = _inputs.si;
    regs.di = _inputs.di;
    regs.bp = _inputs.bp;
    regs.ds = Elite::DataSegment(m_rig.Program());
    regs.es = regs.ds;
    m_rig.Host().CallNear(_entry);
    regs = saved;
  }

  /// Every call of the routine at _entry was compared, and agreed.
  void AssertAllAgreed(std::uint16_t _entry, std::uint64_t _calls)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
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
  std::string m_name;
  ReferenceRig m_rig;
};

} // namespace GameLogicTests
