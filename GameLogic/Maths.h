// GameLogic/Maths.h
#pragma once

#include "Guest.h"

#include <cstdint>

namespace Elite
{

// The reference's arithmetic, ported (plan §5 Phase 3, ADR-010). Each function is the body of the
// routine Symbols.tsv names, on the registers its contract gives: it neither expects a return address
// on the stack nor pops one, so native code calls it directly and NativeRoutines.cpp hooks it.

/// NextRandom (CS:061C): the lagged-Fibonacci step on randomState0-2. Out: AX.
void NextRandom(Guest& _guest);

/// SetSinCos (CS:23B8) with BX = _pair: stores sin and cos of AX (2048 per turn) at DS:_pair. Out: AX
/// sin, BX cos.
void SetSinCos(Guest& _guest, std::uint16_t _pair);

/// RotateBySinCos (CS:23DB): rotates (AX, BX) by DI = sin and BP = cos (0x4000 = 1.0), rounding. Out:
/// AX, BX; DX and BP clobbered.
void RotateBySinCos(Guest& _guest);

/// RotateByStoredSinCos (CS:2459) with BX = _pair: RotateBySinCos by the pair at DS:_pair, keeping DI
/// and BP. Out: AX, BX; DX clobbered.
void RotateByStoredSinCos(Guest& _guest, std::uint16_t _pair);

/// ArcTangent2 (CS:24A5): the angle of (AX, BX), 0 along +BX and 0x200 along +AX. Out: AX; BX, CX, DX
/// clobbered.
void ArcTangent2(Guest& _guest);

/// QuadrantArcTangent (CS:24E5): atan(BX/AX) for AX, BX >= 0, 0-0x200. Out: AX; BX, CX, DX clobbered.
void QuadrantArcTangent(Guest& _guest);

/// RatioArcTangent (CS:24F7): atan(AX/BX) for AX <= BX, 0-0x100, by a search of tangentTable. Out: AX;
/// BX, CX, DX clobbered.
void RatioArcTangent(Guest& _guest);

/// AngleWithinTolerance (CS:2CDB): DX = |AX-CX| as 11-bit angles, less BX; CF set if within. CX comes
/// back sign-extended.
void AngleWithinTolerance(Guest& _guest);

} // namespace Elite
