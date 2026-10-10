// GameLogic/NativeRoutines.h
#pragma once

#include "Pc.h"

#include <cstddef>

namespace Elite
{

/// Puts every routine in place of the reference's own (ADR-010): each entry the program reaches is hooked to
/// its native body followed by the return the original makes, with the contract Symbols.tsv gives it.
/// _program is the reference as LoadReference loaded it into _pc.
void InstallNativeRoutines(Machine::Pc& _pc, const Machine::LoadedProgram& _program);

/// How many entries InstallNativeRoutines hooks.
[[nodiscard]] std::size_t NativeRoutineCount() noexcept;

} // namespace Elite
