// Tests/GameLogicTests/TwinRig.h
#pragma once

#include "AnswerFile.h"
#include "GameState.h"
#include "StateDumps.h"
#include "GuardReport.h"
#include "ReferenceRig.h"
#include "Replay.h"
#include "StateDigest.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace GameLogicTests
{

/// How a TwinRig sets up its machine.
struct TwinOptions
{
  Elite::DosDateTime startMoment = Elite::START_MOMENT; ///< DOS's clock at power-on
  bool fromPowerOn = false;  ///< the twin starts at power-on, its digests counted from there, rather than at the title screen
  bool mousePresent = false; ///< a mouse driver is loaded, the platform's (Elite::Mouse)
};

/// The twins' known answers, from the repository's root. The file's header documents its format.
inline constexpr std::string_view KNOWN_ANSWERS_FILE = "Tests/GameLogicTests/KnownAnswers.txt";

/// The twins' pictures (D33), from the repository's root: beside each known answer, the picture digest of the same state,
/// under the same twin and ordinal. The file's header documents its format.
inline constexpr std::string_view KNOWN_PICTURES_FILE = "Tests/GameLogicTests/KnownPictures.txt";

/// The twins' outcomes (D33), from the repository's root: beside each known answer, the outcomes of the same state
/// (Elite::OutcomeText), under the same twin and ordinal. The file's header documents its format.
inline constexpr std::string_view KNOWN_OUTCOMES_FILE = "Tests/GameLogicTests/KnownOutcomes.txt";

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

/// A byte of the game's data that a twin's set-up names by member pointer: a member of one of GameData's groups
/// (`&Elite::PlayerShipState::playerDead`), of the commander (`&Elite::CommanderState::fuel`), or of the commander's cargo hold
/// (`&Elite::CommanderState::CargoHold::cargoUsedTonnes`). GameData holds each of those structs once, so the pointer's class
/// says where the byte is (DataByteIn).
template <typename Struct> using DataByte = Elite::Field<std::uint8_t> Struct::*;

/// The struct of _data a twin's member pointers name members of (DataByte): the groups the twins set up, the commander and its
/// cargo hold.
template <typename Struct> [[nodiscard]] Struct& StructIn(Elite::GameData& _data) noexcept
{
  if constexpr (std::is_same_v<Struct, Elite::CommanderState>)
  {
    return _data.commander;
  }
  else if constexpr (std::is_same_v<Struct, Elite::CommanderState::CargoHold>)
  {
    return _data.commander.cargoHold;
  }
  else if constexpr (std::is_same_v<Struct, Elite::PlayerShipState>)
  {
    return _data.playerShip;
  }
  else if constexpr (std::is_same_v<Struct, Elite::FlightState>)
  {
    return _data.flight;
  }
  else if constexpr (std::is_same_v<Struct, Elite::InputState>)
  {
    return _data.input;
  }
  else if constexpr (std::is_same_v<Struct, Elite::SaveLoadState>)
  {
    return _data.saveLoad;
  }
  else
  {
    static_assert(std::is_same_v<Struct, Elite::StartUpState>, "a group the twins set up, the commander or its cargo hold");
    return _data.startUp;
  }
}

/// The byte _member names in _state's data (DataByte).
template <typename Struct> [[nodiscard]] Elite::Field<std::uint8_t>& DataByteIn(Elite::GameState& _state, DataByte<Struct> _member) noexcept
{
  return StructIn<Struct>(_state.Data()).*_member;
}

/// A twin (ADR-016): the reference booted to its title screen, or started at power-on, on the native platform
/// (ReferenceRig), and then a scenario no replay reaches, played step by step. A test sets up its state through Both, from
/// the values the game's own code writes, and plays it with Play. Each digest it gives, at every digest step and at the end
/// of every Play, must be the twin's next known answer (KnownAnswers), which the interpreted original gave for the same
/// scenario before D7 deleted it. A twin also shows that its scenario sends no interrupt to code no native routine stands in
/// for.
///
/// Beside each answer, the picture and the outcomes of the same state (D33) are checked against the ones recorded in
/// KNOWN_PICTURES_FILE and KNOWN_OUTCOMES_FILE (GuardReport). A picture, or a timing outcome, that differs is reported: it
/// is written to the test's log with both values. An outcome of the player's progress that differs fails, and so does a
/// picture or outcomes not recorded. While the guard is recorded (RecordingGuard), a twin keeps its pictures and outcomes
/// instead and writes them when it is done, only if every state digest it gave met its known answer. While the data segment
/// is dumped or the digests re-based (StateDumps), each answer's data segment is dumped or held to the dump before the
/// change instead of its digest to the known answer; a re-base fails on any picture or outcome that differs, and the twin
/// writes its digests as its known answers when it is done.
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
  /// log. While the digests are re-based, every one it gave met the re-base's gate, and it writes them as its known answers.
  ~TwinRig()
  {
    if (std::uncaught_exceptions() > m_exceptions)
    {
      return;
    }
    try
    {
      if (StateDumps::Run().Mode() == DigestMode::Rebase &&
          !TwinAnswers::Record(KNOWN_ANSWERS_FILE, TwinAnswers::Shape::Sha256, m_name, m_digestsGiven))
      {
        GuardReport::Log(Widen(m_name) + L": its re-based digests could not be written");
      }
    }
    catch (...)
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

  /// Calls _change(platform, state) on the machine: how a test sets up a state no replay reaches, through the game's memory
  /// and the platform's devices. The name is the one the twins' set-ups were written with, when there were two machines.
  template <typename Change> void Both(Change _change)
  {
    _change(m_rig.Host(), m_rig.State());
  }

  /// The game's memory, for a set-up that calls the game's own routines on it.
  [[nodiscard]] Elite::GameState& State() noexcept
  {
    return m_rig.State();
  }

  /// Plays _steps, in Replay.h's syntax. Each must reach the end of every step but the last, which must stop the run as
  /// _end says: StopReason::Terminated for a test that leaves the program. Every digest step, and the state the steps end
  /// in, must be the twin's next known answer.
  void Play(std::string_view _steps, Elite::StopReason _end = Elite::StopReason::Reached)
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

  [[nodiscard]] Elite::Platform& Host() noexcept
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
  [[nodiscard]] static Elite::StopReason Stop(std::size_t _index, std::size_t _count, Elite::StopReason _end) noexcept
  {
    return _index + 1 == _count ? _end : Elite::StopReason::Reached;
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
  [[nodiscard]] Elite::Checkpoint PlayStep(const Elite::Step& _step, Elite::StopReason _expected, const std::wstring& _where)
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Elite::Checkpoint checkpoint;
    const Elite::StopReason stopped = m_player.Play(_step, checkpoint);
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
    const StateDumps& dumps = StateDumps::Run();
    dumps.Point(m_rig.State(), "twins", m_name, m_digests);
    if (dumps.Mode() == DigestMode::Check)
    {
      m_answers.Check(m_name, m_digests, _checkpoint.digest, _what + L": the digest");
    }
    m_digestsGiven.push_back(_checkpoint.digest);
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
    if (dumps.Mode() == DigestMode::Rebase && (*picture != _checkpoint.picture || *outcomes != _checkpoint.outcomes))
    {
      Assert::Fail((point + L": the picture or the outcomes differ, and a re-base needs every one as recorded").c_str());
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
  std::vector<std::string> m_digestsGiven;     // the state digests, by ordinal from 1, which a re-base records
  std::size_t m_digests = 0;                   // the twin's digests so far, the last one's ordinal
  ReferenceRig m_rig;
  Elite::ReplayPlayer m_player;
};

} // namespace GameLogicTests
