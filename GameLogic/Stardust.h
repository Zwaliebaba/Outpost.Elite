#pragma once

#include "Arith.h"
#include "Canvas.h"
#include "Picture.h"
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
    std::array<std::uint8_t, STARDUST_SLOTS> x{};
    std::array<std::uint8_t, STARDUST_SLOTS> xLow{};
    std::array<std::uint8_t, STARDUST_SLOTS> y{};
    std::array<std::uint8_t, STARDUST_SLOTS> yLow{};
    std::array<std::uint8_t, STARDUST_SLOTS> z{};
    std::array<std::uint8_t, STARDUST_SLOTS> zLow{};

    /*
     * How many specks there are, and it is only ever `NOST` or 3.
     *
     * `NOST` is **12** in this build, which the layout agrees with: `SX` is at 1698 and `SXL` at
     * 1711. The upstream comments beside the two writes to `NOSTM` say "the maximum allowed
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
     * Zero page 186, one intermediate the side views keep across the loop body.
     *
     * `STARS2` stores the divide's quotient here and compares against it thirty instructions later,
     * by which time nothing else still holds it. It is the only byte of the movers' scratch that is
     * genuinely the stardust's: `XX` and `YY` looked like it and are not (§6.45) -- they are the
     * sun's, and are `EDGES`'s parameter and `SUN`'s locals since M2-c.
     */
    std::uint8_t keptQuotient = 0;
  };

  // ---- the wrappers, which are one or two instructions and a fall-through -------------------
  //
  // Seven routines in the ledger, and none of them is more than four bytes of setup before it runs
  // into something already ported. Four are functions here because each is a named entry point the
  // oracle can be called at (§6.20); the other three -- `MLS2`, `MUT1` and `MUT2` -- were nothing
  // but "(S R) = XX(1 0)" in front of a multiply, and since M2-b the multiply takes its addend as a
  // value, so they are the `SignMag16{xx, xxNext}` at each of their call sites in the movers.

  /// Q = A, then (P R) = DELTA / Q through `DVID4`: the whole part and the fraction,
  /// and the fraction is what comes back in A.
  [[nodiscard]] ScaledDivision DivideSpeedBy(const FlightState& _flight, std::uint8_t _divisor) noexcept;

  /// The same for a particle's own distance.
  [[nodiscard]] ScaledDivision DivideSpeedByDistance(const FlightState& _flight, const Stardust& _dust, std::uint8_t _at) noexcept;

  /// Y1 = SY, then (A P) = |SY| * Q through `MLU2`. The carry is part of the answer:
  /// the front view adds `SYL` to it and the rear view subtracts, neither with a `CLC` or a `SEC`.
  /// The `Y1` it stages is the speck's own height, which the movers read from the speck (M2-c).
  [[nodiscard]] Product MultiplyByHeight(const Stardust& _dust, std::uint8_t _at, std::uint8_t _multiplier) noexcept;

  /// P = ALP1, then `MULTS`. (`MULTS-2` is the same without the `LDX`, reached by
  /// the movers with the multiplier already in X -- which is `MultiplyScaled` called directly.)
  [[nodiscard]] Product MultiplyByRoll(const FlightState& _flight, std::uint8_t _value) noexcept;

  /*
   * `ADD`, keep the answer as the particle's new y, and plot it.
   *
   * `_value` is (A P) and `_addend` is (S R), as `ADD` takes them: the movers hand it the pitch
   * or the roll over a zero low byte, and the particle's y. `_across`, `_down` and `_distance` are
   * the `X1`, `Y1` and `ZZ` the mover had staged, which is the speck's OLD position -- `PIXEL2`
   * plots by EOR, so this is the erase. Returns the sum's high byte, `YY+1`, which the mover reads.
   *
   * The ledger marked this ported with the rest of the pixel routines in slice 1d-a and it was not
   * (§6.41): it writes `SYL`, and the stardust arrays did not exist then.
   *
   * `_picture`, `_acrossLow` AND `_downLow` WERE HERE AND ARE NOT ANY MORE (RN-1). The two
   * fractions were `SXL` and `SYL` as they were when the mark being erased was DRAWN -- which is
   * not what the arrays hold by the time this is called -- and they existed solely so the wide
   * mark could be rubbed out at the half-pixel the faithful byte could not express
   * (Resolution.md §4.3). The frame is cleared at every present now, so there is no wide mark to
   * rub out: this plot is the canvas's alone, and the second plot at the speck's NEW position is
   * the one the frame keeps. `check_twins.py`'s `ERASE_NEEDS_NO_TWIN` holds it to that.
   */
  [[nodiscard]] std::uint8_t PlotStardust(Canvas& _canvas, Stardust& _dust, std::uint8_t _at, SignMag16 _value, SignMag16 _addend,
                                          std::uint8_t _across, std::uint8_t _down, std::uint8_t _distance) noexcept;

  /*
   * Swap every speck's x and y, which reflects the whole field in the line x = y,
   * and redraw it.
   *
   * `LOOK1` calls it when the player changes view: it is a cheap way of making the field feel
   * different without generating a new one, and if you watch closely when you switch views you can
   * see that the new dust is the old dust mirrored in the diagonal. `LOOK1` is slice 3d's; this is
   * here because the workspace is.
   */
  void FlipStardust(Canvas& _canvas, Stardust& _dust, Picture* _picture = nullptr) noexcept;

  /*
   * STARS1, STARS2 and STARS6 -- move and redraw the whole field, once per frame.
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
  void MoveStardustAhead(Canvas& _canvas, const FlightState& _flight, Stardust& _dust, Rng& _rng, Picture* _picture = nullptr) noexcept;
  void MoveStardustAstern(Canvas& _canvas, const FlightState& _flight, Stardust& _dust, Rng& _rng, Picture* _picture = nullptr) noexcept;
  void MoveStardustSideways(Canvas& _canvas, FlightState& _flight, Stardust& _dust, Rng& _rng, std::uint8_t _view,
                            Picture* _picture = nullptr) noexcept;

  /*
   * Pick one of the three by the view.
   *
   * `STARS2` takes the view in X because it has to know left from right; the other two do not care.
   */
  void MoveStardust(Canvas& _canvas, FlightState& _flight, Stardust& _dust, Rng& _rng, std::uint8_t _view,
                    Picture* _picture = nullptr) noexcept;

} // namespace Elite
