// Outpost/Cockpit.h
#pragma once

#include "Canvas.h"
#include "FlightSnapshot.h"
#include "Renderer.h"

#include <cstdint>

namespace Outpost
{

/// The cockpit of the canvas design (ADR-034, D63, `Design/Canvas/cockpit-hud-overlay/`): the frame round the view, the message
/// line in the top bar, the gauges, the scanner, the condition light and the compass, in 1920 x 1080 design units, drawn from a
/// frame's dashboard. Until the owner draws them (D54), the ECM's E, the missiles' armed and locked states and the other views'
/// names are placeholders in the design's style.
class Cockpit
{
public:
  /// The space view's place in the frame's window: the 1987 field of view, 256 x 126 pixels at the CGA's 4:3, about 1.69:1,
  /// filling the window's height and centred across (D63).
  static constexpr Engine::Rect VIEW{.left = 308.0f, .top = 40.0f, .width = 1304.0f, .height = 770.0f};

  /// The cockpit of _dashboard into _canvas, over the view. _milliseconds is paced time, which a flashing light flashes by;
  /// _renderer measures text.
  static void Draw(const Elite::SnapshotDashboard& _dashboard, std::uint64_t _milliseconds, Engine::Renderer& _renderer,
                   Engine::Canvas& _canvas);
};

} // namespace Outpost
