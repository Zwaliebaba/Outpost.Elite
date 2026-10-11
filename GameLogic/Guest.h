// GameLogic/Guest.h
#pragma once

#include "GameState.h"
#include "Hardware.h"
#include "Platform.h"

#include <cstdint>

namespace Elite
{

/// What an entry of the program sees when the platform enters it (ADR-010): the game's memory and devices that the routine
/// behind the entry takes, and the little the 8088 handed the original there. One is made for each call of an entry, around
/// the GameState and the Platform the host made for the whole run (Game). Only the entries the platform reaches have one
/// (NativeRoutines.h): Start, where DOS starts the program, and the timer's and keyboard's interrupt handlers.
class Guest
{
public:
  Guest(Platform& _platform, GameState& _state) noexcept
    : m_state(_state),
      m_hardware(_platform)
  {
  }

  /// The game's memory: what a de-assembled routine is given (ADR-012).
  [[nodiscard]] GameState& State() noexcept
  {
    return m_state;
  }

  /// The devices as the game drives them: what a de-assembled routine that sounds, times or reads a device is given
  /// besides the GameState (ADR-014).
  [[nodiscard]] Hardware& Devices() noexcept
  {
    return m_hardware;
  }

  /// BP as DOS leaves it for the program's start (Platform::ENTRY_BASE_POINTER).
  [[nodiscard]] static constexpr std::uint16_t EntryBasePointer() noexcept
  {
    return Platform::ENTRY_BASE_POINTER;
  }

private:
  GameState& m_state;
  Hardware m_hardware;
};

} // namespace Elite
