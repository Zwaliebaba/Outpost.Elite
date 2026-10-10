#include "pch.h"

#include "Replay.h"

#include "Pc.h"
#include "StateDigest.h"

#include <array>
#include <utility>

namespace Elite
{

namespace
{

struct KeyName
{
  std::string_view name;
  std::uint8_t scanCode;
};

// Scan code set 1, as the XT keyboard sends it (the PC/XT Technical Reference, keyboard section).
constexpr auto KEY_NAMES = std::to_array<KeyName>({
  {"Escape", 0x01},      {"1", 0x02},           {"2", 0x03},
  {"3", 0x04},           {"4", 0x05},           {"5", 0x06},
  {"6", 0x07},           {"7", 0x08},           {"8", 0x09},
  {"9", 0x0A},           {"0", 0x0B},           {"minus", 0x0C},
  {"equal", 0x0D},       {"BackSpace", 0x0E},   {"Tab", 0x0F},
  {"q", 0x10},           {"w", 0x11},           {"e", 0x12},
  {"r", 0x13},           {"t", 0x14},           {"y", 0x15},
  {"u", 0x16},           {"i", 0x17},           {"o", 0x18},
  {"p", 0x19},           {"bracketleft", 0x1A}, {"bracketright", 0x1B},
  {"Return", 0x1C},      {"Control_L", 0x1D},   {"a", 0x1E},
  {"s", 0x1F},           {"d", 0x20},           {"f", 0x21},
  {"g", 0x22},           {"h", 0x23},           {"j", 0x24},
  {"k", 0x25},           {"l", 0x26},           {"semicolon", 0x27},
  {"apostrophe", 0x28},  {"grave", 0x29},       {"Shift_L", 0x2A},
  {"backslash", 0x2B},   {"z", 0x2C},           {"x", 0x2D},
  {"c", 0x2E},           {"v", 0x2F},           {"b", 0x30},
  {"n", 0x31},           {"m", 0x32},           {"comma", 0x33},
  {"period", 0x34},      {"slash", 0x35},       {"Shift_R", 0x36},
  {"KP_Multiply", 0x37}, {"Alt_L", 0x38},       {"space", 0x39},
  {"Caps_Lock", 0x3A},   {"F1", 0x3B},          {"F2", 0x3C},
  {"F3", 0x3D},          {"F4", 0x3E},          {"F5", 0x3F},
  {"F6", 0x40},          {"F7", 0x41},          {"F8", 0x42},
  {"F9", 0x43},          {"F10", 0x44},         {"Num_Lock", 0x45},
  {"Scroll_Lock", 0x46}, {"Home", 0x47},        {"Up", 0x48},
  {"Prior", 0x49},       {"KP_Subtract", 0x4A}, {"Left", 0x4B},
  {"KP_Begin", 0x4C},    {"Right", 0x4D},       {"KP_Add", 0x4E},
  {"End", 0x4F},         {"Down", 0x50},        {"Next", 0x51},
  {"Insert", 0x52},      {"Delete", 0x53},      {"KP_Enter", 0x1C},
});

constexpr std::string_view WHITESPACE = " \t\r";
constexpr std::uint8_t BREAK_BIT = 0x80;
constexpr std::size_t DIGEST_HEX_DIGITS = 64;

std::string_view Trim(std::string_view _text) noexcept
{
  const std::size_t first = _text.find_first_not_of(WHITESPACE);
  if (first == std::string_view::npos)
    return {};
  return _text.substr(first, _text.find_last_not_of(WHITESPACE) - first + 1);
}

// "12", "0.5" or "1.250" as milliseconds, without going through floating point.
std::optional<std::uint64_t> ParseSeconds(std::string_view _text) noexcept
{
  std::uint64_t milliseconds = 0;
  std::size_t index = 0;
  bool anyDigit = false;
  for (; index < _text.size() && _text[index] >= '0' && _text[index] <= '9'; ++index)
  {
    if (milliseconds > 1'000'000'000)
      return std::nullopt;
    milliseconds = milliseconds * 10 + static_cast<std::uint64_t>(_text[index] - '0');
    anyDigit = true;
  }
  milliseconds *= 1000;
  if (index < _text.size() && _text[index] == '.')
  {
    ++index;
    std::uint64_t scale = 100;
    for (; index < _text.size() && _text[index] >= '0' && _text[index] <= '9'; ++index)
    {
      if (scale == 0)
        return std::nullopt; // more than three decimals
      milliseconds += static_cast<std::uint64_t>(_text[index] - '0') * scale;
      scale /= 10;
      anyDigit = true;
    }
  }
  if (!anyDigit || index != _text.size())
    return std::nullopt;
  return milliseconds;
}

bool IsDigestHex(std::string_view _text) noexcept
{
  if (_text.size() != DIGEST_HEX_DIGITS)
    return false;
  for (const char digit : _text)
  {
    if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
      return false;
  }
  return true;
}

// The words of one step: at most three are ever needed.
std::vector<std::string_view> Words(std::string_view _text)
{
  std::vector<std::string_view> words;
  while (!_text.empty())
  {
    const std::size_t gap = _text.find_first_of(WHITESPACE);
    words.push_back(_text.substr(0, gap));
    _text = gap == std::string_view::npos ? std::string_view{} : Trim(_text.substr(gap));
  }
  return words;
}

bool ParseStep(std::string_view _text, std::size_t _line, std::vector<Step>& _steps, std::string& _error)
{
  const std::vector<std::string_view> words = Words(_text);
  const auto failure = [&](std::string_view _why)
  {
    _error = "line " + std::to_string(_line) + ": " + std::string(_why) + ": '" + std::string(_text) + "'";
    return false;
  };
  const std::string_view verb = words.front();
  const bool isDigest = verb == "digest";
  if (words.size() < 2 || words.size() > (isDigest ? 3u : 2u))
    return failure(isDigest ? "digest takes a label and an optional value" : "a step is a verb and one argument");

  Step step;
  step.line = _line;
  step.name = std::string(words[1]);
  if (verb == "wait")
  {
    const std::optional<std::uint64_t> milliseconds = ParseSeconds(words[1]);
    if (!milliseconds)
      return failure("wait takes seconds, with at most three decimals");
    step.kind = StepKind::Wait;
    step.waitMilliseconds = *milliseconds;
  }
  else if (verb == "key" || verb == "down" || verb == "up")
  {
    const std::optional<std::uint8_t> scanCode = ScanCodeOf(words[1]);
    if (!scanCode)
      return failure("unknown key name");
    step.kind = verb == "key" ? StepKind::Key : verb == "down" ? StepKind::Down : StepKind::Up;
    step.scanCode = *scanCode;
  }
  else if (verb == "shot")
    step.kind = StepKind::Shot;
  else if (isDigest)
  {
    step.kind = StepKind::Digest;
    if (words.size() == 3)
    {
      if (!IsDigestHex(words[2]))
        return failure("a digest value is 64 lowercase hex digits");
      step.expectedDigest = std::string(words[2]);
    }
  }
  else
    return failure("steps are wait, key, down, up, shot and digest");
  _steps.push_back(std::move(step));
  return true;
}

} // namespace

std::optional<std::uint8_t> ScanCodeOf(std::string_view _keyName) noexcept
{
  for (const KeyName& key : KEY_NAMES)
  {
    if (key.name == _keyName)
      return key.scanCode;
  }
  return std::nullopt;
}

bool ParseSteps(std::string_view _text, std::vector<Step>& _steps, std::string& _error)
{
  std::size_t line = 1;
  while (!_text.empty())
  {
    const std::size_t end = _text.find('\n');
    std::string_view text = _text.substr(0, end);
    _text = end == std::string_view::npos ? std::string_view{} : _text.substr(end + 1);
    if (const std::size_t comment = text.find('#'); comment != std::string_view::npos)
      text = text.substr(0, comment);
    while (!text.empty())
    {
      const std::size_t separator = text.find(';');
      const std::string_view step = Trim(text.substr(0, separator));
      text = separator == std::string_view::npos ? std::string_view{} : text.substr(separator + 1);
      if (!step.empty() && !ParseStep(step, line, _steps, _error))
        return false;
    }
    ++line;
  }
  return true;
}

Machine::StopReason PlayStep(Machine::Pc& _pc, const Machine::LoadedProgram& _program, const Step& _step, std::string& _digest)
{
  switch (_step.kind)
  {
  case StepKind::Wait:
    return _pc.RunUntil(_pc.Clock() + Machine::MicrosecondsToCycles(_step.waitMilliseconds * 1000));
  case StepKind::Key:
    _pc.KeyboardController().Inject(_step.scanCode);
    _pc.KeyboardController().Inject(static_cast<std::uint8_t>(_step.scanCode | BREAK_BIT));
    break;
  case StepKind::Down:
    _pc.KeyboardController().Inject(_step.scanCode);
    break;
  case StepKind::Up:
    _pc.KeyboardController().Inject(static_cast<std::uint8_t>(_step.scanCode | BREAK_BIT));
    break;
  case StepKind::Digest:
    _digest = GameStateDigest(_pc, _program);
    break;
  case StepKind::Shot:
    break;
  }
  return Machine::StopReason::Reached;
}

} // namespace Elite
