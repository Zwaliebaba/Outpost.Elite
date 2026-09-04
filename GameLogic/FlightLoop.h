#pragma once

#include <cstdint>

#include "Arith.h"
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

} // namespace Elite
