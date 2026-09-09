#include "pch.h"

#include "Lasers.h"

#include "Dashboard.h"

#include "EliteTypes.h"
#include "Lines2x.h"

namespace Elite
{

  /// Declared here rather than through `FlightLoop.h`, which includes this file.
  [[nodiscard]] bool DrainEnergy(FlightStatus& _status) noexcept;

  namespace
  {
    /*
     * Two lines from one corner pair to the convergence point.
     *
     * Reached twice, and the second time by falling into it rather than by a `JSR`, which is why the
     * arguments arrive in A and Y rather than on the stack: the first pair is loaded and CALLED,
     * and the second is loaded and runs straight on.
     */
    bool DrawLaserPair(Canvas& _canvas, const LaserBurst& _burst, std::uint8_t _left, std::uint8_t _right, Picture* _picture) noexcept
    {
      Line beam;
      beam.x2 = _left;                   // X2 from A
      beam.x1 = _burst.x;
      beam.y1 = _burst.y;
      beam.y2 = 2u * VIEW_CENTRE_Y - 1u; // The bottom of the view
      (void)DrawLine(_canvas, beam);
      if (DrawingTwins(_picture))
      {
        // The beam's ends are eight-bit view coordinates the routine states outright -- the corner
        // is a literal and the convergence point is `LASX`/`LASY` -- so the wide beam is those
        // numbers doubled, and it gains a single-pixel stroke rather than a new geometry.
        DrawLine2x(*_picture, beam);
      }

      beam.x1 = _burst.x;
      beam.y1 = _burst.y;
      beam.x2 = _right; // X2 from Y this time
      beam.y2 = 2u * VIEW_CENTRE_Y - 1u;
      (void)DrawLine(_canvas, beam); // LL30 again, as a tail call
      if (DrawingTwins(_picture))
      {
        DrawLine2x(*_picture, beam);
      }

      return false;
    }
  } // namespace

  bool DrawLaserLines(Canvas& _canvas, const LaserBurst& _burst, std::uint8_t _view, Picture* _picture) noexcept
  {
    // A non-zero view leaves through the previous routine's `RTS`, borrowed.
    if (_view != 0u)
    {
      return false;
    }

    // The first corner pair called, then the second falling into it again.
    (void)DrawLaserPair(_canvas, _burst, 32u, 224u, _picture);
    return DrawLaserPair(_canvas, _burst, 48u, 208u, _picture);
  }

  bool FireLaser(Canvas& _canvas, Rng& _rng, LaserBurst& _burst, FlightStatus& _status, std::uint8_t _view, bool _carryIn,
                 Picture* _picture) noexcept
  {
    /*
     * A random byte masked to three bits, added to the centre less four, into `LASY`.
     *
     * The mask does not touch the carry, so what the addition adds is `DORND`'s exit carry -- the
     * beam's convergence point is one pixel further down on half the frames for no reason the
     * coordinate itself explains.
     */
    const RngResult down = _rng.Next(_carryIn);
    const AddResult y = AddWithCarry(static_cast<std::uint8_t>(down.value & 7u), static_cast<std::uint8_t>(VIEW_CENTRE_Y - 4u), down.carry);
    _burst.y = y.value;

    // The same again, across, into `LASX`.
    const RngResult across = _rng.Next(y.carry);
    const AddResult x =
      AddWithCarry(static_cast<std::uint8_t>(across.value & 7u), static_cast<std::uint8_t>(VIEW_CENTRE_X - 4u), across.carry);
    _burst.x = x.value;

    /*
     * Eight added to `GNTMP` -- and this one runs on the carry the line above left, which is
     * ALWAYS CLEAR: three bits plus 124 plus at most one is 132, and that cannot carry out of a
     * byte. So a shot costs exactly eight, and this uncleared addition is the constant kind while
     * the two above it are not (§6.65's split, and §6.68 measured it).
     */
    _status.laserTemperature = AddWithCarry(_status.laserTemperature, LASER_HEAT_PER_SHOT, x.carry).value;

    // Built in 3d-d-iii-b, so this is no longer a seam.
    (void)DrainEnergy(_status);

    // And no RTS -- LASLI runs straight on into LASLI2.
    return DrawLaserLines(_canvas, _burst, _view, _picture);
  }

} // namespace Elite
