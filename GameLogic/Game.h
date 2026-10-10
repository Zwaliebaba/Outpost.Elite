// GameLogic/Game.h
#pragma once

#include "GameState.h"
#include "Pc.h"
#include "Reference.h"

#include <cstdint>

namespace Elite
{

/// The game as a host runs it (D19): the GameState, which owns the game's memory, and the PC whose devices, services and
/// native routines run the reference over it (ADR-006, ADR-011). Every host and rig makes one: the shell, ReferenceRunner
/// and the tests' ReferenceRig.
///
/// The PC maps the GameState's three regions into its memory and its hooks hold the GameState, so the PC must not outlive it.
/// That holds by construction: only a Game maps a GameState, and the GameState is built first and destroyed last. A Game is
/// over 100 KiB, the GameState's storage, so a host makes it on the heap.
class Game
{
public:
  /// Powers on the PC (Machine::Pc), its DOS keeping its files in _files, which must outlive the game. Nothing is loaded.
  /// Not noexcept: the devices allocate.
  Game(Machine::FileStore& _files, const Machine::Pc::Desc& _desc);
  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  /// Loads the reference once, as LoadReference does: from its generated image (D3), as DOS would with the PSP at _pspSegment,
  /// and the D5 byte written. Then the GameState takes over the code segment, the data segment and video memory, at the
  /// segments the load put the program at, with the bytes it wrote there, and the native routines go in place of the
  /// original's (InstallNativeRoutines). False on failure, and then nothing is mapped and nothing is started.
  [[nodiscard]] bool Load(std::uint16_t _pspSegment);

  /// The PC the game runs on: its clock, its devices and its picture.
  [[nodiscard]] Machine::Pc& Host() noexcept
  {
    return m_pc;
  }

  [[nodiscard]] const Machine::Pc& Host() const noexcept
  {
    return m_pc;
  }

  /// The game's memory, once Load has mapped it.
  [[nodiscard]] GameState& State() noexcept
  {
    return m_state;
  }

  [[nodiscard]] const GameState& State() const noexcept
  {
    return m_state;
  }

  /// Where Load put the reference.
  [[nodiscard]] const Machine::LoadedProgram& Program() const noexcept
  {
    return m_program;
  }

private:
  // In this order: the PC maps the GameState's storage, so it is built after the GameState and destroyed before it.
  GameState m_state;
  Machine::Pc m_pc;
  Machine::LoadedProgram m_program;
};

} // namespace Elite
