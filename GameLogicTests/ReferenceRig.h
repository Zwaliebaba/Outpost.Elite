// GameLogicTests/ReferenceRig.h
#pragma once

#include "DirectoryFileStore.h"
#include "Game.h"
#include "Pc.h"
#include "Reference.h"

#include <memory>
#include <string_view>
#include <system_error>

namespace GameLogicTests
{

/// A directory under the system's temporary directory, emptied when made and removed when done.
class ScratchDirectory
{
public:
  explicit ScratchDirectory(std::string_view _name)
    : m_path(std::filesystem::temp_directory_path() / ("OutpostEliteGameLogicTests-" + std::string(_name)))
  {
    std::error_code error;
    std::filesystem::remove_all(m_path, error);
    std::filesystem::create_directories(m_path, error);
  }

  ScratchDirectory(const ScratchDirectory&) = delete;
  ScratchDirectory& operator=(const ScratchDirectory&) = delete;

  ~ScratchDirectory()
  {
    try
    {
      std::error_code error;
      std::filesystem::remove_all(m_path, error);
    }
    catch (...)
    {
      // Left behind; the next run removes it before it starts.
      return;
    }
  }

  [[nodiscard]] const std::filesystem::path& Path() const noexcept
  {
    return m_path;
  }

private:
  std::filesystem::path m_path;
};

/// The reference loaded on a PC in paced time (ADR-008), as replays run it and the game does: from its generated image
/// (D3), the PSP where DOSBox-X puts it, the clock starting at midnight on 1 January 1980 unless _startMoment says
/// otherwise, DOS's files in a scratch directory, and the native routines in place of the program's code on a
/// Dispatcher, which interprets nothing (ADR-011). A Game holds the PC and the GameState that owns the game's memory
/// (D19). A load that fails fails the test that made the rig.
class ReferenceRig
{
public:
  explicit ReferenceRig(std::string_view _name, const Machine::Dos::DateTime& _startMoment = Elite::START_MOMENT,
                        bool _mousePresent = false)
    : m_directory(_name),
      m_files(m_directory.Path()),
      m_game(std::make_unique<Elite::Game>(m_files, Desc(_startMoment, _mousePresent)))
  {
    using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
    Assert::IsTrue(m_game->Load(Elite::PSP_SEGMENT), L"the reference loads from its generated image");
  }

  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_game->Host();
  }

  /// The game's memory, which the PC maps.
  [[nodiscard]] Elite::GameState& State() noexcept
  {
    return m_game->State();
  }

  [[nodiscard]] const Machine::LoadedProgram& Program() const noexcept
  {
    return m_game->Program();
  }

  /// Where DOS's files are.
  [[nodiscard]] const std::filesystem::path& Files() const noexcept
  {
    return m_directory.Path();
  }

  /// The store DOS keeps its files in, over Files(): what a replay's file steps copy into.
  [[nodiscard]] Machine::DirectoryFileStore& Store() noexcept
  {
    return m_files;
  }

private:
  [[nodiscard]] static Machine::Pc::Desc Desc(const Machine::Dos::DateTime& _startMoment, bool _mousePresent) noexcept
  {
    Machine::Pc::Desc desc;
    desc.startMoment = _startMoment;
    desc.mousePresent = _mousePresent;
    return desc;
  }

  ScratchDirectory m_directory;
  Machine::DirectoryFileStore m_files;
  std::unique_ptr<Elite::Game> m_game;
};

} // namespace GameLogicTests
