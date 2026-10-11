// GameLogic/Game.h
#pragma once

#include "GameState.h"
#include "Platform.h"
#include "Reference.h"

#include <cstdint>

namespace Elite
{

/// The game as a host runs it (D19, D33): the GameState, which owns the game's memory, and the native platform, whose devices
/// and services run the game over it. Every host and rig makes one: the shell, ReferenceRunner and the tests' ReferenceRig.
///
/// The platform reads and writes the game's memory through the GameState, and the native routines it runs hold both, so the
/// platform must not outlive the GameState. That holds by construction: the members are built in the order GameState,
/// Platform and destroyed in reverse, the platform first, and its native thread with it. A Game is over 100 KiB, the
/// GameState's storage, so a host makes it on the heap.
class Game
{
public:
  /// Powers on the platform, DOS's files in _files, which must outlive the game. Nothing is loaded. Not noexcept: the devices
  /// allocate.
  Game(FileStore& _files, const Platform::Desc& _desc);
  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  /// Loads the reference once, as DOS would with the PSP at _pspSegment: its generated image (D3) straight into the GameState,
  /// its code segment at the PSP plus 10h, with its relocations, the D5 byte and its stored addresses as what they mean
  /// (ReferenceLoadImage). Then the platform starts
  /// the program as DOS does, and the native routines go where the original's entries are (InstallNativeRoutines). False on
  /// failure, and then nothing is loaded and nothing is started.
  [[nodiscard]] bool Load(std::uint16_t _pspSegment);

  /// The platform the game runs on: its clock, its devices and its picture.
  [[nodiscard]] Platform& Host() noexcept
  {
    return m_platform;
  }

  [[nodiscard]] const Platform& Host() const noexcept
  {
    return m_platform;
  }

  /// The game's memory, once Load has loaded it.
  [[nodiscard]] GameState& State() noexcept
  {
    return m_state;
  }

  [[nodiscard]] const GameState& State() const noexcept
  {
    return m_state;
  }

private:
  // In this order: the platform reads and writes the GameState, so it is built after it and destroyed before it.
  GameState m_state;
  Platform m_platform;
};

} // namespace Elite
