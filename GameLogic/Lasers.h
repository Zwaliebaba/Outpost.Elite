#pragma once

#include <cstdint>

#include "Canvas.h"
#include "Dashboard.h"
#include "Rng.h"

namespace Elite
{

  /*
   * The player's laser (slice 3d-c).
   *
   * Four lines from the corners of the view to a point that jitters near the centre, drawn with
   * EOR so the next frame takes them off again. The jitter is what makes the beam look alive, and
   * it is two random bytes -- which is why this routine is where the laser's heat comes from too:
   * the same instruction stream that picks the endpoint adds eight to the laser temperature.
   */

  /// 6502: X and Y -- the centre of the 256 x 144 space view, which every laser line converges on.
  inline constexpr std::uint8_t VIEW_CENTRE_X = 128;
  inline constexpr std::uint8_t VIEW_CENTRE_Y = 72;

  /// 6502: LDA #8 / ADC GNTMP -- what one shot costs in laser heat.
  inline constexpr std::uint8_t LASER_HEAT_PER_SHOT = 8;

  /// 6502: LASX and LASY -- where the beams converge this frame, which persists so the next frame
  /// can rub the same lines out.
  struct LaserBurst
  {
    std::uint8_t x = 0; ///< 6502: LASX
    std::uint8_t y = 0; ///< 6502: LASY
  };

  /// What `LASLI` reaches outside this slice.
  /*
   * 6502: LASLI -- fire: pick the convergence point, heat the laser, drain the banks, draw.
   *
   * THREE UNCLEARED ADDS IN NINE INSTRUCTIONS, and §6.65's split runs right through them. The two
   * coordinates read `DORND`'s exit carry, which the generator decides -- so the convergence point
   * spans NINE rows and nine columns where `AND #7` alone would give eight, and a sweep counting
   * distinct values is what proves it. The third, `LDA GNTMP / ADC #8`, reads the carry the second
   * coordinate left, and that one cannot be set: `AND #7` plus 124 plus at most one is 132. So a
   * shot costs exactly eight heat, and the port asserts that rather than assuming it (§6.68).
   *
   * Returns the exit carry, because `LL30` leaves one and the flight loop's next instruction is an
   * `ADC`. The port returns it rather than guessing.
   */
  [[nodiscard]] bool FireLaser(Canvas& _canvas, DrawWorkspace& _draw, Rng& _rng, LaserBurst& _burst, FlightStatus& _status,
                               std::uint8_t _view, bool _carryIn) noexcept;

  /*
   * 6502: LASLI2 -- draw the four lines without firing, which is how the beam is rubbed out.
   *
   * `LDA QQ11 / BNE LASLI-1` -- and `LASLI-1` is the byte before the routine, which is the previous
   * one's `RTS` borrowed as a branch target. So a chart on screen means no laser at all.
   */
  [[nodiscard]] bool DrawLaserLines(Canvas& _canvas, DrawWorkspace& _draw, const LaserBurst& _burst, std::uint8_t _view) noexcept;

} // namespace Elite
