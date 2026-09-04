#include "pch.h"

#include "Lasers.h"

#include "Dashboard.h"

#include "EliteTypes.h"

namespace Elite
{

  /// 6502: DENGY -- declared here rather than through `FlightLoop.h`, which includes this file.
  [[nodiscard]] bool DrainEnergy(FlightStatus& _status) noexcept;

  namespace
  {
    /*
     * 6502: las -- two lines from one corner pair to the convergence point.
     *
     * Reached twice, and the second time by falling into it rather than by a `JSR`, which is why the
     * arguments arrive in A and Y rather than on the stack: `LDA #32 / LDY #224 / JSR las` and then
     * `LDA #48 / LDY #208` running straight on.
     */
    bool DrawLaserPair(Canvas& _canvas, DrawWorkspace& _draw, const LaserBurst& _burst, std::uint8_t _left, std::uint8_t _right) noexcept
    {
      _draw.x2 = _left;                   // 6502: STA X2
      _draw.x1 = _burst.x;                // 6502: LDA LASX / STA X1
      _draw.y1 = _burst.y;                // 6502: LDA LASY / STA Y1
      _draw.y2 = 2u * VIEW_CENTRE_Y - 1u; // 6502: LDA #2*Y-1 / STA Y2
      DrawLine(_canvas, _draw);           // 6502: JSR LL30

      _draw.x1 = _burst.x;
      _draw.y1 = _burst.y;
      _draw.x2 = _right; // 6502: STY X2
      _draw.y2 = 2u * VIEW_CENTRE_Y - 1u;
      DrawLine(_canvas, _draw); // 6502: JMP LL30 -- a tail call

      return false;
    }
  } // namespace

  bool DrawLaserLines(Canvas& _canvas, DrawWorkspace& _draw, const LaserBurst& _burst, std::uint8_t _view) noexcept
  {
    // 6502: LASLI2 -- LDA QQ11 / BNE LASLI-1, and that is the previous routine's RTS borrowed.
    if (_view != 0u)
    {
      return false;
    }

    // 6502: LDA #32 / LDY #224 / JSR las, then LDA #48 / LDY #208 falling into it again.
    (void)DrawLaserPair(_canvas, _draw, _burst, 32u, 224u);
    return DrawLaserPair(_canvas, _draw, _burst, 48u, 208u);
  }

  bool FireLaser(Canvas& _canvas, DrawWorkspace& _draw, Rng& _rng, LaserBurst& _burst, FlightStatus& _status, std::uint8_t _view,
                 bool _carryIn) noexcept
  {
    /*
     * 6502: JSR DORND / AND #7 / ADC #Y-4 / STA LASY.
     *
     * `AND` does not touch the carry, so what `ADC` adds is `DORND`'s exit carry -- the beam's
     * convergence point is one pixel further down on half the frames for no reason the coordinate
     * itself explains.
     */
    const RngResult down = _rng.Next(_carryIn);
    const AddResult y = AddWithCarry(static_cast<std::uint8_t>(down.value & 7u), static_cast<std::uint8_t>(VIEW_CENTRE_Y - 4u), down.carry);
    _burst.y = y.value;

    // 6502: JSR DORND / AND #7 / ADC #X-4 / STA LASX -- the same again, across.
    const RngResult across = _rng.Next(y.carry);
    const AddResult x =
      AddWithCarry(static_cast<std::uint8_t>(across.value & 7u), static_cast<std::uint8_t>(VIEW_CENTRE_X - 4u), across.carry);
    _burst.x = x.value;

    /*
     * 6502: LDA GNTMP / ADC #8 / STA GNTMP -- and this one runs on the carry the line above left,
     * which is ALWAYS CLEAR: `AND #7` plus 124 plus at most one is 132, and that cannot carry out
     * of a byte. So a shot costs exactly eight, and this uncleared `ADC` is the constant kind while
     * the two above it are not (§6.65's split, and §6.68 measured it).
     */
    _status.laserTemperature = AddWithCarry(_status.laserTemperature, LASER_HEAT_PER_SHOT, x.carry).value;

    // 6502: JSR DENGY -- built in 3d-d-iii-b, so this is no longer a seam.
    (void)DrainEnergy(_status);

    // 6502: and no RTS -- LASLI runs straight on into LASLI2.
    return DrawLaserLines(_canvas, _draw, _burst, _view);
  }

} // namespace Elite
