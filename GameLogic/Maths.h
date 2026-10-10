// GameLogic/Maths.h
#pragma once

#include "NativeEntry.h"

#include <cstdint>
#include <span>

namespace Elite
{

// The reference's arithmetic, ported (plan §5 Phase 3, ADR-010). Each function is the body of the
// routine Symbols.tsv names, on the registers its contract gives: it neither expects a return address
// on the stack nor pops one, so native code calls it directly, and MathsEntries hooks it.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> MathsEntries() noexcept;

/// DivideOverflowInterrupt (CS:025E), the int 0 handler: saves BX and DS at CS:02A1/02A3 and leaves
/// AL = 7Fh or AX = 7FFFh, the remainder untouched, as bit 0 of the opcode two bytes before the return
/// address says. An 80286 pushes the divide's own address, so it steps the return address over a
/// DIV or IDIV it finds there, assuming a register operand.
void DivideOverflowInterrupt(Guest& _guest);

/// DIV r/m16 by _divisor, in native code: DX:AX / _divisor to AX, the remainder to DX, or, when the
/// quotient does not fit, what DivideOverflowInterrupt does for the divide whose next instruction is at
/// CS:_returnOffset. That instruction must not itself be a divide.
void DivideUnsigned(Guest& _guest, std::uint16_t _divisor, std::uint16_t _returnOffset);

/// DIV r/m8 by _divisor, in native code: AX / _divisor to AL and the remainder to AH, or, when the
/// quotient does not fit, what DivideOverflowInterrupt does for a byte divide: AL = 7Fh, AH as it was.
/// The handler reads every byte divide in the program as one.
void DivideByte(Guest& _guest, std::uint8_t _divisor);

/// DIV r/m16 by _divisor: DX:AX / _divisor to AX and the remainder to DX, or AX = 7FFFh and DX as it
/// was. The handler reads every word divide in the program as one but ProjectVertices' two, at
/// CS:2369 and CS:2392, which DivideUnsigned serves.
void DivideWord(Guest& _guest, std::uint16_t _divisor);

/// IDIV r/m16 by _divisor, as the 8088 does it: the magnitudes divided, and the trap as DivideWord's
/// when the quotient's magnitude reaches the sign bit, so that -32768 traps too.
void DivideSignedWord(Guest& _guest, std::uint16_t _divisor);

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

/// VectorLength (CS:2E96): sqrt(AX^2 + BX^2 + CX^2) of signed words, from the top 16 significant bits of
/// the 32-bit sum, by subtracting odd numbers. Out: AX; BX, CX, DX clobbered.
void VectorLength(Guest& _guest);

/// ObjectWithinBox (CS:2F65): VectorWithinBox on the low words of slot DI's position.
void ObjectWithinBox(Guest& _guest);

/// VectorWithinBox (CS:2F6E): CF set if |AX|, |BX| and |CX| are all below DX, stopping at the first
/// that is not. AX, BX, CX clobbered.
void VectorWithinBox(Guest& _guest);

/// RotatePitchYawRoll (CS:3EAC): (y, z) by rotationSinCos[0], (x, z) by [1], (x, y) by [2]. In and
/// out: AX, BX, CX = x, y, z; DX is left as the last RotateBySinCos leaves it.
void RotatePitchYawRoll(Guest& _guest);

/// RotateBySinCos7210 (CS:3F02): (y, z) by rotationSinCos[7], then (x, y) by [2], (x, z) by [1] and
/// (y, z) by [0]. In and out: AX, BX, CX = x, y, z; DX as the last RotateBySinCos leaves it.
void RotateBySinCos7210(Guest& _guest);

/// ScaleByInverseDistance (CS:40A4): DX:AX shifted right by slot DI's scale shift (+0Ah), over 256
/// times one more than the square root of the high word of the view position's squared length. Out:
/// AX at most 255, BX the divisor; CX, DX clobbered.
void ScaleByInverseDistance(Guest& _guest);

/// ShiftRight24 (CS:4326): DL:AX shifted arithmetically right DH times. Out: DL:AX, DH = 0.
void ShiftRight24(Guest& _guest);

/// GetPositionScaleShift (CS:4333): the shift that brings the largest magnitude of slot DI's 24-bit
/// coordinates below 10000h and then below 24B8h. Out: CL, BX the shifted magnitude; AX, DX clobbered.
void GetPositionScaleShift(Guest& _guest);

/// ScalePositionDown (CS:439E): slot DI's 24-bit coordinates shifted right by DH. Out: AX, BX, CX = x,
/// y, z; DX, BP clobbered.
void ScalePositionDown(Guest& _guest);

/// ComputeAnglesToObject (CS:4ECF): the two angles of the direction of slot DI's position, by
/// ArcTangent2 and rotation slot 6. Out: AX, BX; CX, DX, BP clobbered.
void ComputeAnglesToObject(Guest& _guest);

/// ConvertVectorToAngles (CS:4F08): the two angles, negated, of the direction (AX, BX, CX) / 4, by
/// ArcTangent2 and rotation slot 8. Out: AX, BX; CX, DX, BP clobbered.
void ConvertVectorToAngles(Guest& _guest);

} // namespace Elite
