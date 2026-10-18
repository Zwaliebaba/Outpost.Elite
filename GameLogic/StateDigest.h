// GameLogic/StateDigest.h
#pragma once

#include <string>

namespace Elite
{

class Display;
class GameState;

/// The SHA-256, as lowercase hex, of the game's state (ADR-008): what replays check. In this order:
///
///  1. the game's state, every member GameData holds, written by its members in their order (StateWriter, D76), not by
///     how the struct lays them out;
///  2. the display's colour select, the one register the game still sets, a byte (ADR-069).
///
/// Left out on purpose: the code segment, where the triangle filler patches its step instructions as the original rewrites
/// its own (PatchStep), because nothing reads a patched site back: the native filler steps each edge by its sign; the
/// constants and gaps nothing uses, none of which GameData holds (D38); and DOS's and the BIOS's own memory, which the game
/// does not read.
[[nodiscard]] std::string GameStateDigest(const GameState& _state, const Display& _display);

} // namespace Elite
