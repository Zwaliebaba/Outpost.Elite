// GameLogicTests/AnswerFile.h
#pragma once

#include "Outcome.h"
#include "Reference.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameLogicTests
{

/// The environment variable that makes a run record the guard of D33 instead of checking it, when it is set and not empty:
/// the corpus's picture and outcome lines and the twins' pictures and outcomes, each recorded only from a run that met every
/// state digest, and the subsystems' known answers (CheckSubsystemAnswers). The guard is recorded by hand, in one process,
/// never in CI, and a second recording reproduces every file byte for byte.
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

/// A file of answers by twin and ordinal, in KnownAnswers.txt's form: one to a line, the twin's name, the ordinal of the
/// answer within the twin, counted from 1 over all its Play calls, and the value, separated by single spaces. Lines are
/// sorted by twin and ordinal. What the value is, the file's header documents and _shape checks.
class TwinAnswers
{
public:
  /// What a value is.
  enum class Shape : std::uint8_t
  {
    Sha256,  ///< a SHA-256 in lowercase hex: a state digest or a picture digest
    Outcomes ///< outcomes as Elite::OutcomeText writes them, name=value pairs separated by single spaces
  };

  /// Every answer in _file, a path from the repository's root. A file that is not found, a line that is not an answer of
  /// _shape, and an answer given twice fail the test.
  TwinAnswers(std::string_view _file, Shape _shape)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    std::string error;
    const bool parsed = Parse(AnswerFile(_file).Answers(), _shape, m_answers, error); // before the message, which names the line
    Assert::IsTrue(parsed, AnswerFile::Widen(std::string(_file) + ": " + error).c_str());
  }

  /// _twin's answer _ordinal, or nothing.
  [[nodiscard]] const std::string* Find(const std::string& _twin, std::size_t _ordinal) const
  {
    const auto found = m_answers.find(std::pair{_twin, _ordinal});
    return found == m_answers.end() ? nullptr : &found->second;
  }

  /// Replaces every answer _file holds for _twin with _values, the first as ordinal 1, and writes the file again
  /// (AnswerFile::Write), every twin's answers in order of name and ordinal. The file is read afresh, so that what other
  /// twins recorded in the same run is kept. Returns false if it cannot be read or written.
  static bool Record(std::string_view _file, Shape _shape, const std::string& _twin, const std::vector<std::string>& _values) noexcept
  {
    try
    {
      Answers answers;
      std::string error;
      if (!Parse(AnswerFile(_file).Answers(), _shape, answers, error))
      {
        return false;
      }
      std::erase_if(answers, [&_twin](const Answers::value_type& _answer) { return _answer.first.first == _twin; });
      for (std::size_t index = 0; index < _values.size(); ++index)
      {
        answers.emplace(std::pair{_twin, index + 1}, _values[index]);
      }
      std::vector<std::string> lines;
      for (const auto& [key, value] : answers)
      {
        lines.push_back(std::format("{} {} {}", key.first, key.second, value));
      }
      return AnswerFile::Write(_file, lines);
    }
    catch (...)
    {
      return false;
    }
  }

private:
  /// The answers, by the twin's name and the ordinal.
  using Answers = std::map<std::pair<std::string, std::size_t>, std::string>;

  static constexpr std::size_t SHA256_CHARACTERS = 64; // in hex

  /// Whether _value is a value of _shape.
  [[nodiscard]] static bool IsValue(std::string_view _value, Shape _shape)
  {
    std::string error;
    switch (_shape)
    {
    case Shape::Sha256:
      return _value.size() == SHA256_CHARACTERS && _value.find_first_not_of("0123456789abcdef") == std::string_view::npos;
    case Shape::Outcomes:
      return Elite::CheckOutcomeText(_value, error);
    }
    return false;
  }

  /// What a line of _shape holds, for a message.
  [[nodiscard]] static std::string_view Describe(Shape _shape) noexcept
  {
    switch (_shape)
    {
    case Shape::Sha256:
      return "a SHA-256 in lowercase hex";
    case Shape::Outcomes:
      return "outcomes, name=value pairs of Outcome.h's names";
    }
    return "a value";
  }

  /// Every answer of _lines into _answers. Returns false, with _error naming the line, for a line that is not a twin's name,
  /// an ordinal from 1 and a value of _shape, or an answer given twice.
  static bool Parse(const std::vector<AnswerFile::Answer>& _lines, Shape _shape, Answers& _answers, std::string& _error)
  {
    for (const AnswerFile::Answer& line : _lines)
    {
      const std::string_view text = line.text;
      const std::size_t first = text.find(' ');
      const std::size_t second = first == std::string_view::npos ? first : text.find(' ', first + 1);
      const std::string_view twin = text.substr(0, first);
      const std::string ordinalText(second == std::string_view::npos ? std::string_view{} : text.substr(first + 1, second - first - 1));
      const std::string_view value = second == std::string_view::npos ? std::string_view{} : text.substr(second + 1);
      std::size_t ordinal = 0;
      const char* const ordinalEnd = ordinalText.data() + ordinalText.size();
      const std::from_chars_result parsed = std::from_chars(ordinalText.data(), ordinalEnd, ordinal);
      if (twin.empty() || parsed.ec != std::errc() || parsed.ptr != ordinalEnd || ordinal == 0 || !IsValue(value, _shape))
      {
        _error = std::format("line {} is not a twin's name, an ordinal from 1 and {}", line.line, Describe(_shape));
        return false;
      }
      if (!_answers.emplace(std::pair{std::string(twin), ordinal}, std::string(value)).second)
      {
        _error = std::format("line {} gives {}'s answer {} again", line.line, twin, ordinal);
        return false;
      }
    }
    return true;
  }

  Answers m_answers;
};

} // namespace GameLogicTests
