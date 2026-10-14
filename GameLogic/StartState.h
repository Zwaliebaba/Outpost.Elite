// GameLogic/StartState.h
#pragma once

#include "GameData.h"

namespace Elite
{

/// The game's state as the program starts (D76, Phase 7 step 4): Jameson's commander, docked at Lave in the first galaxy, with
/// the texts and buffers the game prints from, the dashboard's caches marked as never shown, the messages and settings the
/// game starts with, and every member it does not set 0. _data must be all 0 when it is called.
void SetStartState(GameData& _data) noexcept;

} // namespace Elite
