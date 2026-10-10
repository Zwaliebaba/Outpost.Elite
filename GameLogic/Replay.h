// GameLogic/Replay.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Machine
{
class Pc;
struct LoadedProgram;
enum class StopReason : std::uint8_t;
} // namespace Machine

namespace Elite
{

enum class StepKind : std::uint8_t
{
  Wait,  ///< let time pass
  Key,   ///< press and release one key at once: what a menu reads through the key buffer
  Down,  ///< press a key and hold it: what flight reads through the key-down table
  Up,    ///< release a held key
  Shot,  ///< write the screen as a PNG (the caller's business)
  Digest ///< the SHA-256 of the game's state (GameStateDigest), checked when an expected value is given
};

struct Step
{
  StepKind kind = StepKind::Wait;
  std::uint64_t waitMilliseconds = 0; ///< Wait
  std::uint8_t scanCode = 0;          ///< Key, Down and Up: the XT make code
  std::string name;                   ///< Key, Down and Up: as written; Shot and Digest: the label
  std::string expectedDigest;         ///< Digest: lowercase hex, or empty for none
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
///
/// Appends the steps to _steps and returns true, or returns false with _error naming the first step it
/// could not read. (Not std::expected: clang 18 with libstdc++ 13 cannot compile it, ADR-004.)
[[nodiscard]] bool ParseSteps(std::string_view _text, std::vector<Step>& _steps, std::string& _error);

/// The XT (scan code set 1) make code of an X keysym name, for the keys the reference reads.
[[nodiscard]] std::optional<std::uint8_t> ScanCodeOf(std::string_view _keyName) noexcept;

/// Plays one step on a machine running the reference: a wait runs it on (Machine::Pc::RunUntil), a key
/// queues its codes on the keyboard, a digest sets _digest to GameStateDigest. A shot does nothing here;
/// writing pictures is the caller's. Returns why the run stopped, StopReason::Reached if it did not.
[[nodiscard]] Machine::StopReason PlayStep(Machine::Pc& _pc, const Machine::LoadedProgram& _program, const Step& _step,
                                           std::string& _digest);

} // namespace Elite
