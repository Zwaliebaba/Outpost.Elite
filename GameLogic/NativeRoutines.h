// GameLogic/NativeRoutines.h
#pragma once

#include "Pc.h"

namespace Elite
{

/// Puts the native game in place of the reference's own code (ADR-010): each entry the Dispatcher can reach is hooked to its
/// native body followed by the return the original makes. Those are Start, where DOS starts the program, and the timer's and
/// the keyboard's interrupt handlers, which int 8 and int 9 name; every other routine is called as a value from them (ADR-012).
/// _program is the reference as LoadReference loaded it into _pc.
void InstallNativeRoutines(Machine::Pc& _pc, const Machine::LoadedProgram& _program);

} // namespace Elite
