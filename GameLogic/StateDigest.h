// GameLogic/StateDigest.h
#pragma once

#include <string>

namespace Machine
{
class Pc;
struct LoadedProgram;
} // namespace Machine

namespace Elite
{

/// The SHA-256, as lowercase hex, of the game's state (ADR-008): what replays check and what Phase 3
/// must leave unchanged. In this order:
///
///  1. the data segment, all 64 KiB of it but the stack (STACK_FIRST_OFFSET up to STACK_END_OFFSET);
///  2. the CGA's 16 KiB of video memory at B800:0000;
///  3. the CGA's mode control and colour select registers and its 18 CRTC registers.
///
/// Left out on purpose: the registers, because a native routine has no instruction pointer to agree on;
/// the code segment, because the triangle filler rewrites its own instructions and a native filler does
/// not; the stack, which holds whatever the last calls and interrupts left below SP; and DOS's and the
/// BIOS's own memory, which the game does not read.
[[nodiscard]] std::string GameStateDigest(const Machine::Pc& _pc, const Machine::LoadedProgram& _program);

} // namespace Elite
