// Outpost/DockedChart.h
#pragma once

#include "Canvas.h"
#include "ChartView.h"
#include "DockedFrame.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Outpost
{

/// D04 to D07, the galactic and the short-range chart (ADR-065, D82, `Design/Canvas/docked/04-…` to `07-…`), drawn from the game's
/// ChartView in the docked frame, in 1920 x 1080 design units: the systems, the current one at the centre of the fuel ring, the
/// cursor, the target bracketed with the line to it, the legend, Find, the countdown's lock in flight, the footer, and Data on
/// System as the side panel. A click or a drag on the chart is found here as a point in the chart's units (D99, D109), which the
/// game is sent (Elite::ChartClick).
class DockedChart
{
public:
  /// The chart of _view, with its frame, into _canvas: the key under _pointer, if any, shown as hovered.
  static void Draw(const Elite::ChartView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                   Engine::Canvas& _canvas);

  /// The point of the chart of _kind at _point, in its units, held to them: on the galactic chart a system's x, 0 to 255, and its
  /// row, 0 to 127; on the short-range chart 3.5 to a galaxy unit about the current system, at 80, 64. Nothing when _point is off
  /// the chart's panel.
  [[nodiscard]] static std::optional<Elite::ChartClick> ChartAt(Elite::ChartKind _kind, Engine::Point _point) noexcept;

  /// Where the point _x, _row of the chart of _kind, in its units, is drawn.
  [[nodiscard]] static Engine::Point PlaceOf(Elite::ChartKind _kind, float _x, float _row) noexcept;

  /// The scan code a click on _key sends while a chart is shown in flight (D110): the flight's own key for the screen the bar names,
  /// F1 the front view, F8 the market, F5 and F6 the charts and F9 the status screen; nothing for ESC DISC and F3 EQUIP.
  [[nodiscard]] static std::optional<std::uint8_t> FlightScanCodeOf(DockedKey _key) noexcept;

  /// What the side panel's badge says of _view's target: `IN RANGE`, `OUT OF RANGE · 1.3 LY SHORT`, `LOCKED · IN RANGE`, or
  /// `CURRENT SYSTEM` for the system the ship is at.
  [[nodiscard]] static std::wstring BadgeOf(const Elite::ChartView& _view);

  /// _value with its thousands set apart by a space, as Data on System prints productivity and radius: 16 900.
  [[nodiscard]] static std::wstring Thousands(std::uint32_t _value);
};

} // namespace Outpost
