// GameLogic/SaveLoad.h
#pragma once

#include "GameState.h"
#include "Host.h"

#include <cstdint>

namespace Elite
{

// The reference's save-load routines, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): saving and loading commanders. Each
// takes values and gives values back, on the GameState and, where it drives a device, the devices (ADR-014).

// ── The routines de-assembled (ADR-012): values in, values out, on the GameState ──

/// diskOperation, what Start does on D10's behalf (ShowDiscScreen): leave the game (D80), or load, save or delete
/// commanderFileName.
inline constexpr std::uint8_t DISK_QUIT = 0;
inline constexpr std::uint8_t DISK_LOAD = 1;
inline constexpr std::uint8_t DISK_SAVE = 2;
inline constexpr std::uint8_t DISK_DELETE = 3;

/// PerformDiskRequest: _request DISK_LOAD loads commanderFileName into commanderBlock, DISK_SAVE saves it and DISK_DELETE
/// deletes it, each through the platform's FileStore (D78); anything else does nothing, as the catalogue is D10's own read
/// (ADR-067). diskError=1 on a failure.
void PerformDiskRequest(GameState& _state, Host& _host, std::uint8_t _request);

/// SaveStartupCommander: commanderFileBytes of commanderBlock copied to startupCommander, a byte at a time, going up as
/// the original's copy does: nothing leaves it running downward where the game calls it (change 3c).
void SaveStartupCommander(GameState& _state);

} // namespace Elite
