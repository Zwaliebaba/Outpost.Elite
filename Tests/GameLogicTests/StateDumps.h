// Tests/GameLogicTests/StateDumps.h
#pragma once

#include "AnswerFile.h"
#include "DataPlaces.h"
#include "GameState.h"
#include "ReferenceImage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameLogicTests
{

/// The environment variable that makes a run dump the game's state at every state digest it takes, into the directory it
/// names, instead of checking the digests: one file of GameData's bytes for each digest step of each replay,
/// DIR/replays/<replay>/<line>.bin, and for each known answer of each twin, DIR/twins/<twin>/<ordinal>.bin, and DIR/layout.tsv,
/// where each top-level member of the 1987 data segment is in them (DATA_PLACES). The pictures and outcomes are checked as
/// ever. Tools/CompareStateValues.py reads two such sets, or a set a tree before change 3 of D33's piece 3 wrote at the 1987
/// layout, member by member: the proof a change that moves the state gives that no member's value moved (D38).
inline constexpr const char* DUMP_STATE = "OUTPOST_ELITE_DUMP_STATE";

/// The environment variable that makes a run re-base the state digests from the directory it names, which holds the dumps of
/// the tree before the change, as DUMP_STATE writes them. At every digest it takes, every member of the state but the unused
/// must hold the value the dump before the change gives it, by name, wherever each tree lays it out (DATA_PLACES, and the 1987
/// offsets for a dump with no layout.tsv); every constant the dump's, its value in the reference's image; and the picture and
/// every outcome, timing outcomes too, the recorded ones exactly. Then the run writes its own digests: every replay's digest
/// lines, and each twin's known answers in KnownAnswers.txt, only once all of the replay's or the twin's points have met that
/// gate. It is run by hand, in one process, never in CI, once the subsystems' answers and the turn probe are known to hold; a
/// second run reproduces every file byte for byte.
inline constexpr const char* REBASE_DIGESTS = "OUTPOST_ELITE_REBASE_DIGESTS";

/// What a run does with each state digest it takes.
enum class DigestMode : std::uint8_t
{
  Check,  ///< the digest must be the one recorded
  Dump,   ///< DUMP_STATE: the game's state is written, the digest not checked
  Rebase, ///< REBASE_DIGESTS: the state is held to the dump before the change, member by member, and the digest recorded
};

/// The run's state-digest mode (DigestMode), read from the environment once.
class StateDumps
{
public:
  StateDumps(const StateDumps&) = delete;
  StateDumps& operator=(const StateDumps&) = delete;

  /// The run's: neither variable set is Check. Both set, either where CI is, or a dump directory whose layout cannot be written
  /// or read, fails the test.
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

  /// The game's state of _state at a digest point, _point of _name, of _kind ("replays" or "twins"): written in Dump mode; in
  /// Rebase mode, held to the dump before the change member by member, and a member that differs, or a dump that is missing,
  /// fails the test, naming the first such member. Nothing in Check mode.
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
    Assert::IsFalse(before.empty(), AnswerFile::Widen(file.string() + ": the dump before the change").c_str());
    const std::span<const std::uint8_t> image = std::span<const std::uint8_t>(Elite::REFERENCE_IMAGE).subspan(Elite::CODE_SEGMENT_BYTES);
    for (const Elite::DataPlace& place : Elite::DATA_PLACES)
    {
      if (place.fate == Elite::DataFate::Unused)
      {
        continue;
      }
      const std::optional<std::size_t> found = Before(place, before.size());
      if (!found.has_value())
      {
        Assert::Fail(AnswerFile::Widen(std::format("{}: the dump before the change has no {}", file.string(), place.name)).c_str());
        return;
      }
      const std::size_t was = *found;
      for (std::size_t index = 0; index < place.bytes; ++index)
      {
        const std::uint8_t old = before[was + index];
        const std::size_t original = std::size_t{place.original} + index;
        const std::uint8_t now = place.place != Elite::NO_PLACE ? data[place.place + index] : original < image.size() ? image[original] : 0;
        if (now != old)
        {
          Assert::Fail(AnswerFile::Widen(std::format("{} {} at {}: {}'s byte {} is {:02X}, and was {:02X}", _kind, _name, _point,
                                                     place.name, index, now, old))
                         .c_str());
        }
      }
    }
  }

private:
  /// The name of the file beside the dumps that says where each member is in them.
  static constexpr const char* LAYOUT_FILE = "layout.tsv";

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
      WriteLayout();
    }
    else if (!rebase.empty())
    {
      m_mode = DigestMode::Rebase;
      m_directory = rebase;
      ReadLayout();
    }
  }

  /// DIR/layout.tsv: `NAME ORIGINAL BYTES PLACE` a line, in hex, PLACE `-` for a constant the state does not hold, after a line
  /// `#bytes SIZE`, GameData's size.
  void WriteLayout()
  {
    std::filesystem::create_directories(m_directory);
    std::ofstream stream(m_directory / LAYOUT_FILE, std::ios::binary | std::ios::trunc);
    stream << std::format("#bytes\t{:X}\n", sizeof(Elite::GameData));
    for (const Elite::DataPlace& place : Elite::DATA_PLACES)
    {
      const std::string where = place.place == Elite::NO_PLACE ? std::string("-") : std::format("{:X}", place.place);
      stream << std::format("{}\t{:04X}\t{:X}\t{}\n", place.name, place.original, place.bytes, where);
    }
    stream.close();
    if (stream.fail())
    {
      m_error = (m_directory / LAYOUT_FILE).string() + " cannot be written";
    }
  }

  /// The places the dumps before the change give the members, from their layout.tsv; with none, the dumps are of the 1987 data
  /// segment, every member at its 1987 offset.
  void ReadLayout()
  {
    std::ifstream stream(m_directory / LAYOUT_FILE, std::ios::binary);
    if (!stream)
    {
      return;
    }
    std::string line;
    while (std::getline(stream, line))
    {
      if (line.starts_with("#"))
      {
        continue;
      }
      const std::size_t first = line.find('\t');
      const std::size_t last = line.rfind('\t');
      if (first == std::string::npos || line.substr(last + 1) == "-")
      {
        continue;
      }
      m_beforePlaces.emplace_back(line.substr(0, first), static_cast<std::size_t>(std::stoul(line.substr(last + 1), nullptr, 16)));
    }
  }

  /// Where the dump before the change holds _place's member, a dump of _size bytes: its place there, by name, or its 1987
  /// offset in a dump of the 1987 data segment. Nothing if it holds none of it.
  [[nodiscard]] std::optional<std::size_t> Before(const Elite::DataPlace& _place, std::size_t _size) const
  {
    std::size_t at = _place.original;
    if (!m_beforePlaces.empty())
    {
      const auto found = std::ranges::find_if(m_beforePlaces, [&_place](const auto& _entry) { return _entry.first == _place.name; });
      if (found == m_beforePlaces.end())
      {
        return std::nullopt;
      }
      at = found->second;
    }
    if (at + _place.bytes > _size)
    {
      return std::nullopt;
    }
    return at;
  }

  DigestMode m_mode = DigestMode::Check;
  std::filesystem::path m_directory;
  std::vector<std::pair<std::string, std::size_t>> m_beforePlaces;
  std::string m_error;
};

} // namespace GameLogicTests
