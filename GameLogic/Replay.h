// GameLogic/Replay.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Timing.h"

namespace Machine
{
class Pc;
class FileStore;
struct LoadedProgram;
enum class StopReason : std::uint8_t;
} // namespace Machine

namespace Elite
{

enum class StepKind : std::uint8_t
{
  Wait,   ///< let time pass
  Key,    ///< press and release one key at once: what a menu reads through the key buffer
  Down,   ///< press a key and hold it: what flight reads through the key-down table
  Up,     ///< release a held key
  Shot,   ///< write the screen as a PNG (the caller's business)
  Digest, ///< the SHA-256 of the game's state (GameStateDigest), checked when an expected value is given
  File    ///< copy a file from beside the replay into DOS's directory, as a commander the game can then load
};

struct Step
{
  StepKind kind = StepKind::Wait;
  std::uint64_t waitMilliseconds = 0; ///< Wait
  std::uint8_t scanCode = 0;          ///< Key, Down and Up: the XT make code
  std::string name;                   ///< Key, Down and Up: as written; Shot and Digest: the label; File: the DOS name, canonical
  std::string expectedDigest;         ///< Digest: lowercase hex, or empty for none
  std::string source;                 ///< File: the name of the file beside the replay that it copies
  std::size_t line = 0;               ///< the 1-based line the step was read from
};

/// Parses the steps of a replay (ADR-008), which Tools/ReferenceScreens.py also reads, so one script drives
/// this host and DOSBox-X alike. A step is a verb and its arguments; steps are separated by ';' or a new
/// line, and '#' starts a comment that runs to the end of the line.
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
///
/// Appends the steps to _steps and returns true, or returns false with _error naming the first step it
/// could not read. (Not std::expected: clang 18 with libstdc++ 13 cannot compile it, ADR-004.)
[[nodiscard]] bool ParseSteps(std::string_view _text, std::vector<Step>& _steps, std::string& _error);

/// The XT (scan code set 1) make code of an X keysym name, for the keys the reference reads.
[[nodiscard]] std::optional<std::uint8_t> ScanCodeOf(std::string_view _keyName) noexcept;

/// The X keysym name a replay uses for an XT make code, or nothing for a code an XT keyboard does not
/// send. Where two names share a code (Return and KP_Enter), the first in the table.
[[nodiscard]] std::optional<std::string_view> KeyNameOf(std::uint8_t _scanCode) noexcept;

/// The moment a replay has reached, _milliseconds after it began at machine cycle _start. Time in a replay
/// is counted in whole milliseconds from its start and converted once, so a recorder that samples time at
/// any rate and a player that sums the recorded waits land on the same cycle.
[[nodiscard]] constexpr Machine::Cycles ReplayCycle(Machine::Cycles _start, std::uint64_t _milliseconds) noexcept
{
  return _start + Machine::MicrosecondsToCycles(_milliseconds * 1000);
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

  /// A digest labelled _name, with the value it must reproduce, at _milliseconds.
  void Digest(std::uint64_t _milliseconds, std::string_view _name, std::string_view _value);

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

/// Plays steps on a machine running the reference, from the moment it is made.
class ReplayPlayer
{
public:
  /// A player for steps that copy no file.
  ReplayPlayer(Machine::Pc& _pc, const Machine::LoadedProgram& _program) noexcept;

  /// A player whose file steps copy from _sources, the directory the replay is in, into _files, the store the
  /// machine's DOS was given.
  ReplayPlayer(Machine::Pc& _pc, const Machine::LoadedProgram& _program, Machine::FileStore& _files, std::filesystem::path _sources);

  /// Plays one step: a wait runs the machine on to the next moment (Machine::Pc::RunUntil, ReplayCycle), a
  /// key queues its codes on the keyboard, a digest sets _digest to GameStateDigest, a file step copies its
  /// file into the store. A shot does nothing here; writing pictures is the caller's. Returns why the run
  /// stopped, StopReason::Reached if it did not. Throws std::runtime_error for a file step that cannot be
  /// done: no store, a source that cannot be read, or a store that refuses it. That is a fault in how the
  /// replay is set up, not anything the game did.
  [[nodiscard]] Machine::StopReason Play(const Step& _step, std::string& _digest);

  /// Milliseconds since the replay began.
  [[nodiscard]] std::uint64_t ElapsedMilliseconds() const noexcept
  {
    return m_elapsedMilliseconds;
  }

private:
  void CopyFile(const Step& _step);

  Machine::Pc& m_pc;
  const Machine::LoadedProgram& m_program;
  Machine::FileStore* m_files = nullptr;
  std::filesystem::path m_sources;
  Machine::Cycles m_start = 0;
  std::uint64_t m_elapsedMilliseconds = 0;
};

} // namespace Elite
