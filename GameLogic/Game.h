// GameLogic/Game.h
#pragma once

#include "GameState.h"
#include "Host.h"

#include <cstdint>

namespace Elite
{

/// The game as a host runs it (D19, D33, D90): the GameState, which owns the game's memory, and the Host, whose clock and
/// devices run the game over it. Every host and rig makes one: the shell and the tests' GameRig.
///
/// The Host reads and writes the game's memory through the GameState, and the routines it runs hold both, so the Host must not
/// outlive the GameState. That holds by construction: the members are built in the order GameState, Host and destroyed in
/// reverse, the Host first, and the program's coroutine with it. A Game is over 100 KiB, the
/// GameState's storage, so a host makes it on the heap.
class Game
{
public:
  /// Powers on the Host, the commander files in _files, which must outlive the game. Nothing is loaded. Not noexcept: the devices
  /// allocate.
  Game(FileStore& _files, const Host::Desc& _desc);
  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  /// Starts the game once: the GameState as the program starts (SetStartState). The Host's program is then Start, and its tick's and keys' handlers TimerInterrupt
  /// and KeyboardInterrupt, which run once the game installs them.
  void Start();

  /// The Host the game runs on: its clock, its devices and its picture.
  [[nodiscard]] Elite::Host& Host() noexcept
  {
    return m_host;
  }

  /// The game's memory, once Load has loaded it.
  [[nodiscard]] GameState& State() noexcept
  {
    return m_state;
  }

private:
  // In this order: the Host reads and writes the GameState, so it is built after it and destroyed before it.
  GameState m_state;
  Elite::Host m_host;
};

} // namespace Elite
