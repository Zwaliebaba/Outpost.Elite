#pragma once

#include <cstdint>

#include "Arith.h"
#include "Dashboard.h"
#include "Rng.h"
#include "ShipMove.h"
#include "ShipSlot.h"

namespace Elite
{

/*
 * What the flight loop calls but does not need (slice 3d-d-i).
 *
 * Four routines the loop uses to answer "how far away is that, roughly" without dividing -- two
 * that OR sign bytes together to find the largest, one that sums three squares, and one that
 * doubles a coordinate and adds another to it -- and the damping the loop applies to the
 * controls before it reads them. None of them needs the loop, which is why they come first
 * (§6.69).
 */

/*
 * 6502: MAS1 -- K(3 2 1) = INWK(Y) doubled, plus INWK(X), written back over INWK(X).
 *
 * The doubling is a sixteen-bit `ASL`/`ROL` with the carry caught in a third byte by
 * `LDA #0 / ROR A`, which turns the overflow into a SIGN rather than losing it -- so what
 * `MVT3` then adds to is a signed 24-bit value built out of a 16-bit one.
 *
 * `MVT3` ends `STA K+3` on every one of its three paths, so the `STA INWK+2,X` that follows it
 * stores `K+3` and the port needs nothing returned from the call. Returns the high byte with the
 * sign cleared, which is what the caller compares against a distance.
 */
[[nodiscard]] std::uint8_t DoubleAndAddCoordinate(ShipBlock& _work, MathWorkspace& _math,
                                                  std::uint8_t _from, std::uint8_t _to) noexcept;

/*
 * 6502: MAS2, and `m` above it -- OR the three sign bytes of a ship block together and drop the
 * sign, which is "the largest of the three distances, to within a factor of two".
 *
 * TWO ENTRY POINTS. `m` is `LDA #0` and then falls in, so it starts from nothing; `MAS2` ORs into
 * whatever the caller left in A. The third such routine this slice has met, after `DILX`'s four
 * and `CLYNS`'s two (§6.63, §6.67), and the only one where both are used deliberately.
 */
[[nodiscard]] std::uint8_t LargestAxisFrom(const Bubble& _bubble, std::uint8_t _slot,
                                           std::uint8_t _a) noexcept;

/// 6502: `m` -- `MAS2` entered with A cleared, which is the ordinary way in.
[[nodiscard]] inline std::uint8_t LargestAxis(const Bubble& _bubble, std::uint8_t _slot) noexcept
{
  return LargestAxisFrom(_bubble, _slot, 0);
}

/*
 * 6502: MAS3 -- A = x^2 + y^2 + z^2 of a ship block's HIGH bytes, saturating at 255.
 *
 * Two `ADC R`s with no `CLC`, both reading the carry `SQUA2` exits with -- which is never set, so
 * the additions are the plain ones they look like. That is measured over all 512 inputs rather
 * than assumed, and it is why `MAS3` needed no change when the flag was modelled (§6.70).
 */
[[nodiscard]] std::uint8_t SumOfSquares(const Bubble& _bubble, MathWorkspace& _math,
                                        std::uint8_t _slot) noexcept;

/// 6502: MAS4 -- the same OR as `MAS2` but over `INWK`'s high bytes rather than a slot's sign
/// bytes, and without the mask. Four instructions, and it is here because the loop calls it.
[[nodiscard]] std::uint8_t LargestShipAxis(const ShipBlock& _work, std::uint8_t _a) noexcept;

/*
 * 6502: cntr -- creep a centre-based control reading one step towards 128.
 *
 * The value runs 1 to 255 with 128 as centred, so damping is "add one below the middle, subtract
 * one above it". Flight loop part 2 is its only caller and it calls it THREE times: twice on
 * `JSTX`, so the roll creeps back by two per pass, and once on `JSTY`.
 *
 * `_dampingDisabled` is `DAMP`, which is a configuration byte the "CAPS LOCK" option toggles
 * between 0 and &FF, and it reads backwards on purpose: NON-ZERO means the damping is switched
 * off. `_dockingComputer` is `auto`, and it wins -- the autopilot always gets damping, whatever
 * the player set.
 *
 * ITS LAST TWO INSTRUCTIONS CANNOT RUN. `.REDU DEX / BEQ BUMP` is reached only when `BUMP`'s
 * `INX` wraps to zero, which needs X = 255 on entry to `BUMP`; but `BUMP` is entered only with
 * X < 128 (from `BPL`) or with X = 128 (undoing the `DEX`), so the wrap never happens. The port
 * leaves them out and the sweep proves it rather than assuming it: a trap on `REDU` records no
 * hits across all 2,304 inputs (§6.71).
 */
[[nodiscard]] std::uint8_t DampTowardsCentre(std::uint8_t _value, std::uint8_t _dockingComputer,
                                             std::uint8_t _dampingDisabled) noexcept;

/*
 * 6502: DENGY -- take one unit off the energy banks, and say whether that emptied them.
 *
 * `DEC ENERGY / PHP / BNE P%+5 / INC ENERGY / PLP / RTS`. The `PHP` is the point: the flag the
 * caller sees is the one the DECREMENT set, not the one the `INC` that undoes it would have. So
 * the banks never reach zero through this -- one is the floor -- and the caller still learns that
 * they tried to.
 *
 * `ENERGY` at zero decrements to 255, which is not guarded and does not need to be: nothing calls
 * this with the banks already empty.
 */
[[nodiscard]] bool DrainEnergy(FlightStatus& _status) noexcept;

/*
 * 6502: SHD, which FALLS INTO DENGY -- bump a shield by one, and PAY FOR IT.
 *
 * `INX / BEQ SHD-2`, and `SHD-2` is the `DEX / RTS` two bytes above the label. So a shield already
 * at 255 is put back and the routine returns; anything less is incremented AND THEN RUNS ON INTO
 * `DENGY`, which takes a unit off the energy banks.
 *
 * A port that read this as a saturating increment -- which is exactly what the four instructions
 * look like -- would recharge the shields for free (§6.83). Flight loop part 13 calls it twice
 * per eighth frame, so the difference is the whole economy of running with the shields down.
 */
[[nodiscard]] std::uint8_t RechargeShield(FlightStatus& _status, std::uint8_t _shield) noexcept;

/*
 * 6502: FAROF2, with `FAROF` one instruction above it -- is every one of a ship's high bytes
 * below `_limit`?
 *
 * Three compares and two branches, and the carry it returns is the answer: SET when the ship is
 * inside the box, clear when any axis is outside it. `FAROF` is `LDA #224` and then this, which
 * is the distance at which the flight loop stops caring about a ship at all.
 */
[[nodiscard]] bool WithinRange(const ShipBlock& _work, std::uint8_t _limit) noexcept;

/// 6502: FAROF -- `WithinRange` at the limit the loop uses, which is 224.
[[nodiscard]] inline bool WithinLoopRange(const ShipBlock& _work) noexcept
{
  return WithinRange(_work, 224u);
}

/*
 * 6502: HITCH -- have we hit this ship?
 *
 * FIVE WAYS TO SAY NO BEFORE IT MEASURES ANYTHING. A non-zero `INWK+8` (the ship is not in front
 * of us), a negative type (the planet or the sun), an exploding ship, or a large x or y offset all
 * take the same `BNE HI1` out with the carry as `CLC` left it. Only then does it square x and y,
 * add them, and compare the sum against the blueprint's own target area at `(XX0),0` and
 * `(XX0),1`.
 *
 * The overflow path is not the same as the near misses. `BCS TN10` reaches a `CLC / RTS` of its
 * own, which says "no" for a sum too big to compare rather than for a ship too far to the side --
 * the same answer by a different route, and the port keeps them apart because the original does.
 */
[[nodiscard]] bool IsHit(const ShipBlock& _work, MathWorkspace& _math, std::uint16_t _blueprint,
                         std::uint8_t _type) noexcept;

/// 6502: SFS1 -- phase 4's "spawn a child ship from this one", which is where the wreckage
/// actually comes from. It is here rather than in `Spawn.h` because the only thing in this slice
/// that calls it is `SPIN`, and `SPIN` is the flight loop's.
class SpawnChildEffects
{
public:
  virtual ~SpawnChildEffects() = default;

  /// 6502: JSR SFS1 with A = the AI flag and X = the type. It returns a carry saying whether the
  /// ship fitted; `SPIN` does not look at it, and this slice has no other caller.
  [[nodiscard]] virtual bool SpawnChild(std::uint8_t _aiFlag, std::uint8_t _type) = 0;
};

/*
 * 6502: SPIN2 -- spawn `_count` ships of one type, one after another.
 *
 * `.spl BEQ oh` READS A FLAG THE INSTRUCTION ABOVE IT DID NOT SET. `STA CNT` leaves the flags
 * alone, so the `BEQ` at the top of the loop is testing whatever the CALLER left in Z -- which
 * for `SPIN2`'s only caller is the `AND #3` two instructions earlier, and for `SPIN` is its own
 * `AND #15`. Both happen to describe the count, which is what makes the loop look ordinary.
 *
 * The loop's back edge is `BNE spl+2`, which lands one instruction PAST the `BEQ`, so the test
 * runs once on entry and never again: after that it is `DEC CNT / BNE` that decides. A port that
 * kept the test inside the loop would agree with the game on every input and be a different
 * routine.
 */
void SpawnItems(MathWorkspace& _math, SpawnChildEffects& _effects, std::uint8_t _type,
                std::uint8_t _count) noexcept;

/*
 * 6502: SPIN -- a destroyed ship drops some of its cargo, or does not.
 *
 * Half the time nothing happens at all: `JSR DORND / BPL oh` throws the whole call away on a
 * clear bit 7. That is the ONLY thing the roll decides.
 *
 * THE COUNT IS NOT RANDOM. `TYA / TAX / LDY #0 / AND (XX0),Y / AND #15` reads as "copy the type
 * into X for `SFS1`", and it does that -- but the copy goes through A, so the `AND` that follows
 * masks the TYPE and not the random byte `DORND` left behind. A ship of a given type against a
 * given blueprint always drops the same amount, on the half of the calls that drop anything.
 * The port had it the obvious way round and the oracle disagreed on the first blueprint whose
 * byte 0 differed from the roll (§6.74).
 */
void SpawnDebris(Rng& _rng, MathWorkspace& _math, SpawnChildEffects& _effects,
                 std::uint16_t _blueprint, std::uint8_t _type, bool _carryIn) noexcept;

} // namespace Elite
