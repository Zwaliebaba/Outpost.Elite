// GameLogicTests/TwinRig.h
#pragma once

#include "ComparisonRig.h"
#include "NativeRoutines.h"
#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDigest.h"

#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
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

/// The twins' known answers, from the repository's root. The file's header documents its format.
inline constexpr std::string_view KNOWN_ANSWERS_FILE = "GameLogicTests/KnownAnswers.txt";

/// The environment variable that makes a run record the twins' known answers instead of checking them, when it is set
/// and not empty. Answers are recorded by hand, never in CI.
inline constexpr const char* RECORD_KNOWN_ANSWERS = "OUTPOST_ELITE_RECORD_KNOWN_ANSWERS";

/// The twins' known answers (D20, ADR-008 item 8), as KNOWN_ANSWERS_FILE holds them: every digest a twin compares, at
/// each digest step and at the end of each Play, recorded from the interpreted original, under the twin's name and the
/// digest's ordinal within the twin, counted from 1 over all its Play calls. Once D7 has deleted the interpreter, they
/// are what a twin's native machine is checked against.
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
    std::string header;
    std::string error;
    const bool read = Read(path, header, m_answers, error); // before the message, which names what it found
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
                    Widen(std::format("{} has no known answer {} in {}: record the twins' answers with {}=1", _twin, _ordinal,
                                      KNOWN_ANSWERS_FILE, RECORD_KNOWN_ANSWERS)))
                     .c_str());
    }
    else if (found->second != _digest)
    {
      Assert::Fail(
        (_what + L" is " + Widen(_digest) + L", and " + Widen(std::format("{}'s known answer {} is {}", _twin, _ordinal, found->second)))
          .c_str());
    }
  }

  /// Whether this run records the answers instead of checking them: RECORD_KNOWN_ANSWERS is set and not empty. Where CI
  /// is set too, as GitHub Actions sets it, that fails the test.
  [[nodiscard]] static bool Recording()
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    if (EnvironmentVariable(RECORD_KNOWN_ANSWERS).empty())
    {
      return false;
    }
    Assert::IsTrue(EnvironmentVariable("CI").empty(),
                   L"OUTPOST_ELITE_RECORD_KNOWN_ANSWERS is set where CI is: known answers are recorded by hand, never in CI");
    return true;
  }

  /// Replaces every answer the file holds for _twin with _answers, the first as ordinal 1, and writes the file again: the
  /// lines before its first answer as they were, then every twin's answers in order of name and ordinal. The file is read
  /// afresh, so that what other twins recorded in the same run is kept. Returns false if it cannot be read or written.
  static bool Record(const std::string& _twin, const std::vector<std::string>& _answers) noexcept
  {
    try
    {
      const std::filesystem::path path = Elite::FindInRepository(KNOWN_ANSWERS_FILE);
      std::string header;
      Answers answers;
      std::string error;
      if (path.empty() || !Read(path, header, answers, error))
      {
        return false;
      }
      std::erase_if(answers, [&_twin](const Answers::value_type& _answer) { return _answer.first.first == _twin; });
      for (std::size_t index = 0; index < _answers.size(); ++index)
      {
        answers.emplace(std::pair{_twin, index + 1}, _answers[index]);
      }
      std::ofstream file(path, std::ios::trunc);
      file << header;
      for (const auto& [key, digest] : answers)
      {
        file << key.first << ' ' << key.second << ' ' << digest << '\n';
      }
      file.close();
      return !file.fail();
    }
    catch (...)
    {
      return false;
    }
  }

private:
  /// The answers, by the twin's name and the ordinal.
  using Answers = std::map<std::pair<std::string, std::size_t>, std::string>;

  static constexpr std::size_t DIGEST_CHARACTERS = 64; // a SHA-256 in hex

  /// Reads _path: _header gets the lines before the first answer, each ending in a new line, and _answers every answer.
  /// Returns false, with _error naming the line, for a line that is neither blank, a comment nor an answer, or an answer
  /// given twice.
  static bool Read(const std::filesystem::path& _path, std::string& _header, Answers& _answers, std::string& _error)
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
        if (_answers.empty())
        {
          _header += line + '\n';
        }
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

  /// The value of the environment variable _name, or empty if it is not set.
  [[nodiscard]] static std::string EnvironmentVariable(const char* _name)
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

  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

  Answers m_answers;
};

/// A twin's native machine alone, on a Dispatcher (ADR-011), each digest it gives, at every digest step and at the end of
/// every Play, checked against the twin's known answers: the half of a TwinRig that D7 keeps, and the whole of a twin
/// once D7 has deleted the interpreter. It takes a twin's set-up and steps as TwinRig does, so that D7 deletes TwinRig,
/// this class takes its name, and no twin's steps change. Until then a test exercises that form by running a twin's
/// scenario on it under the twin's name, which makes it meet that twin's answers. _options.compared is not read: on a
/// Dispatcher nothing is interpreted, so nothing is compared. It checks whether or not RECORD_KNOWN_ANSWERS is set, since
/// it has no original to record from.
class NativeTwin
{
public:
  explicit NativeTwin(std::string_view _name, const TwinOptions& _options = {})
    : NativeTwin(_name, std::string(_name) + "-NativeTwin", _options, &Machine::MakeDispatcher)
  {
    if (!_options.fromPowerOn)
    {
      Play("wait 3");
    }
  }

  NativeTwin(const NativeTwin&) = delete;
  NativeTwin& operator=(const NativeTwin&) = delete;
  NativeTwin(NativeTwin&&) = delete;
  NativeTwin& operator=(NativeTwin&&) = delete;
  ~NativeTwin() = default;

  /// Calls _change(pc, program) on the native machine: TwinRig::Both's set-up, on the one machine there is.
  template <typename Change> void Both(Change _change)
  {
    _change(m_rig.Host(), m_rig.Program());
  }

  /// Plays _steps as TwinRig::Play does, on the native machine alone: each must reach the end of every step but the last,
  /// which must stop it as _end says, and every digest step, and the state the steps end in, must be the twin's next
  /// known answer.
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
    Answer(EndDigest(), Widen(m_name) + L", the state Play ends in");
  }

  [[nodiscard]] Machine::Pc& Native() noexcept
  {
    return m_rig.Host();
  }

  /// Where the machine's DOS keeps its files.
  [[nodiscard]] const std::filesystem::path& Files() const noexcept
  {
    return m_rig.Files();
  }

private:
  friend class TwinRig; // the interpreted half, until D7 deletes it

  /// The native machine of the twin _name, its scratch directory named _scratch, the PC as _options say, on the processor
  /// _makeProcessor makes, with the native routines in place. Not started: the caller plays its first steps.
  NativeTwin(std::string_view _name, std::string_view _scratch, const TwinOptions& _options, Machine::ProcessorFactory _makeProcessor)
    : m_name(_name),
      m_rig(_scratch, _options.startMoment, _makeProcessor),
      m_player(m_rig.Host(), m_rig.Program())
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Assert::IsTrue(m_rig.Loaded(), L"ELITES.EXE at the repository root");
    Elite::InstallNativeRoutines(m_rig.Host(), m_rig.Program());
    m_rig.Host().Native().SetPoisoning(true); // what a routine's contract leaves to it, no caller reads (ADR-012)
  }

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
      Assert::Fail((_where + L": the native twin reached code no native routine stands in for, at " +
                    Widen(std::format("{:04X}:{:04X}", at.cs, at.ip)))
                     .c_str());
    }
    if (stopped == Machine::StopReason::Overran)
    {
      Assert::Fail(
        (_where + L": native " + Widen(m_rig.Host().Native().Overran()) + L" waited, and is not hooked as a routine that waits").c_str());
    }
    Assert::IsTrue(stopped == _expected, (_where + L": the native run stopped otherwise").c_str());
    return digest;
  }

  /// The digest of the state the machine is in.
  [[nodiscard]] std::string EndDigest()
  {
    return Elite::GameStateDigest(m_rig.Host(), m_rig.Program());
  }

  /// The ordinal of the twin's next digest, from 1.
  [[nodiscard]] std::size_t NextOrdinal() noexcept
  {
    return ++m_digests;
  }

  /// The check that survives D7: _digest, which _what names, is the twin's next known answer.
  void Answer(std::string_view _digest, const std::wstring& _what)
  {
    m_answers.Check(m_name, NextOrdinal(), _digest, _what + L": the native digest");
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

/// The reference twice, from one state (ADR-010 item 8): one machine interpreted, the other with the
/// native routines in place and, unless the options say not, every call of a work routine compared.
/// Both boot to the title screen, or start at power-on, and then get the same memory and the same
/// steps. A routine that waits cannot be compared call by call, so this is how a test drives one
/// through a state no replay reaches: every digest of the native twin must be the interpreted one's,
/// and so must the state both end in. An uncompared native twin runs the native code of a routine
/// that waits only sometimes, or that calls DOS, where a compared one keeps the original's outcome; and it
/// runs on a Dispatcher (ADR-011), which interprets nothing, so it also shows the run reaches no code that
/// no native routine stands in for.
/// Every digest the two compare is also a known answer (KnownAnswers): the original's must be the one recorded
/// for it under _name and its ordinal, unless RECORD_KNOWN_ANSWERS is set, when it is recorded instead, and a
/// twin that passes writes its answers to the file when it is done.
/// The native twin is a NativeTwin, which holds every check that outlives the interpreter, so this class is the
/// interpreted half: D7 deletes it, and NativeTwin takes its name.
/// When the rig is done, what the interpreted twin ran is saved as _name's offsets, and the native
/// twin's comparisons as _name's native report, for Tools/RoutineCoverage.py.
class TwinRig
{
public:
  explicit TwinRig(std::string_view _name, const TwinOptions& _options = {})
    : m_name(_name),
      m_recording(KnownAnswers::Recording()),
      m_exceptions(std::uncaught_exceptions()),
      m_original(m_name + "-Original", _options.startMoment),
      m_native(m_name, m_name + "-Native", _options, _options.compared ? &Machine::MakeCpu : &Machine::MakeDispatcher),
      m_originalPlayer(m_original.Host(), m_original.Program())
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Assert::IsTrue(m_original.Loaded(), L"ELITES.EXE at the repository root");
    m_native.Native().Native().SetVerifying(_options.compared);
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
    SaveNativeReport(m_name, m_native.Native().Native());
    // A twin that failed, and so is unwinding, records nothing: the answers it had stay.
    if (m_recording && std::uncaught_exceptions() <= m_exceptions)
    {
      KnownAnswers::Record(m_name, m_recorded);
    }
  }

  /// Calls _change(pc, program) on the interpreted machine and then on the native one: how a test sets
  /// up a state no replay reaches, through Ram() and the data segment (Elite::DataSegment).
  template <typename Change> void Both(Change _change)
  {
    _change(m_original.Host(), m_original.Program());
    _change(m_native.m_rig.Host(), m_native.m_rig.Program());
  }

  /// Plays _steps, in Replay.h's syntax, on both machines. Each must reach the end of every step but the
  /// last, which must stop both as _end says: StopReason::Terminated for a test that leaves the program.
  /// Every digest step, and the state they end in, must agree and be the twin's next known answer; and no
  /// compared call may have disagreed.
  void Play(std::string_view _steps, Machine::StopReason _end = Machine::StopReason::Reached)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    const std::vector<Elite::Step> steps = NativeTwin::Parse(_steps);
    for (std::size_t index = 0; index < steps.size(); ++index)
    {
      const Elite::Step& step = steps[index];
      const Machine::StopReason expected = NativeTwin::Stop(index, steps.size(), _end);
      const std::wstring where = m_native.Where(step);
      std::string original;
      Assert::IsTrue(m_originalPlayer.Play(step, original) == expected, (where + L": the interpreted run stopped otherwise").c_str());
      const std::string native = m_native.PlayStep(step, expected, where);
      if (step.kind == Elite::StepKind::Digest)
      {
        Agree(original, native, where);
      }
    }
    Agree(Elite::GameStateDigest(m_original.Host(), m_original.Program()), m_native.EndDigest(),
          NativeTwin::Widen(m_name) + L", the state Play ends in");
    const std::vector<Machine::NativeCode::Mismatch>& mismatches = m_native.Native().Native().Mismatches();
    if (!mismatches.empty())
    {
      const Machine::NativeCode::Mismatch& first = mismatches.front();
      Assert::Fail(NativeTwin::Widen(first.routine + " call " + std::to_string(first.call) + ": " + first.difference).c_str());
    }
  }

  [[nodiscard]] Machine::Pc& Original() noexcept
  {
    return m_original.Host();
  }

  [[nodiscard]] Machine::Pc& Native() noexcept
  {
    return m_native.Native();
  }

  /// Where a twin's DOS keeps its files.
  [[nodiscard]] const std::filesystem::path& Files(bool _native) const noexcept
  {
    return _native ? m_native.Files() : m_original.Files();
  }

private:
  /// A digest the two twins compare, which _what names: the original's is the twin's next known answer, or is
  /// recorded as it, and the native one must be the original's.
  void Agree(const std::string& _original, const std::string& _native, const std::wstring& _what)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    const std::size_t ordinal = m_native.NextOrdinal();
    if (m_recording)
    {
      m_recorded.push_back(_original);
    }
    else
    {
      m_native.m_answers.Check(m_name, ordinal, _original, _what + L": the interpreted original's digest");
    }
    Assert::IsTrue(_original == _native, (_what + L": the digests differ").c_str());
  }

  std::string m_name;
  bool m_recording = false;            // the original's digests are recorded as the answers, not checked against them
  int m_exceptions = 0;                // std::uncaught_exceptions() when the twin was made
  std::vector<std::string> m_recorded; // the original's digests, by ordinal from 1, while recording
  // What the interpreted twin runs once booted. Made before the machine that marks it, so that it
  // outlives it.
  std::vector<std::uint8_t> m_executed;
  ReferenceRig m_original;
  NativeTwin m_native;
  Elite::ReplayPlayer m_originalPlayer;
};

} // namespace GameLogicTests
