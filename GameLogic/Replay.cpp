#include "pch.h"

#include "Replay.h"

#include "FileStore.h"
#include "Outcome.h"
#include "Platform.h"
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

// The words of one step: three at most, but for an outcome line's.
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

// The digest step named _name if it is the last of _steps, which a picture or outcome line belongs to; otherwise nothing.
Step* DigestBefore(std::vector<Step>& _steps, std::string_view _name) noexcept
{
  if (_steps.empty() || _steps.back().kind != StepKind::Digest || _steps.back().name != _name)
    return nullptr;
  return &_steps.back();
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
  if (verb == "picture")
  {
    // Not a step: the expected picture of the digest step just before it.
    if (words.size() != 3)
      return failure("picture takes the label of the digest before it and a value");
    if (!IsDigestHex(words[2]))
      return failure("a picture's value is 64 lowercase hex digits");
    Step* const digest = DigestBefore(_steps, words[1]);
    if (digest == nullptr)
      return failure("a picture follows the digest step it names");
    if (!digest->expectedPicture.empty())
      return failure("that digest has its picture already");
    digest->expectedPicture = std::string(words[2]);
    return true;
  }
  if (verb == "outcome")
  {
    // Not a step either: the expected outcomes of the digest step just before it.
    if (words.size() < 3)
      return failure("outcome takes the label of the digest before it and its outcomes");
    std::string outcomes;
    for (std::size_t index = 2; index < words.size(); ++index)
      outcomes += std::string(index == 2 ? "" : " ") + std::string(words[index]);
    std::string why;
    if (!CheckOutcomeText(outcomes, why))
      return failure("the outcomes are not name=value pairs of Outcome.h's names: " + why);
    Step* const digest = DigestBefore(_steps, words[1]);
    if (digest == nullptr)
      return failure("an outcome line follows the digest step it names");
    if (!digest->expectedOutcomes.empty())
      return failure("that digest has its outcomes already");
    digest->expectedOutcomes = std::move(outcomes);
    return true;
  }
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
    if (FileStore::CanonicalName(words[1], canonical) != DosError::None)
      return failure("a file's copy is named with a DOS 8.3 name");
    if (!IsBareFileName(words[2]))
      return failure("file copies a file beside the replay, named without a directory");
    step.kind = StepKind::File;
    step.name = std::move(canonical);
    step.source = std::string(words[2]);
  }
  else
    return failure("steps are wait, key, down, up, shot, digest, file and end, and a digest's picture and outcome");
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

bool RecordCheckpoints(std::string_view _text, const std::map<std::size_t, Checkpoint>& _checkpoints, std::string& _recorded,
                       std::string& _error)
{
  std::string recorded;
  std::size_t written = 0;
  bool replacing = false; // past a recorded digest's line, whose old picture and outcome lines the new ones replace
  std::size_t line = 0;
  while (!_text.empty())
  {
    ++line;
    const std::size_t newline = _text.find('\n');
    const std::string_view whole = _text.substr(0, newline == std::string_view::npos ? _text.size() : newline + 1);
    _text.remove_prefix(whole.size());
    // The line's end, which the lines written after it repeat: CRLF, LF, or none on a last line.
    std::string_view ending = whole.ends_with("\r\n") ? "\r\n" : whole.ends_with('\n') ? "\n" : "";
    const std::string_view content = Trim(whole.substr(0, std::min(whole.find('#'), whole.size() - ending.size())));
    const std::vector<std::string_view> words = Words(content);
    if (replacing && !words.empty() && (words.front() == "picture" || words.front() == "outcome"))
      continue;
    replacing = replacing && words.empty();
    recorded += whole;

    const auto checkpoint = _checkpoints.find(line);
    if (checkpoint == _checkpoints.end())
      continue;
    if (words.size() < 2 || words.front() != "digest" || content.find(';') != std::string_view::npos)
    {
      _error = std::format("line {}: what is to be recorded there is not a digest step alone on its line", line);
      return false;
    }
    if (ending.empty())
    {
      ending = "\n";
      recorded += ending;
    }
    recorded += std::format("picture {} {}", words[1], checkpoint->second.picture);
    recorded += ending;
    recorded += std::format("outcome {} {}", words[1], checkpoint->second.outcomes);
    recorded += ending;
    replacing = true;
    ++written;
  }
  if (written != _checkpoints.size())
  {
    _error = std::format("a digest step to record is past the replay's {} lines", line);
    return false;
  }
  _recorded = std::move(recorded);
  return true;
}

bool RecordDigests(std::string_view _text, const std::map<std::size_t, std::string>& _digests, std::string& _recorded, std::string& _error)
{
  std::string recorded;
  std::size_t written = 0;
  std::size_t line = 0;
  while (!_text.empty())
  {
    ++line;
    const std::size_t newline = _text.find('\n');
    const std::string_view whole = _text.substr(0, newline == std::string_view::npos ? _text.size() : newline + 1);
    _text.remove_prefix(whole.size());
    const auto digest = _digests.find(line);
    if (digest == _digests.end())
    {
      recorded += whole;
      continue;
    }
    // The line without its end, CRLF, LF or none on a last line, which is kept with the rest of the line after the digest.
    const std::size_t ending = whole.ends_with("\r\n") ? 2 : whole.ends_with('\n') ? 1 : 0;
    const std::string_view content = Trim(whole.substr(0, std::min(whole.find('#'), whole.size() - ending)));
    const std::vector<std::string_view> words = Words(content);
    if (words.size() != 3 || words.front() != "digest" || content.find(';') != std::string_view::npos)
    {
      _error = std::format("line {}: the digest to record there is not a digest step alone on its line with its digest", line);
      return false;
    }
    // The digest's place in the line, which the new one takes.
    const auto at = static_cast<std::size_t>(words[2].data() - whole.data());
    recorded += whole.substr(0, at);
    recorded += digest->second;
    recorded += whole.substr(at + words[2].size());
    ++written;
  }
  if (written != _digests.size())
  {
    _error = std::format("a digest step to record is past the replay's {} lines", line);
    return false;
  }
  _recorded = std::move(recorded);
  return true;
}

Checkpoint TakeCheckpoint(const GameState& _state, const Cga& _video)
{
  return Checkpoint{GameStateDigest(_state, _video), PictureDigest(_state, _video), OutcomeText(_state)};
}

StopReason ExpectedStop(const Step& _step) noexcept
{
  return _step.kind == StepKind::End ? StopReason::Terminated : StopReason::Reached;
}

ReplayPlayer::ReplayPlayer(Platform& _platform, const GameState& _state) noexcept
  : m_platform(_platform),
    m_state(_state),
    m_start(_platform.Clock())
{
}

ReplayPlayer::ReplayPlayer(Platform& _platform, const GameState& _state, FileStore& _files, std::filesystem::path _sources)
  : m_platform(_platform),
    m_state(_state),
    m_files(&_files),
    m_sources(std::move(_sources)),
    m_start(_platform.Clock())
{
}

StopReason ReplayPlayer::Play(const Step& _step, Checkpoint& _checkpoint)
{
  switch (_step.kind)
  {
  case StepKind::Wait:
  case StepKind::End:
    m_elapsedMilliseconds += _step.waitMilliseconds;
    return m_platform.RunUntil(ReplayCycle(m_start, m_elapsedMilliseconds));
  case StepKind::Key:
    m_platform.Events().Inject(_step.scanCode);
    m_platform.Events().Inject(static_cast<std::uint8_t>(_step.scanCode | BREAK_BIT));
    break;
  case StepKind::Down:
    m_platform.Events().Inject(_step.scanCode);
    break;
  case StepKind::Up:
    m_platform.Events().Inject(static_cast<std::uint8_t>(_step.scanCode | BREAK_BIT));
    break;
  case StepKind::Digest:
    _checkpoint = TakeCheckpoint(m_state, m_platform.Video());
    break;
  case StepKind::File:
    CopyFile(_step);
    break;
  case StepKind::Shot:
    break;
  }
  return StopReason::Reached;
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
  const FileStamp stamp;
  std::unique_ptr<OpenFile> file;
  if (m_files->Create(_step.name, 0, stamp, file) != DosError::None)
    FailFileStep(_step, "the DOS directory refuses to create it");
  std::uint32_t written = 0;
  if (file->Write(bytes, stamp, written) != DosError::None || written != bytes.size())
    FailFileStep(_step, "the DOS directory refuses to write it");
  file.reset();
  if (m_files->SetAttribute(_step.name, 0) != DosError::None)
    FailFileStep(_step, "the DOS directory refuses to clear its attributes");
}

} // namespace Elite
