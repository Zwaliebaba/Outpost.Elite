// GameLogicTests/TwinRig.h
#pragma once

#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDigest.h"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameLogicTests
{

/// How a TwinRig sets up its machine.
struct TwinOptions
{
  Machine::Dos::DateTime startMoment = Elite::START_MOMENT; ///< DOS's clock at power-on
  bool fromPowerOn = false;  ///< the twin starts at power-on, its digests counted from there, rather than at the title screen
  bool mousePresent = false; ///< a mouse driver is loaded, the emulated one the PC services int 33h with
};

/// The twins' known answers, from the repository's root. The file's header documents its format.
inline constexpr std::string_view KNOWN_ANSWERS_FILE = "GameLogicTests/KnownAnswers.txt";

/// The twins' known answers (D20, ADR-008 item 8), as KNOWN_ANSWERS_FILE holds them: every digest a twin gives, at each
/// digest step and at the end of each Play, recorded from the interpreted original before D7 deleted it, under the twin's
/// name and the digest's ordinal within the twin, counted from 1 over all its Play calls. They are what a twin's machine
/// is checked against, and an answer changes only by a ruling recorded with its cause (ADR-016 item 4).
class KnownAnswers
{
public:
  /// Every answer in the file. A file that is not found, a line that is neither blank, a comment nor an answer, and an
  /// answer given twice fail the test.
  KnownAnswers()
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    const std::filesystem::path path = Elite::FindInRepository(KNOWN_ANSWERS_FILE);
    Assert::IsFalse(path.empty(), L"GameLogicTests/KnownAnswers.txt in the repository");
    std::string error;
    const bool read = Read(path, m_answers, error); // before the message, which names what it found
    Assert::IsTrue(read, Widen(std::string(KNOWN_ANSWERS_FILE) + ": " + error).c_str());
  }

  /// Asserts that _digest, which _what names, is _twin's known answer _ordinal. A missing answer fails too, naming the
  /// twin and the ordinal.
  void Check(const std::string& _twin, std::size_t _ordinal, std::string_view _digest, const std::wstring& _what) const
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    const auto found = m_answers.find(std::pair{_twin, _ordinal});
    if (found == m_answers.end())
    {
      Assert::Fail((_what + L" is " + Widen(_digest) + L", and " +
                    Widen(std::format("{} has no known answer {} in {}", _twin, _ordinal, KNOWN_ANSWERS_FILE)))
                     .c_str());
    }
    else if (found->second != _digest)
    {
      Assert::Fail(
        (_what + L" is " + Widen(_digest) + L", and " + Widen(std::format("{}'s known answer {} is {}", _twin, _ordinal, found->second)))
          .c_str());
    }
  }

private:
  /// The answers, by the twin's name and the ordinal.
  using Answers = std::map<std::pair<std::string, std::size_t>, std::string>;

  static constexpr std::size_t DIGEST_CHARACTERS = 64; // a SHA-256 in hex

  /// Reads every answer in _path into _answers. Returns false, with _error naming the line, for a line that is neither
  /// blank, a comment nor an answer, or an answer given twice.
  static bool Read(const std::filesystem::path& _path, Answers& _answers, std::string& _error)
  {
    std::ifstream file(_path);
    if (!file)
    {
      _error = "cannot be read";
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
        continue;
      }
      std::istringstream fields(line);
      std::string twin;
      std::string ordinalText;
      std::string digest;
      std::string extra;
      fields >> twin >> ordinalText >> digest;
      std::size_t ordinal = 0;
      const char* const ordinalEnd = ordinalText.data() + ordinalText.size();
      const std::from_chars_result parsed = std::from_chars(ordinalText.data(), ordinalEnd, ordinal);
      const bool hex = digest.find_first_not_of("0123456789abcdef") == std::string::npos;
      if (twin.empty() || parsed.ec != std::errc() || parsed.ptr != ordinalEnd || ordinal == 0 || digest.size() != DIGEST_CHARACTERS ||
          !hex || (fields >> extra))
      {
        _error = std::format("line {} is not a twin's name, an ordinal from 1 and a SHA-256 in lowercase hex", number);
        return false;
      }
      if (!_answers.emplace(std::pair{twin, ordinal}, digest).second)
      {
        _error = std::format("line {} gives {}'s answer {} again", number, twin, ordinal);
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

  Answers m_answers;
};

/// A twin (ADR-016): the reference booted to its title screen, or started at power-on, on a Dispatcher with the native
/// routines in place (ReferenceRig), and then a scenario no replay reaches, played step by step. A test sets up its state
/// through Both, from the values the game's own code writes, and plays it with Play. Each digest it gives, at every digest
/// step and at the end of every Play, must be the twin's next known answer (KnownAnswers), which the interpreted original
/// gave for the same scenario before D7 deleted it. On a Dispatcher nothing is interpreted, so a twin also shows that its
/// scenario reaches no code that no native routine stands in for.
class TwinRig
{
public:
  explicit TwinRig(std::string_view _name, const TwinOptions& _options = {})
    : m_name(_name),
      m_rig(_name, _options.startMoment, _options.mousePresent),
      m_player(m_rig.Host(), m_rig.Program())
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Assert::IsTrue(m_rig.Loaded(), L"ELITES.EXE at the repository root");
    if (!_options.fromPowerOn)
    {
      Play("wait 3");
    }
  }

  TwinRig(const TwinRig&) = delete;
  TwinRig& operator=(const TwinRig&) = delete;
  TwinRig(TwinRig&&) = delete;
  TwinRig& operator=(TwinRig&&) = delete;
  ~TwinRig() = default;

  /// Calls _change(pc, program) on the machine: how a test sets up a state no replay reaches, through Ram() and the data
  /// segment (Elite::DataSegment). The name is the one the twins' set-ups were written with, when there were two machines.
  template <typename Change> void Both(Change _change)
  {
    _change(m_rig.Host(), m_rig.Program());
  }

  /// Plays _steps, in Replay.h's syntax. Each must reach the end of every step but the last, which must stop the run as
  /// _end says: StopReason::Terminated for a test that leaves the program. Every digest step, and the state the steps end
  /// in, must be the twin's next known answer.
  void Play(std::string_view _steps, Machine::StopReason _end = Machine::StopReason::Reached)
  {
    const std::vector<Elite::Step> steps = Parse(_steps);
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
      const Elite::Step& step = steps[index];
      const std::wstring where = Where(step);
      const std::string digest = PlayStep(step, Stop(index, steps.size(), _end), where);
      if (step.kind == Elite::StepKind::Digest)
      {
        Answer(digest, where);
      }
    }
    Answer(Elite::GameStateDigest(m_rig.Host(), m_rig.Program()), Widen(m_name) + L", the state Play ends in");
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_rig.Host();
  }

  /// Where the machine's DOS keeps its files.
  [[nodiscard]] const std::filesystem::path& Files() const noexcept
  {
    return m_rig.Files();
  }

private:
  [[nodiscard]] static std::vector<Elite::Step> Parse(std::string_view _steps)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    std::vector<Elite::Step> steps;
    std::string error;
    const bool parsed = Elite::ParseSteps(_steps, steps, error); // before the message, which names the step it could not read
    Assert::IsTrue(parsed, Widen(error).c_str());
    return steps;
  }

  /// How step _index of _count must stop the run: the last as _end says, every other at its end.
  [[nodiscard]] static Machine::StopReason Stop(std::size_t _index, std::size_t _count, Machine::StopReason _end) noexcept
  {
    return _index + 1 == _count ? _end : Machine::StopReason::Reached;
  }

  /// _step, for a message: the twin, the line, and a digest step's label.
  [[nodiscard]] std::wstring Where(const Elite::Step& _step) const
  {
    std::wstring where = Widen(m_name) + L" line " + std::to_wstring(_step.line);
    if (_step.kind == Elite::StepKind::Digest)
    {
      where += L" (digest " + Widen(_step.name) + L")";
    }
    return where;
  }

  /// Plays _step, which must stop the run as _expected says, and returns its digest: a digest step's, or empty.
  [[nodiscard]] std::string PlayStep(const Elite::Step& _step, Machine::StopReason _expected, const std::wstring& _where)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    std::string digest;
    const Machine::StopReason stopped = m_player.Play(_step, digest);
    if (stopped == Machine::StopReason::Unported)
    {
      const Machine::Registers& at = m_rig.Host().Processor().Regs();
      Assert::Fail(
        (_where + L": the twin reached code no native routine stands in for, at " + Widen(std::format("{:04X}:{:04X}", at.cs, at.ip)))
          .c_str());
    }
    if (stopped == Machine::StopReason::Overran)
    {
      Assert::Fail(
        (_where + L": native " + Widen(m_rig.Host().Native().Overran()) + L" waited, and is not hooked as a routine that waits").c_str());
    }
    Assert::IsTrue(stopped == _expected, (_where + L": the run stopped otherwise").c_str());
    return digest;
  }

  /// _digest, which _what names, is the twin's next known answer.
  void Answer(std::string_view _digest, const std::wstring& _what)
  {
    m_answers.Check(m_name, ++m_digests, _digest, _what + L": the digest");
  }

  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

  std::string m_name;
  KnownAnswers m_answers;
  std::size_t m_digests = 0; // the twin's digests so far, the last one's ordinal
  ReferenceRig m_rig;
  Elite::ReplayPlayer m_player;
};

} // namespace GameLogicTests
