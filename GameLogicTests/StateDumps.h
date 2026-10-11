// GameLogicTests/StateDumps.h
#pragma once

#include "AnswerFile.h"
#include "GameState.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace GameLogicTests
{

/// The environment variable that makes a run dump the data segment at every state digest it takes, into the directory it
/// names, instead of checking the digests: one file of GameData's 65,536 bytes for each digest step of each replay,
/// DIR/replays/<replay>/<line>.bin, and for each known answer of each twin, DIR/twins/<twin>/<ordinal>.bin. The pictures
/// and outcomes are checked as ever. Tools/CompareStateDumps.py compares two such sets: the proof a change that converts
/// stored words gives that nothing else moved (D33, piece 3).
inline constexpr const char* DUMP_STATE = "OUTPOST_ELITE_DUMP_STATE";

/// The environment variable that makes a run re-base the state digests from the directory it names, which holds the dumps
/// of the tree before the change, as DUMP_STATE writes them, and mask.txt, the fields the change converted
/// (CompareStateDumps.py's mask). At every digest it takes, the data segment must be the dump's but in those fields, and the
/// picture and every outcome, timing outcomes too, must be the recorded ones exactly. Then the run writes its own digests:
/// every replay's digest lines, and each twin's known answers in KnownAnswers.txt, only once all of the replay's or the
/// twin's points have met that gate. It is run by hand, in one process, never in CI, once the subsystems' answers and the
/// turn probe are known to hold; a second run reproduces every file byte for byte.
inline constexpr const char* REBASE_DIGESTS = "OUTPOST_ELITE_REBASE_DIGESTS";

/// What a run does with each state digest it takes.
enum class DigestMode : std::uint8_t
{
  Check,  ///< the digest must be the one recorded
  Dump,   ///< DUMP_STATE: the data segment is written, the digest not checked
  Rebase, ///< REBASE_DIGESTS: the data segment is held to the dump before the change, and the digest recorded
};

/// The run's state-digest mode (DigestMode), read from the environment once, with the mask a re-base applies.
class StateDumps
{
public:
  StateDumps(const StateDumps&) = delete;
  StateDumps& operator=(const StateDumps&) = delete;

  /// The run's: neither variable set is Check. Both set, either where CI is, or a re-base whose mask cannot be read, fails
  /// the test.
  [[nodiscard]] static const StateDumps& Run()
  {
    static const StateDumps RUN;
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Assert::IsTrue(RUN.m_mode == DigestMode::Check || EnvironmentVariable("CI").empty(),
                   L"OUTPOST_ELITE_DUMP_STATE or OUTPOST_ELITE_REBASE_DIGESTS is set where CI is: never in CI");
    Assert::IsTrue(RUN.m_mode == DigestMode::Check || EnvironmentVariable(RECORD_GUARD).empty(),
                   L"OUTPOST_ELITE_RECORD_GUARD is set with a dump or a re-base, which check the pictures and outcomes it records");
    Assert::IsTrue(RUN.m_error.empty(), AnswerFile::Widen(RUN.m_error).c_str());
    return RUN;
  }

  [[nodiscard]] DigestMode Mode() const noexcept
  {
    return m_mode;
  }

  /// The data segment of _state at a digest point, _point of _name, of _kind ("replays" or "twins"): written in Dump mode;
  /// in Rebase mode, held to the dump before the change but in the mask, and a byte that differs elsewhere, or a dump that is
  /// missing, fails the test, naming the first such byte. Nothing in Check mode.
  void Point(const Elite::GameState& _state, std::string_view _kind, std::string_view _name, std::size_t _point) const
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    const std::span<const std::uint8_t, sizeof(Elite::GameData)> data = _state.DataBytes();
    const std::filesystem::path file = m_directory / _kind / _name / (std::to_string(_point) + ".bin");
    if (m_mode == DigestMode::Dump)
    {
      std::filesystem::create_directories(file.parent_path());
      std::ofstream stream(file, std::ios::binary | std::ios::trunc);
      stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
      stream.close();
      Assert::IsFalse(stream.fail(), AnswerFile::Widen(file.string() + " cannot be written").c_str());
      return;
    }
    if (m_mode != DigestMode::Rebase)
    {
      return;
    }
    const std::vector<std::uint8_t> before = Elite::ReadWholeFile(file);
    Assert::AreEqual(data.size(), before.size(), AnswerFile::Widen(file.string() + ": the dump before the change").c_str());
    for (std::size_t offset = 0; offset < data.size(); ++offset)
    {
      if (data[offset] != before[offset] && !m_masked[offset])
      {
        Assert::Fail(AnswerFile::Widen(std::format("{} {} at {}: DS:{:04X} is {:02X}, and was {:02X}, outside the fields the change "
                                                   "converted",
                                                   _kind, _name, _point, offset, data[offset], before[offset]))
                       .c_str());
      }
    }
  }

private:
  StateDumps()
  {
    const std::string dump = EnvironmentVariable(DUMP_STATE);
    const std::string rebase = EnvironmentVariable(REBASE_DIGESTS);
    if (!dump.empty() && !rebase.empty())
    {
      m_error = "OUTPOST_ELITE_DUMP_STATE and OUTPOST_ELITE_REBASE_DIGESTS are both set";
      return;
    }
    if (!dump.empty())
    {
      m_mode = DigestMode::Dump;
      m_directory = dump;
    }
    else if (!rebase.empty())
    {
      m_mode = DigestMode::Rebase;
      m_directory = rebase;
      ReadMask(m_directory / "mask.txt");
    }
  }

  /// The ranges of _path, `START END NAME` in hex a line, end exclusive, into m_masked. A line that is not one sets m_error.
  void ReadMask(const std::filesystem::path& _path)
  {
    std::ifstream stream(_path, std::ios::binary);
    if (!stream)
    {
      m_error = _path.string() + " cannot be read";
      return;
    }
    std::string line;
    std::size_t number = 0;
    std::size_t ranges = 0;
    while (std::getline(stream, line))
    {
      ++number;
      std::istringstream words(line);
      std::string startText;
      std::string endText;
      words >> startText >> endText;
      std::size_t start = 0;
      std::size_t end = 0;
      const auto parse = [](const std::string& _text, std::size_t& _value)
      {
        const std::from_chars_result parsed = std::from_chars(_text.data(), _text.data() + _text.size(), _value, 16);
        return !_text.empty() && parsed.ec == std::errc() && parsed.ptr == _text.data() + _text.size();
      };
      if (!parse(startText, start) || !parse(endText, end) || start >= end || end > sizeof(Elite::GameData))
      {
        m_error = std::format("{} line {} is not a range START END NAME in hex", _path.string(), number);
        return;
      }
      for (std::size_t offset = start; offset < end; ++offset)
      {
        m_masked[offset] = true;
      }
      ++ranges;
    }
    if (ranges == 0)
    {
      m_error = _path.string() + " masks nothing";
    }
  }

  DigestMode m_mode = DigestMode::Check;
  std::filesystem::path m_directory;
  std::array<bool, sizeof(Elite::GameData)> m_masked{};
  std::string m_error;
};

} // namespace GameLogicTests
