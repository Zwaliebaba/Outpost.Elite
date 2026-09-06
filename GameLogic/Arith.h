#pragma once

#include "EliteTypes.h"

#include <cstdint>
#include <span>

namespace Elite
{

  /*
   * The zero-page scratch bytes the drawing and movement routines still share (Modernize.md
   * slice M2-c takes them further).
   *
   * In the original these are fixed addresses that every routine reads and writes by name, and
   * callers set some of them up before a call and read others afterwards. Until M2-b the
   * arithmetic kernel was one of those routines: a multiplier "left the low byte in P" for a caller
   * three files away, and the workspace was passed to hundreds of functions so that it could. The
   * kernel now takes its operands as values and hands its answers back as structs (`Product`,
   * `Quotient`, `SignedSum` and kin below), so what is left here is the scratch the NON-kernel
   * routines hand each other -- the clipper's slope, the planet drawer's radius, the stardust's
   * coordinate -- which the census in Modernize.md section 4.3 names field by field.
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
     * 6502: CNT -- zero page 170, a counter with FOURTEEN users.
     *
     * `LL9` parts 6 and 8, `BLINE`, `CIRCLE2`, `PLS22`, `SUN` parts 1 and 3, `TACTICS`, `DOEXP`,
     * `PTCLS2`, `SPIN` and `STATUS` all write it and read it back. Every one of them initialises it
     * before reading, so nothing hands it between units and separate copies would be unobservable
     * -- which is the argument that kept `XX2` and `K3` apart. Here it costs one field to be right
     * instead of unobservably-not-wrong, so it lives here rather than in the first workspace that
     * happened to need it (§6.49).
     */
    std::uint8_t cnt = 0;

    /*
     * 6502: TGT and CNT2 -- 168 and 171, and shared the same way `CNT` is (§6.49).
     *
     * `TGT` is what a walk counts up to: `PLS2` sets it to 31 for a meridian, `PL9` to 64 for a
     * crater, `SUN` to its own, and `DOEXP` and `PTCLS2` to theirs. `CNT2` is the angle a walk is
     * at, and `TACTICS`, `DOCKIT` and `TITLE` use it for something else entirely.
     *
     * They are here for the same reason and with the same argument: every user sets them before
     * reading, so separate copies would be unobservable, and one field costs less than the proof.
     */
    std::uint8_t tgt = 0;
    std::uint8_t cnt2 = 0;

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

    // 6502: K, a four-byte result block that the planet and sun drawers and the dashboard fill.
    // What the kernel used to leave here comes back as a `KBlock` value since M2-b.
    std::uint8_t k[4] = {0, 0, 0, 0};

    /*
     * 6502: K2 -- a SECOND four-byte block, and separate storage rather than a second use of K.
     *
     * The ellipse drawer holds one pair of projected axes here while `K` holds the other, and the
     * original agrees that they are two blocks -- K is at zero page 119 and K2 at 178, nowhere near
     * each other. (`MV40`, which was the first argument for two blocks, holds both as locals now.)
     */
    std::uint8_t k2[4] = {0, 0, 0, 0};
  };

  // ---- what the kernel answers with ------------------------------------------------------------

  /*
   * 6502: (A P) and the carry -- what the shift-and-add multipliers leave behind.
   *
   * Every one of them ends on a `ROR P` and neither the `DEX / BNE` above it nor the `RTS` below
   * touches the carry, so what a caller sees is that rotate's carry out -- the low bit of P before
   * the last shift. Three callers read it in an `ADC` or `SBC` with no `CLC`/`SEC` in between:
   * `MVEIT` after `MLTU2`, and the stardust after `MLU1` and `MLU2`. The port returned only the
   * byte until `MVEIT` came out one adrift in a ship's y coordinate (§6.33), and the stardust
   * needed the same thing from a different multiplier. The LOW byte was left in `P` until M2-b, for
   * `ADD` and the stardust to read from the workspace; it is a field now.
   */
  struct Product
  {
    std::uint8_t high = 0; ///< 6502: A
    std::uint8_t low = 0;  ///< 6502: P
    bool carry = false;

    /// The product as the sixteen-bit sign-magnitude pair `ADD` takes: (A P), high byte first.
    [[nodiscard]] constexpr SignMag16 Pair() const noexcept
    {
      return SignMag16{low, high};
    }
  };

  /// 6502: (A P+1 P) and the carry -- the sixteen-step multiply's twenty-four bit product.
  struct Product24
  {
    std::uint8_t high = 0; ///< 6502: A
    std::uint8_t mid = 0;  ///< 6502: P+1
    std::uint8_t low = 0;  ///< 6502: P
    bool carry = false;
  };

  /// What the sign-magnitude addition hands back: the original returns the high byte in A and
  /// the low byte in X, and callers use both.
  struct AddSignedResult
  {
    std::uint8_t high = 0;
    std::uint8_t low = 0;

    /*
     * 6502: the carry, which `PLS22` reads twice and the port dropped until it did (§6.53).
     *
     * `ADD` has three exits and none of them clears it: the same-sign path leaves whatever
     * `ADC T1` produced, the `BCS MU9` path leaves it SET by definition, and the negating path
     * leaves the second `SBC U`'s. `PLS22` then does `STA T / BPL PL42 / ... / .PL42 TXA /
     * ADC K3`, so a meridian's position on the screen depends on it.
     *
     * The ninth dropped flag, and the field is added rather than the signature changed because
     * every other caller reads `high` and `low` alone.
     */
    bool carry = false;

    /// The sum as the pair the next `ADD` or `MAD` takes as its (S R).
    [[nodiscard]] constexpr SignMag16 Pair() const noexcept
    {
      return SignMag16{low, high};
    }
  };

  /// 6502: R and the carry -- the logarithm divide's answer, which `LL9` branches on.
  struct Quotient
  {
    std::uint8_t value = 0; ///< 6502: R
    bool carry = false;
  };

  /// 6502: (U R) -- the sixteen-bit answer of `LL61`, which `LL9`'s projection reads both halves of.
  struct Quotient16
  {
    std::uint8_t high = 0; ///< 6502: U
    std::uint8_t low = 0;  ///< 6502: R
  };

  /// 6502: (P+1 P) and T -- the long division's quotient and the sign it applies on the way out.
  struct WideQuotient
  {
    std::uint8_t high = 0; ///< 6502: P+1
    std::uint8_t low = 0;  ///< 6502: P
    std::uint8_t sign = 0; ///< 6502: T -- the operands' signs, EORed, in bit 7

    /// 6502: LDA P / ORA T -- what the routine returns in A: the low byte with the sign on top.
    [[nodiscard]] constexpr std::uint8_t Signed() const noexcept
    {
      return static_cast<std::uint8_t>(low | sign);
    }
  };

  /// 6502: A and the carry from the logarithm multiply -- a byte scaled by another over 256.
  struct LogProduct
  {
    std::uint8_t value = 0; ///< 6502: A
    bool carry = false;
  };

  /*
   * What `LL38` leaves behind, and the carry is part of it.
   *
   * The routine's own header says "C flag: set if the addition overflowed, clear otherwise", and it
   * goes to some trouble to make that true -- the subtracting branch has an explicit `CLC` before
   * its `RTS` that would otherwise be dead. `LL9`'s face-visibility loop reads it: a `BCS ovflw`
   * there halves the ship's position and starts the face again. The port returned only the byte
   * until `LL9` needed the flag, which is the third time a dropped register has come back (§6.33).
   * `sign` is `S` on the way out -- flipped when the subtraction went past zero -- which the
   * callers used to read back from the workspace.
   */
  struct SignedSum
  {
    std::uint8_t value = 0; ///< 6502: A
    std::uint8_t sign = 0;  ///< 6502: S, afterwards
    bool carry = false;
  };

  /// What `DVID4` leaves: the whole part in `P`, the fraction in `R`, and the carry `SPS2` passes
  /// on to `SP2` (§6.60).
  struct ScaledDivision
  {
    std::uint8_t whole = 0;    ///< 6502: P
    std::uint8_t fraction = 0; ///< 6502: R -- the remainder scaled up by the divisor, through LL28's body
    bool carry = false;
  };

  /// 6502: Q and the carry -- the square root, and the last bit to fall out of it.
  struct Root
  {
    std::uint8_t value = 0; ///< 6502: Q
    bool carry = false;
  };

  /*
   * 6502: K(3 2 1 0) -- the four-byte block `MULT3` and `DVID3B` answer with and `MVT3` adds to.
   *
   * Three bytes of magnitude, low first, and a fourth holding the sign in bit 7 over the
   * magnitude's top seven bits. Bytes 1 to 3 have the shape of a ship's coordinate, which is how
   * `MV40` reads them: the bottom byte exists to carry into the one above it, and the answer is
   * stored from K+1 upwards.
   */
  struct KBlock
  {
    std::uint8_t low = 0;  ///< 6502: K
    std::uint8_t mid = 0;  ///< 6502: K+1
    std::uint8_t high = 0; ///< 6502: K+2
    std::uint8_t top = 0;  ///< 6502: K+3 -- seven bits of magnitude under the sign

    /// 6502: MU5 -- fills the four-byte K block with A. The original also clears carry; nothing
    /// downstream reads that, so it is not modelled.
    [[nodiscard]] static constexpr KBlock Filled(std::uint8_t _value) noexcept
    {
      return KBlock{_value, _value, _value, _value};
    }

    /// 6502: K+1 to K+3 as INWK reads them -- the coordinate the block holds from its second byte.
    [[nodiscard]] constexpr SignMag24 Coordinate() const noexcept
    {
      return SignMag24{mid, high, top};
    }

    [[nodiscard]] constexpr bool operator==(const KBlock&) const noexcept = default;
  };

  // ---- the multipliers ---------------------------------------------------------------------------

  /// 6502: MU11 -- (A P) = P * X, unsigned, with no guard on a zero multiplier: `MULTU` is the
  /// entry point that checks, and a zero here multiplies by 255 as the original would.
  [[nodiscard]] Product MultiplyUnguarded(std::uint8_t _multiplicand, std::uint8_t _multiplier) noexcept;

  /// 6502: MULTU -- (A P) = P * Q, unsigned.
  [[nodiscard]] Product MultiplyUnsigned(std::uint8_t _multiplicand, std::uint8_t _multiplier) noexcept;

  /// 6502: MLU2 -- (A P) = |A| * Q, unsigned. The stardust reads its carry.
  [[nodiscard]] Product MultiplyMagnitude(std::uint8_t _value, std::uint8_t _multiplier) noexcept;

  /*
   * 6502: MULT1 -- (A P) = Q * A for sign-magnitude operands; the high byte carries the sign.
   *
   * 6502: MULT12 -- the same product stored as (S R). A caller that went on to `MAD` or `ADD` used
   * to find it in the workspace; it hands the `Product`'s pair over instead.
   */
  [[nodiscard]] Product MultiplySigned(std::uint8_t _value, std::uint8_t _multiplier) noexcept;

  /*
   * 6502: SQUA2 -- (A P) = A * A for an A already known to be positive.
   *
   * Returns the carry, because `MAS3` reads it: it sums three squares with `JSR SQUA2 / ADC R` and
   * no `CLC` between them, twice over. The fifteenth dropped flag -- and it is ALWAYS CLEAR, over
   * every input either entry point can be given, which the exhaustive sweep asserts rather than
   * argues. `MU1`, taken when A is zero, opens `CLC`; `MU11` ends on a `ROR P` that never carries
   * out for a square.
   *
   * So `MAS3` was already right before this was modelled: an `ADC` cannot see a clear carry, which
   * is §6.65's question answered the harmless way. `DVID4`'s carry is the same shape (§6.60);
   * `DIL2`'s is not, because there it lands in an `SBC` (§6.70).
   */
  [[nodiscard]] Product SquareUnsigned(std::uint8_t _value) noexcept;

  /// 6502: SQUA -- (A P) = |A| * |A|, clearing the sign bit first. Carries the same flag.
  [[nodiscard]] Product Square(std::uint8_t _value) noexcept;

  /// 6502: MULTS -- (A P) = P * |A|, scaled: only five of the eight bits get an addition and the
  /// remaining three are shifted through, which divides the result down. Used where one operand
  /// is known to be small. (6502: MU6 is its zero exit, which clears P and P+1; nothing reads the
  /// second byte, so the port has no field for it.)
  [[nodiscard]] Product MultiplyScaled(std::uint8_t _multiplicand, std::uint8_t _value) noexcept;

  /// 6502: MLTU2 -- (A P+1 P) = (~A P) * Q, sixteen steps through the complemented multiplier:
  /// `_high` arrives as the ONES' COMPLEMENT of the multiplicand's high byte, as the routine reads it.
  [[nodiscard]] Product24 MultiplyWide(std::uint8_t _high, std::uint8_t _low, std::uint8_t _multiplier) noexcept;

  // ---- the sign-magnitude adders -----------------------------------------------------------------

  /// 6502: ADD (with its MU8 and MU9 branches) -- (A X) = (A P) + (S R), sign-magnitude. `_value`
  /// is (A P) and `_addend` is (S R), each with its sign in the high byte's bit 7.
  [[nodiscard]] AddSignedResult AddSigned(SignMag16 _value, SignMag16 _addend) noexcept;

  /// 6502: MAD -- (A X) = Q * A + (S R). The multiply-accumulate the geometry code runs on.
  [[nodiscard]] AddSignedResult MultiplyAndAdd(std::uint8_t _value, std::uint8_t _multiplier, SignMag16 _addend) noexcept;

  // ---- the dividers ------------------------------------------------------------------------------

  /// 6502: DVID96 -- A = A / 96, keeping the sign bit. The tail TIS1 shares.
  [[nodiscard]] std::uint8_t DivideBy96(std::uint8_t _value) noexcept;

  /// 6502: TIS1 -- (A ?) = (-X * A + (S R)) / 96, sign-magnitude. `_multiplier` is X, which the
  /// routine stores in Q on the way in.
  [[nodiscard]] std::uint8_t MultiplyAddDivide96(std::uint8_t _value, std::uint8_t _multiplier, SignMag16 _addend) noexcept;

  /// 6502: TIS2 -- A = A / Q, sign-magnitude, saturating at 96 when the magnitude is too large.
  [[nodiscard]] std::uint8_t DivideSigned(std::uint8_t _value, std::uint8_t _divisor) noexcept;

  /// 6502: DVIDT -- (P+1 P) = (A P+1) / Q, sixteen-step long division. `_high` is A and `_low` is
  /// the P the caller staged; the answer's `Signed()` is the byte the routine returns.
  [[nodiscard]] WideQuotient DivideWide(std::uint8_t _high, std::uint8_t _low, std::uint8_t _divisor) noexcept;

  // ---- the logarithm-table routines -----------------------------------------------------
  //
  // Where shift-and-add is too slow, the game adds logarithms and looks the answer back up.
  // These read the tables in LookupTables.h and are exact about which of the two inverse tables
  // applies, because that choice falls out of a parity test rather than being a detail.

  /*
   * 6502: FMLTU -- A = A * Q / 256, through the logarithm tables.
   *
   * IT CLOBBERS `P`. The routine opens `STX P` and every one of its four exits ends `LDX P`, so it
   * preserves the caller's X by parking it in `P` -- and `P` keeps that register value afterwards.
   * A port that does not model registers cannot say what X was, so this port has never written it,
   * and the fact is written here rather than lost.
   *
   * Nothing in the shipped build reads `P` after an `FMLTU` without writing it first: the six
   * callers are `MVEIT` part 3, `CIRCLE2` through `FMLTU2`, `LL51`, `PLS22`, `LL9` part 5 and
   * `EXS1`, and of those only `PLS22` mentions `P` at all -- twice, both `STA P`. So the stale byte
   * is invisible to the game, and it stopped being invisible to the PORT the moment slice 4b-b
   * compared `P` after `PTCLS` (§6.144). `EXS1` is the one call site that can prove what X was --
   * the generator's previous byte, two instructions earlier -- and it writes `P` itself.
   */
  [[nodiscard]] LogProduct MultiplyByLog(std::uint8_t _value, std::uint8_t _multiplier, bool _carryIn) noexcept;

  /// 6502: LL28 -- R = 256 * A / Q, saturating at 255 when A is not smaller than Q. Returns the
  /// carry the routine leaves, because its callers branch on it.
  [[nodiscard]] Quotient DivideByLog(std::uint8_t _dividend, std::uint8_t _divisor) noexcept;

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
   * `S` was used as scratch for the shift count and left there; it is a local now, and `LL28` not
   * touching it is what made that safe in the original.
   *
   * `_high` IS AN INPUT: the doubling is `ROL U`, which rotates whatever the caller left in `U` up
   * under the answer. `LL9` clears it before the y divide for exactly that reason, and arrives at
   * the x divide with the zero its halving loop ended on -- so both callers hand it a zero, and the
   * sweep hands it something else to prove the rotate is a rotate.
   */
  [[nodiscard]] Quotient16 DivideWideByLog(std::uint8_t _dividend, std::uint8_t _divisor, std::uint8_t _high) noexcept;

  /// 6502: LL38 (with its LL39 and LL40 branches) -- combines Q and R under the signs in A and S,
  /// flipping S when the result turns negative. `_termSign` is A (a sign in bit 7), `_term` is Q,
  /// and `_total` is (S R): the running sum and its sign, which comes back as `SignedSum::sign`.
  [[nodiscard]] SignedSum CombineSigned(std::uint8_t _termSign, std::uint8_t _term, SignMag16 _total) noexcept;

  /// 6502: ARCTAN -- the angle of the ratio P over Q, as a byte turn.
  [[nodiscard]] std::uint8_t Arctan(std::uint8_t _numerator, std::uint8_t _denominator) noexcept;

  /*
   * 6502: FMLTU2 -- A = K * sin(A) / 256, where the sine comes from SNE indexed by the low five
   * bits of A. It sets Q and falls straight through into FMLTU, so this is that whole path.
   *
   * The carry is part of the answer, and `CIRCLE2` is the caller that reads it: `JSR FMLTU2 / TAX /
   * LDA #0 / STA T / LDA CNT / ADC #15`, with no `CLC`. `FMLTU`'s two antilog exits leave it SET
   * and its zero exit leaves it CLEAR, so it is data-dependent and not a constant like `MULTU`'s
   * (§6.43). The eighth dropped flag, and again the exhaustive sweep that already existed verified
   * the wider model for the price of one line (§6.42).
   */
  [[nodiscard]] LogProduct MultiplyBySine(std::uint8_t _value, std::uint8_t _angle, bool _carryIn) noexcept;

  /*
   * 6502: DVID4 -- an 8.8 fixed-point divide. P comes out as the whole part of A / Q, and R as
   * the fraction: eight steps of restoring division, then -- because the routine has no RTS of
   * its own -- LL28's body scaling the remainder back up, which leaves R.
   *
   * Both halves are the routine. Its two callers in the shipped game JSR to the top and return
   * from the bottom of the code it falls into, so a port that stopped after the division would be
   * a different routine that happens to share a name.
   *
   * THE EXIT CARRY IS THE LOGARITHM DIVIDE'S, and only the saturating exit sets it. The eight
   * division steps cannot leave it set: `ASL A / STA P` puts a zero in P's bit 0, and eight
   * `ROL P`s later that zero is what comes out -- so the carry at the fall-through is always
   * clear, and what a caller sees is `LL222` or nothing. `SPS2` hands it to an `ADC #195` and an
   * `SBC T` with no `CLC` or `SEC` between, which is the thirteenth dropped flag (§6.60).
   *
   * The shipped C64 build unrolls the eight steps rather than looping; that changes nothing about
   * the result, which is why this reads as a loop.
   */
  [[nodiscard]] ScaledDivision DivideAndScale(std::uint8_t _dividend, std::uint8_t _divisor) noexcept;

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
  [[nodiscard]] Root SquareRoot(std::uint8_t _high, std::uint8_t _low) noexcept;

  /*
   * 6502: MULT3 -- K(4) = (A P+1 P) * Q, a twenty-four bit magnitude by an eight bit one, signed
   * (slice 3a). `_value` is (A P+1 P) as the sign-magnitude coordinate it always is at the one
   * caller: the sign in `sgn`'s bit 7 over the magnitude's top bits.
   *
   * The shift-and-add is the usual one with a trick in it worth naming, because it looks like an
   * off-by-one: the routine stores |Q| - 1 in T and then adds it with `ADC` at a point where the
   * carry is always SET, so what actually gets added is |Q|. The subtraction and the carry cancel,
   * and a port that "corrected" the `SBC #1` would be wrong by one on every partial product.
   *
   * `MVEIT` reaches this through `MV40`, the path a planet or a sun takes.
   */
  [[nodiscard]] KBlock MultiplySigned24(SignMag24 _value, std::uint8_t _multiplier) noexcept;

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
   *
   * RETURNS THE LENGTH, which the original leaves in `Q`: `DOCKIT` reads it after `TA2` falls in
   * here (`JSR TA2 / LDA Q / STA K`), so it is part of the answer and not scratch.
   */
  [[nodiscard]] std::uint8_t Normalise(std::span<std::uint8_t, 3> _vector) noexcept;

  /*
   * 6502: DVID3B -- sign-magnitude, twenty-four bits over twenty-four (slice 3b). This is the
   * divide the whole of the projection runs through. `_numerator` is P(2 1 0) and `_denominator`
   * is (S R Q), each with its sign in the top byte's bit 7.
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
  [[nodiscard]] KBlock DivideSigned24(SignMag24 _numerator, SignMag24 _denominator) noexcept;

} // namespace Elite
