#pragma once

#include "EliteTypes.h"

#include <cstdint>

namespace Elite
{

/*
 * The zero-page scratch bytes the arithmetic routines share.
 *
 * In the original these are fixed addresses that every routine reads and writes by name, and
 * callers set some of them up before a call and read others afterwards. The port keeps that
 * shape rather than converting each routine to parameters and return values, because the
 * calling convention *is* part of the behaviour being verified: a routine that leaves P holding
 * the low byte is relied upon by its caller three files away.
 *
 * 6502: P, Q, R, S, T, T1, U.
 */
struct MathWorkspace
{
  // P is a three-byte block in the original, and the wider routines use all of it.
  std::uint8_t p = 0;
  std::uint8_t p1 = 0;
  std::uint8_t p2 = 0;

  std::uint8_t q = 0;
  std::uint8_t r = 0;
  std::uint8_t s = 0;
  std::uint8_t t = 0;
  std::uint8_t t1 = 0;
  std::uint8_t u = 0;

  // 6502: widget -- a scratch byte the logarithm routines use to hold their first operand
  // while the index register is busy addressing a table.
  std::uint8_t widget = 0;

  // 6502: K, a four-byte result block that several routines fill.
  std::uint8_t k[4] = { 0, 0, 0, 0 };
};

/// 6502: MU11 -- (A P) = P * X, unsigned. Returns the high byte; the low byte is left in P.
[[nodiscard]] std::uint8_t MultiplyByX(MathWorkspace& _work, std::uint8_t _x) noexcept;

/// 6502: MULTU -- (A P) = P * Q, unsigned. Returns the high byte, low byte left in P.
[[nodiscard]] std::uint8_t MultiplyUnsigned(MathWorkspace& _work) noexcept;

/// 6502: MLU2 -- (A P) = |A| * Q, unsigned. Returns the high byte, low byte left in P.
[[nodiscard]] std::uint8_t MultiplyMagnitudeByQ(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: MULT1 -- (A P) = Q * A for sign-magnitude operands. Returns the high byte, which
/// carries the sign; the low byte is left in P.
[[nodiscard]] std::uint8_t MultiplySigned(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: MULT12 -- (S R) = Q * A, sign-magnitude. The result is left in the workspace.
void MultiplySignedToSR(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: SQUA2 -- (A P) = A * A for an A already known to be positive.
[[nodiscard]] std::uint8_t SquareUnsigned(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: SQUA -- (A P) = |A| * |A|, clearing the sign bit first.
[[nodiscard]] std::uint8_t Square(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// What the sign-magnitude addition hands back: the original returns the high byte in A and
/// the low byte in X, and callers use both.
struct AddSignedResult
{
  std::uint8_t high = 0;
  std::uint8_t low = 0;
};

/// 6502: ADD (with its MU8 and MU9 branches) -- (A X) = (A P) + (S R), sign-magnitude.
[[nodiscard]] AddSignedResult AddSigned(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: MAD -- (A X) = Q * A + (S R). The multiply-accumulate the geometry code runs on.
[[nodiscard]] AddSignedResult MultiplyAndAdd(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: MU5 -- fills the four-byte K block with A. The original also clears carry; nothing
/// downstream reads that, so it is not modelled.
void FillK(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: MU6 -- sets both low bytes of P to A.
void SetPairP(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: MULTS -- (A P) = P * |A|, scaled: only five of the eight bits get an addition and the
/// remaining three are shifted through, which divides the result down. Used where one operand
/// is known to be small.
[[nodiscard]] std::uint8_t MultiplyScaled(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: MLTU2 -- (A P+1 P) = (~A P) * Q, sixteen steps through the complemented multiplier.
[[nodiscard]] std::uint8_t MultiplyWide(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: DVID96 -- A = A / 96, keeping the sign bit. The tail TIS1 shares.
[[nodiscard]] std::uint8_t DivideBy96(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: TIS1 -- (A ?) = (-X * A + (S R)) / 96, sign-magnitude.
[[nodiscard]] std::uint8_t MultiplyAddDivide96(MathWorkspace& _work, std::uint8_t _a, std::uint8_t _x) noexcept;

/// 6502: TIS2 -- A = A / Q, sign-magnitude, saturating at 96 when the magnitude is too large.
[[nodiscard]] std::uint8_t DivideByQ(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: DVIDT -- (P+1 P) = (A P+1) / Q, sixteen-step long division. Returns the low byte with
/// the sign of the quotient applied.
[[nodiscard]] std::uint8_t DivideWide(MathWorkspace& _work, std::uint8_t _a) noexcept;

// ---- the logarithm-table routines -----------------------------------------------------
//
// Where shift-and-add is too slow, the game adds logarithms and looks the answer back up.
// These read the tables in LookupTables.h and are exact about which of the two inverse tables
// applies, because that choice falls out of a parity test rather than being a detail.

/// 6502: FMLTU -- A = A * Q / 256, through the logarithm tables.
[[nodiscard]] std::uint8_t MultiplyByLog(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: LL28 -- R = 256 * A / Q, saturating at 255 when A is not smaller than Q. Returns the
/// carry the routine leaves, because its callers branch on it.
[[nodiscard]] bool DivideToR(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: LL38 (with its LL39 and LL40 branches) -- combines Q and R under the signs in A and S,
/// flipping S when the result turns negative.
[[nodiscard]] std::uint8_t CombineSigned(MathWorkspace& _work, std::uint8_t _a) noexcept;

/// 6502: ARCTAN -- the angle of the ratio P over Q, as a byte turn.
[[nodiscard]] std::uint8_t Arctan(MathWorkspace& _work) noexcept;

/// 6502: FMLTU2 -- A = K * sin(A) / 256, where the sine comes from SNE indexed by the low five
/// bits of A. It sets Q and falls straight through into FMLTU, so this is that whole path.
[[nodiscard]] std::uint8_t MultiplyKBySine(MathWorkspace& _work, std::uint8_t _a) noexcept;

/*
 * 6502: DVID4 -- an 8.8 fixed-point divide. P comes out as the whole part of A / Q, and R as
 * the fraction: eight steps of restoring division, then -- because the routine has no RTS of
 * its own -- LL28's body scaling the remainder back up, which leaves R.
 *
 * Both halves are the routine. Its two callers in the shipped game JSR to the top and return
 * from the bottom of the code it falls into, so a port that stopped after the division would be
 * a different routine that happens to share a name. Returns R.
 *
 * The shipped C64 build unrolls the eight steps rather than looping; that changes nothing about
 * the result, which is why this reads as a loop.
 */
[[nodiscard]] std::uint8_t DivideAndScale(MathWorkspace& _work, std::uint8_t _a) noexcept;

/*
 * 6502: LL5 -- Q = square root of (R Q), by the schoolbook bitwise method.
 *
 * Eight rounds, each shifting two more bits of the radicand in and testing whether the next
 * candidate bit fits. The comparison is spread across three registers with a borrow threaded
 * between them, which is why this is ported as flags rather than as arithmetic.
 *
 * The inventory grouped this with the state-dependent helpers and deferred it to 3a. It is not
 * state-dependent -- it takes R and Q and leaves Q -- and TT111 needs it, so it lands here.
 */
void SquareRoot(MathWorkspace& _work) noexcept;

} // namespace Elite
