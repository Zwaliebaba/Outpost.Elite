// GameLogic/NativeEntry.h
#pragma once

#include "Guest.h"

#include <cstdint>

namespace Elite
{

/// What an entry the platform reaches is (ADR-041), besides the program's start (StartEntry): the game's handler of an interrupt,
/// which leaves by IRET.
enum class EntryKind : std::uint8_t
{
  TimerHandler,
  KeyboardHandler
};

/// One entry of the reference that the platform reaches (ADR-010): what it is, and the native body. Each subsystem's file lists
/// its own, and InstallNativeRoutines hands them all to the platform.
struct NativeEntry
{
  EntryKind kind;
  void (*body)(Guest&);
};

} // namespace Elite
