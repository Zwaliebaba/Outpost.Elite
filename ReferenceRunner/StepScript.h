// ReferenceRunner/StepScript.h
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ReferenceRunner
{

enum class StepKind : std::uint8_t
{
  Wait,  // let emulated time pass
  Key,   // press and release one key
  Shot,  // write the screen as a PNG
  Digest // print a SHA-256 of the machine's state
};

struct Step
{
  StepKind kind = StepKind::Wait;
  std::uint64_t waitMilliseconds = 0; // Wait
  std::uint8_t scanCode = 0;          // Key: the XT make code
  std::string name;                   // Key: as written; Shot and Digest: the label
};

/// Parses the steps that Tools/ReferenceScreens.py also reads, so one script drives this host and
/// DOSBox-X alike. Steps are separated by ';':
///
///   wait N       N seconds, with up to three decimals (`wait 0.25`)
///   key NAME     an X keysym name, as xdotool takes it: F1-F10, space, Escape, Return, a-z, 0-9, Up...
///   shot NAME    write NAME.png
///   digest NAME  print NAME and the SHA-256 of memory and registers (this host only)
///
/// Appends the steps to _steps and returns true, or returns false with _error naming the first step it
/// could not read. (Not std::expected: clang 18 with libstdc++ 13 cannot compile it, ADR-004.)
[[nodiscard]] bool ParseSteps(std::string_view _text, std::vector<Step>& _steps, std::string& _error);

/// The XT (scan code set 1) make code of an X keysym name, for the keys the reference reads.
[[nodiscard]] std::optional<std::uint8_t> ScanCodeOf(std::string_view _keyName) noexcept;

} // namespace ReferenceRunner
