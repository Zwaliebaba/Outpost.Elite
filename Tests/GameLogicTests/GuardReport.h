// Tests/GameLogicTests/GuardReport.h
#pragma once

#include "Outcome.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace GameLogicTests
{

/// What the corpus or a twin found of the guard D33 records beside its state digests: the outcomes (Outcome.h). A timing
/// outcome that differs from the one recorded fails nothing: it is written to the test's log at once, naming the point and both values, and counted in the summary, which the corpus and
/// every twin write when done. An outcome of the player's progress that differs, or one not recorded, fails the test.
class GuardReport
{
public:
  /// _scope names the run in the log: "corpus", or a twin's name.
  explicit GuardReport(std::wstring _scope)
    : m_scope(std::move(_scope))
  {
  }

  /// The outcomes _actual at _where against _recorded, the ones recorded there (Elite::CompareOutcomes), each difference
  /// written to the log. Returns why the test fails, naming the outcomes of the player's progress that differ or were not
  /// recorded, or nothing when none does.
  [[nodiscard]] std::wstring Outcomes(const std::wstring& _where, std::string_view _recorded, std::string_view _actual)
  {
    ++m_outcomesChecked;
    std::wstring failure;
    for (const Elite::OutcomeDifference& difference : Elite::CompareOutcomes(_recorded, _actual))
    {
      const bool gates = difference.kind == Elite::OutcomeKind::Progress || !difference.recorded;
      (gates ? m_progressDiffer : m_timingDiffer) += 1;
      std::wstring what = Widen(difference.name);
      what += L" is ";
      what += Widen(difference.actual);
      what += difference.recorded ? L", and was recorded as " + Widen(*difference.recorded) : L", and was not recorded";
      std::wstring message = m_scope;
      message += L": at ";
      message += _where;
      message += gates ? L", the progress outcome differs: " : L", the timing outcome differs: ";
      message += what;
      Log(message);
      if (gates)
      {
        failure += failure.empty() ? L"" : L"; ";
        failure += what;
      }
    }
    return failure.empty() ? failure : _where + L": outcomes of the player's progress differ: " + failure;
  }

  /// "<scope> outcomes: N checked, P differ in progress, T differ in timing".
  [[nodiscard]] std::wstring Summary() const
  {
    return m_scope + L" outcomes: " + std::to_wstring(m_outcomesChecked) + L" checked, " + std::to_wstring(m_progressDiffer) +
           L" differ in progress, " + std::to_wstring(m_timingDiffer) + L" differ in timing";
  }

  /// Writes the summary to the test's log; anything that goes wrong in doing so is dropped, so that a destructor may call it.
  void LogSummary() const noexcept
  {
    try
    {
      Log(Summary());
    }
    catch (...)
    {
      return;
    }
  }

  /// Writes _message to the test's log.
  static void Log(const std::wstring& _message)
  {
    Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(_message.c_str());
  }

private:
  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

  std::wstring m_scope;
  std::size_t m_outcomesChecked = 0;
  std::size_t m_progressDiffer = 0; // outcomes of the player's progress that differ, or were not recorded
  std::size_t m_timingDiffer = 0;
};

} // namespace GameLogicTests
