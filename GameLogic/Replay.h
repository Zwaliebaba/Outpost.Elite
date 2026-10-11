// GameLogic/Replay.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Timing.h"

namespace Elite
{

class Cga;
class FileStore;
class Platform;
enum class StopReason : std::uint8_t;

class GameState;

/// What a digest step takes of the game: the state digest, which checks every byte, the picture digest, which checks what
/// the player sees (StateDigest.h), each in lowercase hex, and the outcomes, the named facts a player could notice
/// (Outcome.h).
struct Checkpoint
{
  std::string digest;   ///< GameStateDigest
  std::string picture;  ///< PictureDigest
  std::string outcomes; ///< OutcomeText
};

/// The checkpoint of the game whose memory _state holds and whose CGA is _video, as it is now.
[[nodiscard]] Checkpoint TakeCheckpoint(const GameState& _state, const Cga& _video);

enum class StepKind : std::uint8_t
{
  Wait,   ///< let time pass
  Key,    ///< press and release one key at once: what a menu reads through the key buffer
  Down,   ///< press a key and hold it: what flight reads through the key-down table
  Up,     ///< release a held key
  Shot,   ///< write the screen as a PNG (the caller's business)
  Digest, ///< the SHA-256 of the game's state (GameStateDigest), checked when an expected value is given
  File,   ///< copy a file from beside the replay into DOS's directory, as a commander the game can then load
  End     ///< let time pass, in which the program must end: a replay that leaves for DOS
};

struct Step
{
  StepKind kind = StepKind::Wait;
  std::uint64_t waitMilliseconds = 0; ///< Wait and End
  std::uint8_t scanCode = 0;          ///< Key, Down and Up: the XT make code
  std::string name;                   ///< Key, Down and Up: as written; Shot and Digest: the label; File: the DOS name, canonical
  std::string expectedDigest;         ///< Digest: lowercase hex, or empty for none
  std::string expectedPicture;        ///< Digest: the picture digest its picture line gives, lowercase hex, or empty for none
  std::string expectedOutcomes;       ///< Digest: the outcomes its outcome line gives (OutcomeText's form), or empty for none
  std::string source;                 ///< File: the name of the file beside the replay that it copies
  std::size_t line = 0;               ///< the 1-based line the step was read from
};

/// Parses the steps of a replay (ADR-008). A step is a verb and its arguments; steps are separated by ';' or a
/// new line, and '#' starts a comment that runs to the end of the line.
///
///   wait S               S seconds, with up to three decimals (`wait 0.25`)
///   key NAME             press and release NAME, an X keysym name as xdotool takes it: F1-F10, space,
///                        Escape, Return, a-z, 0-9, Up, Alt_L...
///   down NAME, up NAME   press NAME and hold it; release it
///   shot NAME            write NAME.png
///   digest NAME [HEX]    the game-state digest, labelled NAME; with HEX, the value it must have
///   file NAME SOURCE     put a copy of SOURCE, a file beside the replay (Replays/ for the corpus), into DOS's
///                        directory as NAME, a DOS 8.3 name, at this moment: a file with no attributes, stamped
///                        1980-01-01 00:00:00, as one copied there before the run would be. This is how a replay
///                        starts from a prepared commander, which the disc menu then loads as the game's own save.
///                        SOURCE is a bare file name, so a replay reaches nothing but the files beside it.
///   end S                S seconds, as wait, by the end of which the program must have ended (int 20h, as
///                        StopReason::Terminated): how a replay that leaves for DOS ends. It is the
///                        last step that runs the machine: only digest and shot steps, which look at what the
///                        program left, may follow it. No other step may end the program (ExpectedStop).
///   picture NAME HEX     not a step but a line of the digest step NAME, which must be the step just before it: the picture
///                        digest (PictureDigest) that digest point gives, in its Step's expectedPicture. A replay keeps every
///                        line it had before pictures were recorded; the picture lines were added, each after its digest's
///                        line (RecordCheckpoints), from runs that met every state digest (D33).
///   outcome NAME FACTS   likewise a line of the digest step NAME just before it, after its picture line if it has one: the
///                        outcomes (Outcome.h) that digest point gives, FACTS being name=value pairs as OutcomeText writes
///                        them, in its Step's expectedOutcomes. Recorded as the picture lines are.
///
/// Appends the steps to _steps and returns true, or returns false with _error naming the first step it
/// could not read. (Not std::expected: clang 18 with libstdc++ 13 cannot compile it, ADR-004.)
[[nodiscard]] bool ParseSteps(std::string_view _text, std::vector<Step>& _steps, std::string& _error);

/// _text, a replay, with the checkpoint of each digest step in _checkpoints, by the line the step is on, written after that
/// line: a picture line, then an outcome line. The picture and outcome lines that followed it before are replaced; every
/// other line is kept, byte for byte, and a new line ends as the digest's line does. This is how the corpus's pictures and
/// outcomes are recorded (CorpusTests), only from runs that met every state digest. Returns false, with _error naming the
/// line, if a digest step in _checkpoints is not alone on its line or no digest step is on it.
[[nodiscard]] bool RecordCheckpoints(std::string_view _text, const std::map<std::size_t, Checkpoint>& _checkpoints, std::string& _recorded,
                                     std::string& _error);

/// _text, a replay, with the state digest of each digest step in _digests, by the line the step is on, written over the one
/// that line holds; every other byte is kept. This is how the corpus's digests are re-based when a change moves them
/// (D33, piece 3), only from runs whose data segment differs from the one before the change in nothing but the fields it
/// converted (Tests/GameLogicTests/StateDumps.h). Returns false, with _error naming the line, if a line in _digests is not a digest
/// step alone on its line with the digest it expects, or is past the replay's end.
[[nodiscard]] bool RecordDigests(std::string_view _text, const std::map<std::size_t, std::string>& _digests, std::string& _recorded,
                                 std::string& _error);

/// How _step must leave the run for its replay to go on (ReplayPlayer::Play): StopReason::Terminated for an end
/// step, whose program must end within it, and StopReason::Reached for every other step, during which the program
/// must not stop. So only a replay's end step may end the program, and an end step that does not end it fails.
[[nodiscard]] StopReason ExpectedStop(const Step& _step) noexcept;

/// The XT (scan code set 1) make code of an X keysym name, for the keys the reference reads.
[[nodiscard]] std::optional<std::uint8_t> ScanCodeOf(std::string_view _keyName) noexcept;

/// The X keysym name a replay uses for an XT make code, or nothing for a code an XT keyboard does not
/// send. Where two names share a code (Return and KP_Enter), the first in the table.
[[nodiscard]] std::optional<std::string_view> KeyNameOf(std::uint8_t _scanCode) noexcept;

/// The moment a replay has reached, _milliseconds after it began at machine cycle _start. Time in a replay
/// is counted in whole milliseconds from its start and converted once, so a recorder that samples time at
/// any rate and a player that sums the recorded waits land on the same cycle.
[[nodiscard]] constexpr Cycles ReplayCycle(Cycles _start, std::uint64_t _milliseconds) noexcept
{
  return _start + MicrosecondsToCycles(_milliseconds * 1000);
}

/// Writes a session as a replay (ADR-008): keys pressed and released, and digests, each at a moment
/// counted in whole milliseconds from the session's start, so that the replay plays back to the cycle
/// (ReplayCycle).
class ReplayRecorder
{
public:
  /// _header is written first, each line as a comment.
  explicit ReplayRecorder(std::string_view _header);

  /// A key pressed (_down) or released at _milliseconds. Moments must not go backwards. Returns false,
  /// and records nothing, for a code KeyNameOf does not name: the caller must not send it to the game
  /// either, or the replay would not be the session.
  bool Key(std::uint64_t _milliseconds, std::uint8_t _scanCode, bool _down);

  /// A digest labelled _name at _milliseconds, with the values _checkpoint says it must reproduce: its digest line, then its
  /// picture line and its outcome line.
  void Digest(std::uint64_t _milliseconds, std::string_view _name, const Checkpoint& _checkpoint);

  /// The replay so far.
  [[nodiscard]] const std::string& Text() const noexcept
  {
    return m_text;
  }

private:
  void WaitUntil(std::uint64_t _milliseconds);

  std::string m_text;
  std::uint64_t m_lastMilliseconds = 0;
};

/// Plays steps on a machine running the reference, from the moment it is made: _platform, and _state, the game's memory it
/// runs on (Game).
class ReplayPlayer
{
public:
  /// A player for steps that copy no file.
  ReplayPlayer(Platform& _platform, const GameState& _state) noexcept;

  /// A player whose file steps copy from _sources, the directory the replay is in, into _files, the store the
  /// machine's DOS was given.
  ReplayPlayer(Platform& _platform, const GameState& _state, FileStore& _files, std::filesystem::path _sources);

  /// Plays one step: a wait or an end runs the machine on to the next moment (Platform::RunUntil, ReplayCycle),
  /// a key queues its codes on the keyboard, a digest sets _checkpoint to TakeCheckpoint, a file step copies its
  /// file into the store. A shot does nothing here; writing pictures is the caller's. Returns why the run
  /// stopped, StopReason::Reached if it did not; ExpectedStop says which the step must return. A digest after
  /// an end step reads the machine as the ended program left it. Throws std::runtime_error for a file step that
  /// cannot be done: no store, a source that cannot be read, or a store that refuses it. That is a fault in how
  /// the replay is set up, not anything the game did.
  [[nodiscard]] StopReason Play(const Step& _step, Checkpoint& _checkpoint);

  /// Milliseconds since the replay began.
  [[nodiscard]] std::uint64_t ElapsedMilliseconds() const noexcept
  {
    return m_elapsedMilliseconds;
  }

private:
  void CopyFile(const Step& _step);

  Platform& m_platform;
  const GameState& m_state;
  FileStore* m_files = nullptr;
  std::filesystem::path m_sources;
  Cycles m_start = 0;
  std::uint64_t m_elapsedMilliseconds = 0;
};

} // namespace Elite
