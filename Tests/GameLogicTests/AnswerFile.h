// Tests/GameLogicTests/AnswerFile.h
#pragma once

#include "Reference.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace GameLogicTests
{

/// The environment variable that makes a run record the subsystems' known answers (CheckSubsystemAnswers, D33) instead of
/// checking them, when it is set and not empty. They are recorded by hand, in one process, never in CI, and a second
/// recording reproduces every file byte for byte.
inline constexpr const char* RECORD_GUARD = "OUTPOST_ELITE_RECORD_GUARD";

/// The value of the environment variable _name, or empty if it is not set.
[[nodiscard]] inline std::string EnvironmentVariable(const char* _name)
{
#if defined(_MSC_VER)
  // MSVC's SDL checks reject std::getenv as unsafe (C4996); _dupenv_s is the replacement they name.
  char* value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, _name) != 0 || value == nullptr)
  {
    return {};
  }
  struct Release
  {
    void operator()(char* _text) const noexcept
    {
      std::free(_text);
    }
  };
  const std::unique_ptr<char, Release> owned(value);
  return std::string(owned.get());
#else
  const char* const value = std::getenv(_name);
  return value == nullptr ? std::string() : std::string(value);
#endif
}

/// Whether this run records the guard instead of checking it: RECORD_GUARD is set and not empty. Where CI is set too, as
/// GitHub Actions sets it, that fails the test.
[[nodiscard]] inline bool RecordingGuard()
{
  using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
  if (EnvironmentVariable(RECORD_GUARD).empty())
  {
    return false;
  }
  Assert::IsTrue(EnvironmentVariable("CI").empty(),
                 L"OUTPOST_ELITE_RECORD_GUARD is set where CI is: the guard is recorded by hand, never in CI");
  return true;
}

/// A text file of answers in the repository: comment lines, which start with '#', and blank lines up to the first answer,
/// which are its header and document its format, then one answer to a line. Blank lines and comments after the first
/// answer are ignored, and a recording does not keep them.
class AnswerFile
{
public:
  /// An answer, and the 1-based line it is on.
  struct Answer
  {
    std::size_t line;
    std::string text;
  };

  /// Every answer in _file, a path from the repository's root. A file that is not found or cannot be read fails the test.
  explicit AnswerFile(std::string_view _file)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    const std::filesystem::path path = Elite::FindInRepository(_file);
    Assert::IsFalse(path.empty(), Widen(std::string(_file) + " in the repository").c_str());
    std::string header;
    const bool read = Read(path, header, m_answers);
    Assert::IsTrue(read, Widen(std::string(_file) + " cannot be read").c_str());
  }

  [[nodiscard]] const std::vector<Answer>& Answers() const noexcept
  {
    return m_answers;
  }

  /// Writes _file again: its header as the file holds it now, then _answers, each ending in a new line ('\n', whatever the
  /// platform). Returns false if it cannot be read or written.
  static bool Write(std::string_view _file, const std::vector<std::string>& _answers) noexcept
  {
    try
    {
      const std::filesystem::path path = Elite::FindInRepository(_file);
      std::string header;
      std::vector<Answer> answers;
      if (path.empty() || !Read(path, header, answers))
      {
        return false;
      }
      std::ofstream file(path, std::ios::binary | std::ios::trunc);
      file << header;
      for (const std::string& answer : _answers)
      {
        file << answer << '\n';
      }
      file.close();
      return !file.fail();
    }
    catch (...)
    {
      return false;
    }
  }

  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

private:
  /// Reads _path: _header gets the lines before the first answer, each ending in a new line, and _answers every answer.
  static bool Read(const std::filesystem::path& _path, std::string& _header, std::vector<Answer>& _answers)
  {
    std::ifstream file(_path, std::ios::binary);
    if (!file)
    {
      return false;
    }
    std::string line;
    for (std::size_t number = 1; std::getline(file, line); ++number)
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      if (line.empty() || line.front() == '#')
      {
        if (_answers.empty())
        {
          _header += line + '\n';
        }
        continue;
      }
      _answers.push_back(Answer{number, line});
    }
    return true;
  }

  std::vector<Answer> m_answers;
};

/// A subsystem's known answers (D33): _answers, what its value routines give for the inputs the test walks, a line each in
/// the order they are listed, are checked against those _file holds, and there must be _count of them, the count its header
/// states. A line that differs fails, naming it and both texts, and so does a count that differs. While the guard is
/// recorded (RecordingGuard), _file is written with _answers instead, its header kept.
inline void CheckSubsystemAnswers(std::string_view _file, const std::vector<std::string>& _answers, std::size_t _count)
{
  using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
  Assert::AreEqual(_count, _answers.size(),
                   AnswerFile::Widen(std::string(_file) + ": the answers the test gives, as its header states").c_str());
  if (RecordingGuard())
  {
    Assert::IsTrue(AnswerFile::Write(_file, _answers), AnswerFile::Widen(std::string(_file) + " cannot be written").c_str());
    return;
  }
  const AnswerFile file(_file);
  for (std::size_t index = 0; index < _answers.size() && index < file.Answers().size(); ++index)
  {
    const AnswerFile::Answer& recorded = file.Answers()[index];
    if (recorded.text != _answers[index])
    {
      Assert::Fail(AnswerFile::Widen(std::format("{} line {}: the answer is '{}', and was recorded as '{}'", _file, recorded.line,
                                                 _answers[index], recorded.text))
                     .c_str());
    }
  }
  Assert::AreEqual(_count, file.Answers().size(), AnswerFile::Widen(std::string(_file) + ": the answers recorded").c_str());
}

} // namespace GameLogicTests
