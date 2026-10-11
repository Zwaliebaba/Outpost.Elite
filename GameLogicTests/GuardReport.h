// GameLogicTests/GuardReport.h
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace GameLogicTests
{

/// What the corpus or a twin found of the guard D33 records beside its state digests: the pictures (PictureDigest), which
/// report rather than gate. A picture that differs from the one recorded fails nothing: it is written to the test's log at
/// once, naming the point and both values, and counted in the summary, which the corpus and every twin write when done.
class GuardReport
{
public:
  /// _scope names the run in the log: "corpus", or a twin's name.
  explicit GuardReport(std::wstring _scope)
    : m_scope(std::move(_scope))
  {
  }

  /// The picture _actual at _where against _recorded, the one recorded there.
  void Picture(const std::wstring& _where, std::string_view _recorded, std::string_view _actual)
  {
    ++m_picturesChecked;
    if (_actual != _recorded)
    {
      ++m_picturesDiffer;
      Log(m_scope + L": the picture at " + _where + L" differs: it is " + Widen(_actual) + L", and was recorded as " + Widen(_recorded));
    }
  }

  [[nodiscard]] std::size_t PicturesChecked() const noexcept
  {
    return m_picturesChecked;
  }

  [[nodiscard]] std::size_t PicturesDiffer() const noexcept
  {
    return m_picturesDiffer;
  }

  /// "<scope> pictures: N checked, M differ".
  [[nodiscard]] std::wstring Summary() const
  {
    return m_scope + L" pictures: " + std::to_wstring(m_picturesChecked) + L" checked, " + std::to_wstring(m_picturesDiffer) + L" differ";
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
  std::size_t m_picturesChecked = 0;
  std::size_t m_picturesDiffer = 0;
};

} // namespace GameLogicTests
