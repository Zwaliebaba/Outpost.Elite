#include "pch.h"

#include "Replay.h"

#include "FileStore.h"
#include "Pc.h"
#include "StateDigest.h"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
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

// A file name with no directory or drive in it: what a file step may copy, so that a replay reaches only
// the files beside it.
bool IsBareFileName(std::string_view _name) noexcept
{
  return !_name.empty() && _name != "." && _name != ".." && _name.find_first_of("/\\:") == std::string_view::npos;
}

// A file step that cannot be done: the replay's set-up is at fault, not the game.
[[noreturn]] void FailFileStep(const Step& _step, std::string_view _why)
{
  throw std::runtime_error(std::format("line {}: file {} {}: {}", _step.line, _step.name, _step.source, _why));
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

// _ended: an end step has been read, so only digest and shot steps may follow (ExpectedStop).
bool ParseStep(std::string_view _text, std::size_t _line, std::vector<Step>& _steps, bool& _ended, std::string& _error)
{
  const std::vector<std::string_view> words = Words(_text);
  const auto failure = [&](std::string_view _why)
  {
    _error = "line " + std::to_string(_line) + ": " + std::string(_why) + ": '" + std::string(_text) + "'";
    return false;
  };
  const std::string_view verb = words.front();
  const bool isDigest = verb == "digest";
  const bool isFile = verb == "file";
  if (isFile && words.size() != 3)
    return failure("file takes a DOS name and the file beside the replay to copy");
  if (!isFile && (words.size() < 2 || words.size() > (isDigest ? 3u : 2u)))
    return failure(isDigest ? "digest takes a label and an optional value" : "a step is a verb and one argument");

  Step step;
  step.line = _line;
  step.name = std::string(words[1]);
  if (verb == "wait" || verb == "end")
  {
    const std::optional<std::uint64_t> milliseconds = ParseSeconds(words[1]);
    if (!milliseconds)
      return failure(std::string(verb) + " takes seconds, with at most three decimals");
    step.kind = verb == "wait" ? StepKind::Wait : StepKind::End;
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
  else if (isFile)
  {
    std::string canonical;
    if (Machine::FileStore::CanonicalName(words[1], canonical) != Machine::DosError::None)
      return failure("a file's copy is named with a DOS 8.3 name");
    if (!IsBareFileName(words[2]))
      return failure("file copies a file beside the replay, named without a directory");
    step.kind = StepKind::File;
    step.name = std::move(canonical);
    step.source = std::string(words[2]);
  }
  else
    return failure("steps are wait, key, down, up, shot, digest, file and end");
  if (_ended && step.kind != StepKind::Digest && step.kind != StepKind::Shot)
    return failure("only digest and shot may follow end, the last step that runs the machine");
  _ended = _ended || step.kind == StepKind::End;
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

std::optional<std::string_view> KeyNameOf(std::uint8_t _scanCode) noexcept
{
  for (const KeyName& key : KEY_NAMES)
  {
    if (key.scanCode == _scanCode)
      return key.name;
  }
  return std::nullopt;
}

ReplayRecorder::ReplayRecorder(std::string_view _header)
{
  while (!_header.empty())
  {
    const std::size_t end = _header.find('\n');
    m_text += "# ";
    m_text += _header.substr(0, end);
    m_text += '\n';
    _header = end == std::string_view::npos ? std::string_view{} : _header.substr(end + 1);
  }
}

bool ReplayRecorder::Key(std::uint64_t _milliseconds, std::uint8_t _scanCode, bool _down)
{
  const std::optional<std::string_view> name = KeyNameOf(_scanCode);
  if (!name)
    return false;
  WaitUntil(_milliseconds);
  m_text += std::format("{} {}\n", _down ? "down" : "up", *name);
  return true;
}

void ReplayRecorder::Digest(std::uint64_t _milliseconds, std::string_view _name, std::string_view _value)
{
  WaitUntil(_milliseconds);
  m_text += std::format("digest {} {}\n", _name, _value);
}

void ReplayRecorder::WaitUntil(std::uint64_t _milliseconds)
{
  if (_milliseconds <= m_lastMilliseconds)
    return;
  const std::uint64_t wait = _milliseconds - m_lastMilliseconds;
  m_text += std::format("wait {}.{:03}\n", wait / 1000, wait % 1000);
  m_lastMilliseconds = _milliseconds;
}

bool ParseSteps(std::string_view _text, std::vector<Step>& _steps, std::string& _error)
{
  bool ended = std::any_of(_steps.begin(), _steps.end(), [](const Step& _step) { return _step.kind == StepKind::End; });
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
      if (!step.empty() && !ParseStep(step, line, _steps, ended, _error))
        return false;
    }
    ++line;
  }
  return true;
}

Machine::StopReason ExpectedStop(const Step& _step) noexcept
{
  return _step.kind == StepKind::End ? Machine::StopReason::Terminated : Machine::StopReason::Reached;
}

ReplayPlayer::ReplayPlayer(Machine::Pc& _pc, const Machine::LoadedProgram& _program) noexcept
  : m_pc(_pc),
    m_program(_program),
    m_start(_pc.Clock())
{
}

ReplayPlayer::ReplayPlayer(Machine::Pc& _pc, const Machine::LoadedProgram& _program, Machine::FileStore& _files,
                           std::filesystem::path _sources)
  : m_pc(_pc),
    m_program(_program),
    m_files(&_files),
    m_sources(std::move(_sources)),
    m_start(_pc.Clock())
{
}

Machine::StopReason ReplayPlayer::Play(const Step& _step, std::string& _digest)
{
  switch (_step.kind)
  {
  case StepKind::Wait:
  case StepKind::End:
    m_elapsedMilliseconds += _step.waitMilliseconds;
    return m_pc.RunUntil(ReplayCycle(m_start, m_elapsedMilliseconds));
  case StepKind::Key:
    m_pc.KeyboardController().Inject(_step.scanCode);
    m_pc.KeyboardController().Inject(static_cast<std::uint8_t>(_step.scanCode | BREAK_BIT));
    break;
  case StepKind::Down:
    m_pc.KeyboardController().Inject(_step.scanCode);
    break;
  case StepKind::Up:
    m_pc.KeyboardController().Inject(static_cast<std::uint8_t>(_step.scanCode | BREAK_BIT));
    break;
  case StepKind::Digest:
    _digest = GameStateDigest(m_pc, m_program);
    break;
  case StepKind::File:
    CopyFile(_step);
    break;
  case StepKind::Shot:
    break;
  }
  return Machine::StopReason::Reached;
}

void ReplayPlayer::CopyFile(const Step& _step)
{
  if (m_files == nullptr)
    FailFileStep(_step, "this player was given no DOS directory to copy into");
  const std::filesystem::path path = m_sources / _step.source;
  std::error_code error;
  const std::uintmax_t sizeBytes = std::filesystem::file_size(path, error);
  std::ifstream stream(path, std::ios::binary);
  if (error || !stream)
    FailFileStep(_step, std::format("cannot read {}", path.string()));
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sizeBytes));
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream)
    FailFileStep(_step, std::format("cannot read {}", path.string()));

  // Created, then its attributes cleared, as SaveCommanderFile leaves the game's own saves: the loader refuses
  // a file with any attribute set. The stamp is the store's default, DOS's earliest moment.
  const Machine::FileStamp stamp;
  std::unique_ptr<Machine::OpenFile> file;
  if (m_files->Create(_step.name, 0, stamp, file) != Machine::DosError::None)
    FailFileStep(_step, "the DOS directory refuses to create it");
  std::uint32_t written = 0;
  if (file->Write(bytes, stamp, written) != Machine::DosError::None || written != bytes.size())
    FailFileStep(_step, "the DOS directory refuses to write it");
  file.reset();
  if (m_files->SetAttribute(_step.name, 0) != Machine::DosError::None)
    FailFileStep(_step, "the DOS directory refuses to clear its attributes");
}

} // namespace Elite
