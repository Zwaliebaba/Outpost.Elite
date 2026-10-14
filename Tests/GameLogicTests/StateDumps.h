// Tests/GameLogicTests/StateDumps.h
#pragma once

#include "AnswerFile.h"
#include "GameState.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace GameLogicTests
{

/// Every byte and word of a struct of the state, by its path from GameData (`commander.fuel`, `input.keyBuffer[3]`), and its value:
/// the state as Visit lists it (D76), whatever the layout.
class StateLeaves
{
public:
  template <typename T> void operator()(std::string_view _name, const T& _member)
  {
    const std::string path = m_path.empty() ? std::string(_name) : m_path + "." + std::string(_name);
    Leaf(path, _member);
  }

  [[nodiscard]] const std::vector<std::pair<std::string, std::uint32_t>>& Values() const noexcept
  {
    return m_values;
  }

private:
  template <typename T> void Leaf(const std::string& _path, const T& _member)
  {
    if constexpr (std::is_array_v<T>)
    {
      for (std::size_t index = 0; index < std::extent_v<T>; ++index)
      {
        Leaf(std::format("{}[{}]", _path, index), _member[index]);
      }
    }
    else if constexpr (requires { _member.Visit(*this); })
    {
      const std::string outer = m_path;
      m_path = _path;
      _member.Visit(*this);
      m_path = outer;
    }
    else
    {
      m_values.emplace_back(_path, Bits(_member));
    }
  }

  template <typename U> [[nodiscard]] static std::uint32_t Bits(const Elite::Field<U>& _field) noexcept
  {
    const U value = _field;
    if constexpr (std::is_enum_v<U>)
    {
      return static_cast<std::underlying_type_t<U>>(value);
    }
    else
    {
      return value;
    }
  }

  std::string m_path;
  std::vector<std::pair<std::string, std::uint32_t>> m_values;
};

/// The environment variable that makes a run dump the game's state at every state digest it takes, into the directory it
/// names, instead of checking the digests: one file for each digest step of each replay, DIR/replays/<replay>/<line>.txt, and
/// for each known answer of each twin, DIR/twins/<twin>/<ordinal>.txt, each byte and word of the state a line, its path and
/// its value in hex (StateLeaves). The pictures and outcomes are checked as ever.
inline constexpr const char* DUMP_STATE = "OUTPOST_ELITE_DUMP_STATE";

/// The environment variable that makes a run re-base the state digests from the directory it names, which holds the dumps of
/// the tree before the change, as DUMP_STATE writes them. At every digest it takes, every byte and word of the state must hold
/// the value the dump before the change gives it, by its path, wherever each tree lays it out; and the picture and
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
    StateLeaves leaves;
    _state.Data().Visit(leaves);
    const std::filesystem::path file = m_directory / _kind / _name / (std::to_string(_point) + ".txt");
    if (m_mode == DigestMode::Dump)
    {
      std::filesystem::create_directories(file.parent_path());
      std::ofstream stream(file, std::ios::binary | std::ios::trunc);
      for (const auto& [path, value] : leaves.Values())
      {
        stream << std::format("{} {:X}\n", path, value);
      }
      stream.close();
      Assert::IsFalse(stream.fail(), AnswerFile::Widen(file.string() + " cannot be written").c_str());
      return;
    }
    if (m_mode != DigestMode::Rebase)
    {
      return;
    }
    std::ifstream stream(file, std::ios::binary);
    Assert::IsTrue(static_cast<bool>(stream), AnswerFile::Widen(file.string() + ": the dump before the change").c_str());
    std::map<std::string, std::uint32_t> before;
    std::string path;
    std::string value;
    while (stream >> path >> value)
    {
      before[path] = static_cast<std::uint32_t>(std::stoul(value, nullptr, 16));
    }
    for (const auto& [where, now] : leaves.Values())
    {
      const auto was = before.find(where);
      if (was == before.end() || was->second != now)
      {
        Assert::Fail(AnswerFile::Widen(std::format("{} {} at {}: {} is {:X}, and was {}", _kind, _name, _point, where, now,
                                                   was == before.end() ? std::string("not in the dump") : std::format("{:X}", was->second)))
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
    }
  }

  DigestMode m_mode = DigestMode::Check;
  std::filesystem::path m_directory;
  std::string m_error;
};

} // namespace GameLogicTests
