// GameLogic/Maths.h
#pragma once

#include "GameState.h"
#include "NativeEntry.h"
#include "ObjectSlot.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Elite
{

// The reference's arithmetic, ported (plan §5 Phase 3, ADR-010) and de-assembled (ADR-012): the routines
// take values and give values back, and their entries, below, keep the register contracts the hooks and
// the callers not yet de-assembled use. The divides that go through the game's trap are value routines
// too.

/// The entries of this subsystem ported so far, for InstallNativeRoutines.
[[nodiscard]] std::span<const NativeEntry> MathsEntries() noexcept;

// ── The routines (ADR-012): values in, values out, on the GameState ──
//
// Each is what the routine Symbols.tsv names computes, with no register in sight: its inputs are
// parameters, its results come back, and every byte it writes is written as the original writes it,
// in the same order, scratch words and the divide trap's saves in the code segment included. Signed
// words are std::int16_t; angles, 2048 to a turn, are std::uint16_t.

/// The two halves of a rotation, as sineTable holds them: 4000h is 1.0.
struct SinCos
{
  std::int16_t sine;
  std::int16_t cosine;
};

/// The two coordinates a rotation turns, in the order the original's AX and BX hold them.
struct Pair
{
  std::int16_t first;
  std::int16_t second;
};

/// A point or a direction in space.
struct Vector
{
  std::int16_t x;
  std::int16_t y;
  std::int16_t z;
};

/// The two angles of a direction, as ComputeAnglesToObject and ConvertVectorToAngles give them.
struct Angles
{
  std::uint16_t first;
  std::uint16_t second;
};

/// What AngleWithinTolerance finds.
struct AngleTolerance
{
  bool within;          ///< the angles differ by less than the tolerance
  std::uint16_t excess; ///< how much more than the tolerance they differ by, as a word
};

/// ScaleByInverseDistance's result and the divisor it took.
struct InverseDistanceScale
{
  std::uint16_t scaled;  ///< at most 255
  std::uint16_t divisor; ///< 256 times one more than the root of the high word of the view position's squared length
};

/// GetPositionScaleShift's result.
struct PositionScale
{
  std::uint8_t shift;      ///< what brings the largest coordinate below 10000h and then below 24B8h
  std::uint16_t magnitude; ///< that coordinate's magnitude, so shifted
};

/// What DivideOverflowInterrupt leaves for the divide that trapped.
struct DivideTrap
{
  std::uint16_t resume; ///< the offset its IRET returns to
  std::uint16_t ax;     ///< AL = 7Fh over the AX the divide left, or AX = 7FFFh
};

/// What a byte divide leaves: AL and AH.
struct ByteQuotient
{
  std::uint8_t quotient;  ///< 7Fh when the divide traps
  std::uint8_t remainder; ///< the dividend's high byte, as it was, when the divide traps
};

/// What a word divide leaves: AX and DX.
struct WordQuotient
{
  std::uint16_t quotient;  ///< what DivideOverflowInterrupt leaves in AX when the divide traps
  std::uint16_t remainder; ///< the dividend's high word, as it was, when the divide traps
};

/// DivideOverflowInterrupt (CS:025E), the int 0 handler, for the divide whose return address is _segment:_offset, with AX
/// = _ax and BX = _bx there: BX and the data segment saved at CS:02A1/02A3, then AL = 7Fh or AX = 7FFFh, the remainder
/// untouched, as bit 0 of the opcode two bytes before the return address says. An 80286 pushes the divide's own address,
/// so it steps the return address over a DIV or IDIV it finds there, assuming a register operand.
[[nodiscard]] DivideTrap DivideOverflowInterrupt(GameState& _state, std::uint16_t _segment, std::uint16_t _offset, std::uint16_t _ax,
                                                 std::uint16_t _bx);

/// DIV r/m16: _dividend / _divisor, or, when the quotient does not fit, what DivideOverflowInterrupt does for the divide
/// whose next instruction is at CS:_returnOffset, with BX = _bx. That instruction must not itself be a divide.
[[nodiscard]] WordQuotient DivideUnsigned(GameState& _state, std::uint32_t _dividend, std::uint16_t _divisor, std::uint16_t _returnOffset,
                                          std::uint16_t _bx);

/// DIV r/m8: _dividend / _divisor, or, when the quotient does not fit, what DivideOverflowInterrupt does for a byte
/// divide with BX = _bx: AL = 7Fh, AH as it was. The handler reads every byte divide in the program as one.
[[nodiscard]] ByteQuotient DivideByte(GameState& _state, std::uint16_t _dividend, std::uint8_t _divisor, std::uint16_t _bx);

/// DIV r/m16: _dividend / _divisor, or AX = 7FFFh and DX as it was, the trap's saves made with BX = _bx. The handler reads
/// every word divide in the program as one but ProjectVertices' two, at CS:2369 and CS:2392, which DivideUnsigned serves.
[[nodiscard]] WordQuotient DivideWord(GameState& _state, std::uint32_t _dividend, std::uint16_t _divisor, std::uint16_t _bx);

/// IDIV r/m16, as the 8088 does it: the magnitudes divided, and the trap as DivideWord's when the quotient's magnitude
/// reaches the sign bit, so that -32768 traps too.
[[nodiscard]] WordQuotient DivideSignedWord(GameState& _state, std::uint32_t _dividend, std::uint16_t _divisor, std::uint16_t _bx);

/// NextRandom (CS:061C): the lagged-Fibonacci step on randomState0-2, and the number it makes.
[[nodiscard]] std::uint16_t NextRandom(GameState& _state);

/// sin and cos of _angle, from sineTable.
[[nodiscard]] SinCos SinCosOf(const GameState& _state, std::uint16_t _angle);

/// SetSinCos (CS:23B8): sin and cos of _angle, stored as rotationSinCos[_pair].
SinCos SetSinCos(GameState& _state, std::size_t _pair, std::uint16_t _angle);

/// RotateBySinCos (CS:23DB): _point turned by _by, each product doubled and rounded on the bit below.
[[nodiscard]] Pair RotateBySinCos(Pair _point, SinCos _by);

/// RotateByStoredSinCos (CS:2459): RotateBySinCos by rotationSinCos[_pair].
[[nodiscard]] Pair RotateByStoredSinCos(const GameState& _state, std::size_t _pair, Pair _point);

/// ArcTangent2 (CS:24A5): the angle of (_a, _b), 0 along +_b and 200h along +_a.
[[nodiscard]] std::uint16_t ArcTangent2(GameState& _state, std::int16_t _a, std::int16_t _b);

/// QuadrantArcTangent (CS:24E5): atan(_b/_a) for magnitudes, 0-200h.
[[nodiscard]] std::uint16_t QuadrantArcTangent(GameState& _state, std::uint16_t _a, std::uint16_t _b);

/// RatioArcTangent (CS:24F7): atan(_numerator/_denominator) for _numerator <= _denominator, 0-100h, by
/// a search of tangentTable. A divide that overflows goes through the trap, which saves _denominator,
/// the BX of its DIV, and the data segment in the code segment.
[[nodiscard]] std::uint16_t RatioArcTangent(GameState& _state, std::uint16_t _numerator, std::uint16_t _denominator);

/// AngleWithinTolerance (CS:2CDB): whether 11-bit angles _a and _b differ by less than _tolerance.
[[nodiscard]] AngleTolerance AngleWithinTolerance(std::uint16_t _a, std::uint16_t _b, std::uint16_t _tolerance);

/// VectorLength (CS:2E96): the length of _vector from the top 16 significant bits of its squared length,
/// by subtracting odd numbers.
[[nodiscard]] std::uint16_t VectorLength(Vector _vector);

/// VectorWithinBox (CS:2F6E): whether |x|, |y| and |z| are all below _halfSize.
[[nodiscard]] bool VectorWithinBox(Vector _vector, std::uint16_t _halfSize);

/// ObjectWithinBox (CS:2F65): VectorWithinBox on the low words of _slot's position.
[[nodiscard]] bool ObjectWithinBox(const ObjectSlot& _slot, std::uint16_t _halfSize);

/// RotatePitchYawRoll (CS:3EAC): (y, z) by rotationSinCos[0], (x, z) by [1], (x, y) by [2]. It keeps the
/// rotated y, then z, in rotateScratch on the way, as the original does.
[[nodiscard]] Vector RotatePitchYawRoll(GameState& _state, Vector _vector);

/// RotateRollYawPitch (CS:3EC7): (x, y) by rotationSinCos[2], (x, z) by [1], (y, z) by [0].
[[nodiscard]] Vector RotateRollYawPitch(const GameState& _state, Vector _vector);

/// RotateBySinCos7210 (CS:3F02): (y, z) by rotationSinCos[7], then (x, y) by [2], (x, z) by [1] and
/// (y, z) by [0], keeping z, y and then x in rotateScratch on the way.
[[nodiscard]] Vector RotateBySinCos7210(GameState& _state, Vector _vector);

/// ScaleByInverseDistance (CS:40A4): _value shifted right by _slot's disc scale, over 256 times one more
/// than the square root of the high word of the view position's squared length, at most 255.
[[nodiscard]] InverseDistanceScale ScaleByInverseDistance(GameState& _state, const ObjectSlot& _slot, std::uint32_t _value);

/// ShiftRight24 (CS:4326): the 24-bit _value shifted arithmetically right _count times, in 24 bits.
[[nodiscard]] std::uint32_t ShiftRight24(std::uint32_t _value, std::uint8_t _count);

/// GetPositionScaleShift (CS:4333): the shift for _slot's position.
[[nodiscard]] PositionScale GetPositionScaleShift(const ObjectSlot& _slot);

/// ScalePositionDown (CS:439E): _slot's 24-bit coordinates shifted right by _shift, their low words.
[[nodiscard]] Vector ScalePositionDown(const ObjectSlot& _slot, std::uint8_t _shift);

/// ComputeAnglesToObject (CS:4ECF): the two angles of the direction of _slot's position, by ArcTangent2 and
/// rotationSinCos[6], which it sets.
[[nodiscard]] Angles ComputeAnglesToObject(GameState& _state, const ObjectSlot& _slot);

/// ConvertVectorToAngles (CS:4F08): the two angles, negated, of the direction _vector / 4, by ArcTangent2
/// and rotationSinCos[8], which it sets.
[[nodiscard]] Angles ConvertVectorToAngles(GameState& _state, Vector _vector);

// ── Their entries: the register contracts, for the hooks and for callers not yet converted ──
//
// Each reads its routine's inputs from the registers Symbols.tsv's contract names, calls it, and writes
// its results back there. The registers the contract leaves to the routine it hands to Guest::Clobber,
// unless a caller reads what the original leaves in one: then the entry leaves that, and the contract
// compares it (VectorWithinBoxEntry, and BP of the two angle entries).

void DivideOverflowInterruptEntry(Guest& _guest); ///< The return address on the stack, IP then CS; DS the divide's.
void NextRandomEntry(Guest& _guest);
void SetSinCosEntry(Guest& _guest, std::uint16_t _pairOffset); ///< BX = _pairOffset, an entry of rotationSinCos
void RotateBySinCosEntry(Guest& _guest);
void RotateByStoredSinCosEntry(Guest& _guest, std::uint16_t _pairOffset);
void ArcTangent2Entry(Guest& _guest);
void QuadrantArcTangentEntry(Guest& _guest);
void RatioArcTangentEntry(Guest& _guest);
void AngleWithinToleranceEntry(Guest& _guest);
void VectorLengthEntry(Guest& _guest);
void ObjectWithinBoxEntry(Guest& _guest);
void VectorWithinBoxEntry(Guest& _guest);
void RotatePitchYawRollEntry(Guest& _guest);
void RotateRollYawPitchEntry(Guest& _guest);
void RotateBySinCos7210Entry(Guest& _guest);
void ScaleByInverseDistanceEntry(Guest& _guest);
void ShiftRight24Entry(Guest& _guest);
void GetPositionScaleShiftEntry(Guest& _guest);
void ScalePositionDownEntry(Guest& _guest);
void ComputeAnglesToObjectEntry(Guest& _guest);
void ConvertVectorToAnglesEntry(Guest& _guest);

} // namespace Elite
