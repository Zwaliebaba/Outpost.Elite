// GameLogic/Game.h
#pragma once

#include "GameState.h"
#include "Pc.h"
#include "Platform.h"
#include "Reference.h"

#include <cstdint>

namespace Elite
{

/// The game as a host runs it (D19): the GameState, which owns the game's memory, the native platform, whose devices replace
/// the emulated PC's one at a time (D27), and the PC whose devices, services and native routines run the reference over them
/// (ADR-006, ADR-011). Every host and rig makes one: the shell, ReferenceRunner and the tests' ReferenceRig.
///
/// The PC maps the GameState's three regions into its memory, and its hooks hold the GameState and the platform, so the PC
/// must not outlive either. That holds by construction: only a Game maps a GameState, and the members are built in the order
/// GameState, Platform, Pc and destroyed in reverse, the Pc first. The platform reads and writes the game's memory through the
/// GameState, so it comes after it. A Game is over 100 KiB, the GameState's storage, so a host makes it on the heap.
class Game
{
public:
  /// Powers on the PC (Machine::Pc) and the native platform, the files of both in _files, which must outlive the game: the
  /// platform's DOS file services and the PC's DOS are given the same store. Nothing is loaded. Not noexcept: the devices
  /// allocate.
  Game(Machine::FileStore& _files, const Machine::Pc::Desc& _desc);
  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;

  /// Loads the reference once, as LoadReference does: from its generated image (D3), as DOS would with the PSP at _pspSegment,
  /// and the D5 byte written. Then the GameState takes over the code segment, the data segment and video memory, at the
  /// segments the load put the program at, with the bytes it wrote there, the platform starts the program as DOS does, and the
  /// native routines go in place of the original's (InstallNativeRoutines). False on failure, and then nothing is mapped and
  /// nothing is started.
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

  /// The native platform: the devices that have replaced the emulated PC's (D27).
  [[nodiscard]] Platform& NativePlatform() noexcept
  {
    return m_platform;
  }

  /// Where Load put the reference.
  [[nodiscard]] const Machine::LoadedProgram& Program() const noexcept
  {
    return m_program;
  }

private:
  // In this order: the PC maps the GameState's storage and its hooks reach the platform, so it is built after both and
  // destroyed before them; the platform writes through the GameState, so it is built after it and destroyed before it.
  GameState m_state;
  Platform m_platform;
  Machine::Pc m_pc;
  Machine::LoadedProgram m_program;
};

} // namespace Elite
