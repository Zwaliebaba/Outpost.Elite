// GameLogic/StateDigest.h
#pragma once

#include <string>

namespace Machine
{
class Cga;
} // namespace Machine

namespace Elite
{

class GameState;

/// The SHA-256, as lowercase hex, of the game's state (ADR-008): what replays check and what Phase 3
/// must leave unchanged. In this order:
///
///  1. the data segment, all 64 KiB of it but the stack (STACK_FIRST_OFFSET up to STACK_END_OFFSET);
///  2. the CGA's 16 KiB of video memory at B800:0000;
///  3. the CGA's mode control and colour select registers and its 18 CRTC registers.
///
/// The first two are _state's own bytes (D19), the third _video's.
///
/// Left out on purpose: the registers, because a native routine has no instruction pointer to agree on;
/// the code segment, where the triangle filler patches its step instructions as the original rewrites
/// its own (PatchStep), because nothing reads a patched site back: the native filler steps each edge by
/// its sign; the stack range, which holds whatever the last calls and interrupts left below SP, and at
/// its foot triangleEdgeMasks (DS:AD60-AD6F), constant data; and DOS's and the BIOS's own memory, which
/// the game does not read.
[[nodiscard]] std::string GameStateDigest(const GameState& _state, const Machine::Cga& _video);

} // namespace Elite
