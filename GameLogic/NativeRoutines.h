// GameLogic/NativeRoutines.h
#pragma once

#include "Pc.h"

#include <iosfwd>

namespace Elite
{

/// Puts every routine ported so far in place of the reference's own (ADR-010): each entry the program
/// can reach from code still interpreted is hooked to its native body followed by the return the
/// original makes, with the contract Symbols.tsv gives it. Also tells the comparison where the
/// program's stack begins. _program is the reference as LoadReference loaded it into _pc.
void InstallNativeRoutines(Machine::Pc& _pc, const Machine::LoadedProgram& _program);

/// How many entries InstallNativeRoutines hooks.
[[nodiscard]] std::size_t NativeRoutineCount() noexcept;

/// Each hooked entry's books as Tools/RoutineCoverage.py reads them, one tab-separated row per entry
/// under a header: entry, routine, calls, verified, unverifiable, mismatches, and the offsets the
/// original ran in the calls that were compared.
void WriteNativeReport(const Machine::NativeCode& _native, std::ostream& _out);

} // namespace Elite
