// GameLogicTests/TwinRig.h
#pragma once

#include "AnswerFile.h"
#include "GuardReport.h"
#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDigest.h"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
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

/// The twins' pictures (D33), from the repository's root: beside each known answer, the picture digest of the same state,
/// under the same twin and ordinal. The file's header documents its format.
inline constexpr std::string_view KNOWN_PICTURES_FILE = "GameLogicTests/KnownPictures.txt";

/// The twins' outcomes (D33), from the repository's root: beside each known answer, the outcomes of the same state
/// (Elite::OutcomeText), under the same twin and ordinal. The file's header documents its format.
inline constexpr std::string_view KNOWN_OUTCOMES_FILE = "GameLogicTests/KnownOutcomes.txt";

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
    : m_answers(KNOWN_ANSWERS_FILE, TwinAnswers::Shape::Sha256)
  {
  }

  /// Asserts that _digest, which _what names, is _twin's known answer _ordinal. A missing answer fails too, naming the
  /// twin and the ordinal.
  void Check(const std::string& _twin, std::size_t _ordinal, std::string_view _digest, const std::wstring& _what) const
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    const std::string* const found = m_answers.Find(_twin, _ordinal);
    if (found == nullptr)
    {
      Assert::Fail((_what + L" is " + Widen(_digest) + L", and " +
                    Widen(std::format("{} has no known answer {} in {}", _twin, _ordinal, KNOWN_ANSWERS_FILE)))
                     .c_str());
    }
    else if (*found != _digest)
    {
      Assert::Fail(
        (_what + L" is " + Widen(_digest) + L", and " + Widen(std::format("{}'s known answer {} is {}", _twin, _ordinal, *found))).c_str());
    }
  }

private:
  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

  TwinAnswers m_answers;
};

/// A twin (ADR-016): the reference booted to its title screen, or started at power-on, on a Dispatcher with the native
/// routines in place (ReferenceRig), and then a scenario no replay reaches, played step by step. A test sets up its state
/// through Both, from the values the game's own code writes, and plays it with Play. Each digest it gives, at every digest
/// step and at the end of every Play, must be the twin's next known answer (KnownAnswers), which the interpreted original
/// gave for the same scenario before D7 deleted it. On a Dispatcher nothing is interpreted, so a twin also shows that its
/// scenario reaches no code that no native routine stands in for.
///
/// Beside each answer, the picture and the outcomes of the same state (D33) are checked against the ones recorded in
/// KNOWN_PICTURES_FILE and KNOWN_OUTCOMES_FILE (GuardReport). A picture, or a timing outcome, that differs is reported: it
/// is written to the test's log with both values. An outcome of the player's progress that differs fails, and so does a
/// picture or outcomes not recorded. While the guard is recorded (RecordingGuard), a twin keeps its pictures and outcomes
/// instead and writes them when it is done, only if every state digest it gave met its known answer.
class TwinRig
{
public:
  explicit TwinRig(std::string_view _name, const TwinOptions& _options = {})
    : m_name(_name),
      m_report(Widen(_name)),
      m_recording(RecordingGuard()),
      m_exceptions(std::uncaught_exceptions()),
      m_rig(_name, _options.startMoment, _options.mousePresent),
      m_player(m_rig.Host(), m_rig.State())
  {
    if (!_options.fromPowerOn)
    {
      Play("wait 3");
    }
  }

  TwinRig(const TwinRig&) = delete;
  TwinRig& operator=(const TwinRig&) = delete;
  TwinRig(TwinRig&&) = delete;
  TwinRig& operator=(TwinRig&&) = delete;

  /// A twin that is done, and threw nothing, so that every state digest it gave met its known answer: while the guard is
  /// recorded it writes its pictures and outcomes, replacing the ones it had; otherwise it writes its summary to the test's
  /// log.
  ~TwinRig()
  {
    if (std::uncaught_exceptions() > m_exceptions)
    {
      return;
    }
    if (!m_recording)
    {
      m_report.LogSummary();
    }
    else if (!TwinAnswers::Record(KNOWN_PICTURES_FILE, TwinAnswers::Shape::Sha256, m_name, m_recordedPictures) ||
             !TwinAnswers::Record(KNOWN_OUTCOMES_FILE, TwinAnswers::Shape::Outcomes, m_name, m_recordedOutcomes))
    {
      try
      {
        GuardReport::Log(Widen(m_name) + L": its pictures and outcomes could not be written");
      }
      catch (...)
      {
        return;
      }
    }
  }

  /// Calls _change(pc, program) on the machine: how a test sets up a state no replay reaches, through Ram() and the data
  /// segment (Elite::DataSegment). The name is the one the twins' set-ups were written with, when there were two machines.
  template <typename Change> void Both(Change _change)
  {
    _change(m_rig.Host(), m_rig.Program());
  }

  /// The game's memory, for a set-up that calls the game's own routines on it.
  [[nodiscard]] Elite::GameState& State() noexcept
  {
    return m_rig.State();
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
      const Elite::Checkpoint checkpoint = PlayStep(step, Stop(index, steps.size(), _end), where);
      if (step.kind == Elite::StepKind::Digest)
      {
        Answer(checkpoint, where);
      }
    }
    Answer(Elite::TakeCheckpoint(m_rig.State(), m_rig.Host().Video()), Widen(m_name) + L", the state Play ends in");
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

  /// Plays _step, which must stop the run as _expected says, and returns its checkpoint: a digest step's, or empty.
  [[nodiscard]] Elite::Checkpoint PlayStep(const Elite::Step& _step, Machine::StopReason _expected, const std::wstring& _where)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Elite::Checkpoint checkpoint;
    const Machine::StopReason stopped = m_player.Play(_step, checkpoint);
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
    return checkpoint;
  }

  /// _checkpoint, which _what names, is the twin's next answer: its digest must be the known answer, and its picture and
  /// outcomes are checked against the recorded ones (GuardReport), which fails on an outcome of the player's progress that
  /// differs and reports the rest; a picture or outcomes not recorded fail. While the guard is recorded, the picture and
  /// the outcomes are kept for the destructor to write instead.
  void Answer(const Elite::Checkpoint& _checkpoint, const std::wstring& _what)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    ++m_digests;
    m_answers.Check(m_name, m_digests, _checkpoint.digest, _what + L": the digest");
    if (m_recording)
    {
      m_recordedPictures.push_back(_checkpoint.picture);
      m_recordedOutcomes.push_back(_checkpoint.outcomes);
      return;
    }
    const std::string* const picture = m_pictures.Find(m_name, m_digests);
    if (picture == nullptr)
    {
      Assert::Fail((_what + L": the picture is " + Widen(_checkpoint.picture) + L", and " +
                    Widen(std::format("{} has no recorded picture {} in {}", m_name, m_digests, KNOWN_PICTURES_FILE)))
                     .c_str());
    }
    const std::wstring point = _what + L", answer " + std::to_wstring(m_digests);
    m_report.Picture(point, *picture, _checkpoint.picture);
    const std::string* const outcomes = m_outcomes.Find(m_name, m_digests);
    if (outcomes == nullptr)
    {
      Assert::Fail((point + L": the outcomes are " + Widen(_checkpoint.outcomes) + L", and " +
                    Widen(std::format("{} has no recorded outcomes {} in {}", m_name, m_digests, KNOWN_OUTCOMES_FILE)))
                     .c_str());
    }
    if (const std::wstring failure = m_report.Outcomes(point, *outcomes, _checkpoint.outcomes); !failure.empty())
    {
      Assert::Fail(failure.c_str());
    }
  }

  [[nodiscard]] static std::wstring Widen(std::string_view _text)
  {
    return std::wstring(_text.begin(), _text.end());
  }

  std::string m_name;
  KnownAnswers m_answers;
  TwinAnswers m_pictures{KNOWN_PICTURES_FILE, TwinAnswers::Shape::Sha256};
  TwinAnswers m_outcomes{KNOWN_OUTCOMES_FILE, TwinAnswers::Shape::Outcomes};
  GuardReport m_report;
  bool m_recording = false;                    // the guard is recorded, not checked (RecordingGuard)
  int m_exceptions = 0;                        // std::uncaught_exceptions() when the twin was made
  std::vector<std::string> m_recordedPictures; // while recording, the pictures by ordinal from 1
  std::vector<std::string> m_recordedOutcomes; // while recording, the outcomes by ordinal from 1
  std::size_t m_digests = 0;                   // the twin's digests so far, the last one's ordinal
  ReferenceRig m_rig;
  Elite::ReplayPlayer m_player;
};

} // namespace GameLogicTests
