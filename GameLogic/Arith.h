#pragma once

#include "EliteTypes.h"

#include <cstdint>
#include <span>

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

  /*
   * 6502: XX(1 0) and YY(1 0) -- two sixteen-bit scratch values at zero page 93 and 95.
   *
   * They are here rather than with the stardust, which is where they were first put and where
   * only their FIRST caller lives (§6.45). `EDGES`, `WPLS` and three of `SUN`'s four parts read
   * and write the same two labels, and `SUNX` sits immediately after them at 97 -- so they are a
   * shared coordinate pair, not a workspace one routine owns. The two users are never live at
   * the same time, which is exactly why nothing would ever have failed.
   */
  std::uint8_t xx = 0;
  std::uint8_t xxNext = 0;
  std::uint8_t yy = 0;
  std::uint8_t yyNext = 0;

  // 6502: widget -- a scratch byte the logarithm routines use to hold their first operand
  // while the index register is busy addressing a table.
  std::uint8_t widget = 0;

  // 6502: K, a four-byte result block that several routines fill.
  std::uint8_t k[4] = { 0, 0, 0, 0 };

  /*
   * 6502: K2 -- a SECOND four-byte block, and separate storage rather than a second use of K.
   *
   * `MV40` is what settles that: it holds a partial result in K2 while `MULT3` overwrites K, and
   * then adds the two together. They are live at the same time, so folding them into one would
   * lose the first. The original agrees -- K is at zero page 119 and K2 at 178, nowhere near each
   * other.
   */
  std::uint8_t k2[4] = { 0, 0, 0, 0 };
};

/*
 * What the shift-and-add multipliers leave behind, and the carry is not incidental.
 *
 * Every one of them ends on a `ROR P` and neither the `DEX / BNE` above it nor the `RTS` below
 * touches the carry, so what a caller sees is that rotate's carry out -- the low bit of P before
 * the last shift. Three callers read it in an `ADC` or `SBC` with no `CLC`/`SEC` in between:
 * `MVEIT` after `MLTU2`, and the stardust after `MLU1` and `MLU2`. The port returned only the
 * byte until `MVEIT` came out one adrift in a ship's y coordinate (§6.33), and the stardust
 * needed the same thing from a different multiplier.
 */
struct WideResult
{
  std::uint8_t high = 0;
  bool carry = false;
};

/// 6502: MU11 -- (A P) = P * X, unsigned. Returns the high byte and the carry; the low byte is
/// left in P.
[[nodiscard]] WideResult MultiplyByX(MathWorkspace& _work, std::uint8_t _x) noexcept;

/// 6502: MULTU -- (A P) = P * Q, unsigned.
[[nodiscard]] WideResult MultiplyUnsigned(MathWorkspace& _work) noexcept;

/// 6502: MLU2 -- (A P) = |A| * Q, unsigned. The stardust reads its carry.
[[nodiscard]] WideResult MultiplyMagnitudeByQ(MathWorkspace& _work, std::uint8_t _a) noexcept;

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
[[nodiscard]] WideResult MultiplyWide(MathWorkspace& _work, std::uint8_t _a) noexcept;

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

/*
 * 6502: LL61 (with its LL84 error exit) -- (U R) = 256 * A / Q, for an A that is NOT smaller
 * than Q (slice 3b).
 *
 * `LL28`'s sister, and it works by borrowing `LL28`: halve A until it is small enough, divide,
 * then double the answer back the same number of times. The doubling is into `U`, which is why
 * this one is sixteen bits wide where `LL28` is eight.
 *
 * Two ways to fail and both answer 50 rather than saturating: a zero divisor, and an overflow out
 * of the doubling. Fifty is not a rounding of anything -- it is a value `LL9` treats as "this
 * vertex is roughly here", and the ship still gets drawn.
 *
 * `S` is used as scratch for the shift count and is LEFT there. `LL28` does not touch it, which
 * is what makes that safe, and a port that gave `LL28` a use for `S` would break this quietly.
 */
void DivideToUR(MathWorkspace& _work, std::uint8_t _a) noexcept;

/*
 * What `LL38` leaves behind, and the carry is part of it.
 *
 * The routine's own header says "C flag: set if the addition overflowed, clear otherwise", and it
 * goes to some trouble to make that true -- the subtracting branch has an explicit `CLC` before
 * its `RTS` that would otherwise be dead. `LL9`'s face-visibility loop reads it: a `BCS ovflw`
 * there halves the ship's position and starts the face again. The port returned only the byte
 * until `LL9` needed the flag, which is the third time a dropped register has come back (§6.33).
 */
struct SignedSum
{
  std::uint8_t value = 0;
  bool carry = false;
};

/// 6502: LL38 (with its LL39 and LL40 branches) -- combines Q and R under the signs in A and S,
/// flipping S when the result turns negative.
[[nodiscard]] SignedSum CombineSigned(MathWorkspace& _work, std::uint8_t _a) noexcept;

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

/*
 * 6502: MULT3 -- K(4) = (A P+1 P) * Q, a twenty-four bit magnitude by an eight bit one, signed
 * (slice 3a).
 *
 * The shift-and-add is the usual one with a trick in it worth naming, because it looks like an
 * off-by-one: the routine stores |Q| - 1 in T and then adds it with `ADC` at a point where the
 * carry is always SET, so what actually gets added is |Q|. The subtraction and the carry cancel,
 * and a port that "corrected" the `SBC #1` would be wrong by one on every partial product.
 *
 * `MVEIT` reaches this through `MV40`, the path a planet or a sun takes. The name follows
 * `MultiplySignedToSR` (`MULT12`), because what distinguishes these from the other multipliers is
 * where they leave the answer rather than what they do to it.
 */
void MultiplySignedToK(MathWorkspace& _work, std::uint8_t _a) noexcept;

/*
 * 6502: NORM -- scale the three-byte vector in XX15 to a length of 96 (slice 3a).
 *
 * Sum the squares, take the square root, divide each component by it. `TIDY` calls this every
 * sixteenth iteration of the main loop to stop a ship's orientation vectors drifting out of shape
 * as the rounding in `MVEIT` accumulates.
 *
 * THE ADDITIONS HAVE NO `CLC` BEFORE THEM, which is not an oversight in the original and is the
 * one thing here a port can quietly get wrong: `LDA P / ADC Q` follows `JSR SQUA`, so whatever
 * carry `SQUA` exits with is part of the sum. `TheNormaliserMatchesNORM` sweeps the vector space
 * against the shipped routine, which is what settles it rather than reading the multiplier.
 */
void Normalise(MathWorkspace& _work, std::span<std::uint8_t, 3> _vector) noexcept;

/*
 * 6502: DVID3B -- sign-magnitude, twenty-four bits over twenty-four (slice 3b). This is the
 * divide the whole of the projection runs through.
 *
 * IT RETURNS 256 TIMES THE RATIO. The upstream summary and the routine's own name both say
 * `K(3 2 1 0) = P(2 1 0) / (S R Q)`, and that is the ratio with a scale left off: the eight-bit
 * division at the middle of it produces `256 * A / Q`, and the shifts at the end put back the
 * difference between the two scaling loops and nothing else. So `K = 256 * P / (S R Q)`, which is
 * what makes it a projection -- one screen pixel is a ratio of 1/256, and a ship at z = 1 is off
 * the screen whatever its x is. §6.36 records what it cost to find that out.
 *
 * The trick that makes it work with an EIGHT-bit divider is scaling. Shift the numerator left
 * until its top byte reaches 64, counting the shifts up in Y; shift the denominator left until
 * its top bit is set, counting those same shifts back down; divide the two top bytes with LL31's
 * body; then shift the answer by whatever Y ended up as. Shifting both sides the same way does
 * not change a ratio, so all the scaling has to do is keep the tally.
 *
 * Two things a port gets wrong by reading it as arithmetic, and one that looks like a third and
 * is not.
 *
 * The inlined `LL31` runs exactly eight times because R starts at 254: the seven set bits are a
 * counter that shifts out of the top, and the loop ends when the zero in bit 0 reaches bit 7.
 *
 * The high branch of that division does not compare anything. A numerator whose shift pushed a
 * bit into the carry is nine bits wide, so it cannot be smaller than an eight-bit denominator,
 * and the original subtracts and then forces the quotient bit with a `SEC`. Taking that bit from
 * the subtraction's own carry instead is wrong on every call that reaches the branch.
 *
 * And the denominator loop LOOKS as though its shape matters -- the `DEY` is at the top and the
 * test at the bottom, so it always runs once -- and it does not: A is `S AND %01111111` on entry,
 * bit 7 is therefore clear, and a while-loop would enter too. The `BMI DV9` commented out above
 * it in the original source could never have branched either. Rewriting the loop as a while is an
 * EQUIVALENT mutation and the sweep does not catch it, which is the honest thing to say about it.
 *
 * Q MUST BE NON-ZERO. `DVID3B2` guarantees that with an `ORA #1` before it sets Q, and with a
 * zero denominator the original spins forever waiting for a bit that never arrives -- so this
 * does too, rather than inventing an answer the game has never seen.
 */
void DivideSignedToK(MathWorkspace& _work) noexcept;

} // namespace Elite
