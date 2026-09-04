#pragma once

#include "Arith.h"
#include "Canvas.h"
#include "Rng.h"
#include "ShipMove.h"

#include <array>
#include <cstdint>

namespace Elite
{

/*
 * The stardust (slice 3c).
 *
 * Elite has no starfield. It has at most twelve specks of dust in a box around the player, each
 * with a position and a distance, and every frame it moves them, draws them, and replaces any
 * that leave the box with a new one at the far edge. That is the entire sense of motion in the
 * game: the ships do not move past you, the dust does.
 *
 * Each particle is three sixteen-bit coordinates held as two parallel arrays -- `SX` with `SXL`
 * for the fraction, and so on. Thirteen entries, which three measurements agree on: `NOSTM`
 * counts down to 1 so entry 0 is never used, the loops index by Y from `NOSTM`, and `SX` is at
 * 1698 with `SXL` at 1711.
 */
inline constexpr std::size_t STARDUST_SLOTS = 13;

struct Stardust
{
  std::array<std::uint8_t, STARDUST_SLOTS> x{};    ///< 6502: SX
  std::array<std::uint8_t, STARDUST_SLOTS> xLow{}; ///< 6502: SXL
  std::array<std::uint8_t, STARDUST_SLOTS> y{};    ///< 6502: SY
  std::array<std::uint8_t, STARDUST_SLOTS> yLow{}; ///< 6502: SYL
  std::array<std::uint8_t, STARDUST_SLOTS> z{};    ///< 6502: SZ
  std::array<std::uint8_t, STARDUST_SLOTS> zLow{}; ///< 6502: SZL

  /*
   * 6502: NOSTM -- how many specks there are, and it is only ever `NOST` or 3.
   *
   * `NOST` is **12** in this build, which the layout agrees with: `SX` is at 1698 and `SXL` at
   * 1711. The upstream comments beside the two `STA NOSTM` instructions say "the maximum allowed
   * (18)" and "(20)", and both are other versions' — §6.38's lesson, and the number this port
   * would have taken if it had read the comment instead of the constant.
   *
   * Three is the value used near a planet, where the planet and the sun already give the eye
   * something to move against; twelve is out in the dark.
   *
   * The movers walk from `count` DOWN TO 1, so entry 0 is never used. A count of zero would run
   * the original's loop 256 times and read a long way past the arrays; it cannot happen, and this
   * port stops instead of reproducing it, which is the one place the stardust diverges.
   */
  std::uint8_t count = 0;

  /*
   * 6502: newzp -- zero page 186, one intermediate the side views keep across the loop body.
   *
   * `STARS2` stores the divide's quotient here and compares against it thirty instructions later,
   * by which time nothing else still holds it. It is the only byte of the movers' scratch that is
   * genuinely the stardust's: `XX` and `YY` looked like it and are not (§6.45), so they live in
   * `MathWorkspace` where `EDGES` and the sun can reach them.
   */
  std::uint8_t newzp = 0;
};

// ---- the wrappers, which are one or two instructions and a fall-through -------------------
//
// Seven routines in the ledger, and none of them is more than four bytes of setup before it runs
// into something already ported. They are functions here rather than inlined because the ledger
// counts them and because each is a named entry point the oracle can be called at (§6.20).

/// 6502: DV41 -- Q = A, then (P R) = DELTA / Q through `DVID4`.
[[nodiscard]] std::uint8_t DivideSpeedBy(MathWorkspace& _math, const FlightState& _flight,
                                         std::uint8_t _a) noexcept;

/// 6502: DV42 -- the same for a particle's own distance.
[[nodiscard]] std::uint8_t DivideSpeedByDistance(MathWorkspace& _math, const FlightState& _flight,
                                                 const Stardust& _dust, std::uint8_t _at) noexcept;

/// 6502: MLU1 -- Y1 = SY, then (A P) = |SY| * Q through `MLU2`. The carry is part of the answer:
/// the front view adds `SYL` to it and the rear view subtracts, neither with a `CLC` or a `SEC`.
[[nodiscard]] WideResult MultiplyByHeight(MathWorkspace& _math, DrawWorkspace& _draw,
                                          const Stardust& _dust, std::uint8_t _at) noexcept;

/// 6502: MLS1 -- P = ALP1, then `MULTS`. And `MULTS-2`, which is the same without the `LDX`,
/// reached by the movers with the multiplier already in X.
[[nodiscard]] std::uint8_t MultiplyByRoll(MathWorkspace& _math, const FlightState& _flight,
                                          std::uint8_t _a) noexcept;
[[nodiscard]] std::uint8_t MultiplyScaledBy(MathWorkspace& _math, std::uint8_t _x,
                                            std::uint8_t _a) noexcept;

/*
 * 6502: MLS2 -- (S R) = XX(1 0), then `MLS1`. And MUT1 and MUT2 -- R = XX, and S = XX+1 as well,
 * then `MULT1`.
 *
 * These three are in this file and take no `Stardust`, which is not a contradiction. Their only
 * callers in the whole build are `STARS1` and `STARS6`, so this is where they belong; the bytes
 * they read are shared with the sun, so `MathWorkspace` is what they take. Where a routine lives
 * and what it reads are separate questions and the ledger has conflated them seven times now.
 */
[[nodiscard]] std::uint8_t MultiplyPositionByRoll(MathWorkspace& _math, const FlightState& _flight,
                                                  std::uint8_t _a) noexcept;
[[nodiscard]] std::uint8_t MultiplyPosition(MathWorkspace& _math, std::uint8_t _a) noexcept;
[[nodiscard]] std::uint8_t MultiplyPositionSigned(MathWorkspace& _math, std::uint8_t _a) noexcept;

/*
 * 6502: PIX1 -- `ADD`, keep the answer as the particle's new y, and plot it.
 *
 * The ledger marked this ported with the rest of the pixel routines in slice 1d-a and it was not
 * (§6.41): it writes `SYL`, and the stardust arrays did not exist then.
 */
void PlotStardust(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math, Stardust& _dust,
                  std::uint8_t _at, std::uint8_t _a) noexcept;

/*
 * 6502: FLIP -- swap every speck's x and y, which reflects the whole field in the line x = y,
 * and redraw it.
 *
 * `LOOK1` calls it when the player changes view: it is a cheap way of making the field feel
 * different without generating a new one, and if you watch closely when you switch views you can
 * see that the new dust is the old dust mirrored in the diagonal. `LOOK1` is slice 3d's; this is
 * here because the workspace is.
 */
void FlipStardust(Canvas& _canvas, DrawWorkspace& _draw, Stardust& _dust) noexcept;


/*
 * 6502: STARS1, STARS2 and STARS6 -- move and redraw the whole field, once per frame.
 *
 * Three routines for three views and they are not the same arithmetic dressed differently. The
 * front view (`STARS1`) brings dust towards you and rolls and pitches it; the rear view
 * (`STARS6`) pushes it away, which is the same code with the subtractions turned round; and the
 * side views (`STARS2`) slide it across, which needs neither a divide by distance nor the same
 * kill test.
 *
 * Each ends by drawing every particle, and `PIXEL2` plots by EOR -- so the same call that draws
 * this frame's dust erases last frame's, exactly as the ship drawing does.
 *
 * A particle that leaves the box is not clipped, it is REPLACED: `KILL1`, `KILL2` and `KILL6`
 * each roll a new one at the far edge, which is why the field never thins out. That makes the
 * random generator part of the routine's answer rather than a detail, so it is a parameter.
 */
void MoveStardustAhead(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math,
                       const FlightState& _flight, Stardust& _dust, Rng& _rng) noexcept;
void MoveStardustAstern(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math,
                        const FlightState& _flight, Stardust& _dust, Rng& _rng) noexcept;
void MoveStardustSideways(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math,
                          FlightState& _flight, Stardust& _dust, Rng& _rng,
                          std::uint8_t _view) noexcept;

/*
 * 6502: STARS -- pick one of the three by the view.
 *
 * `STARS2` takes the view in X because it has to know left from right; the other two do not care.
 */
void MoveStardust(Canvas& _canvas, DrawWorkspace& _draw, MathWorkspace& _math,
                  FlightState& _flight, Stardust& _dust, Rng& _rng, std::uint8_t _view) noexcept;

} // namespace Elite
