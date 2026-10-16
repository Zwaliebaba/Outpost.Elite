// Outpost/DockedStatus.h
#pragma once

#include "Canvas.h"
#include "DockedFrame.h"
#include "StatusView.h"

#include <optional>
#include <string>

namespace Outpost
{

/// D08, the commander's status (ADR-063, D82, D75, `Design/Canvas/docked/08-commander-status.png`), drawn from the game's
/// StatusView in the docked frame, in 1920 x 1080 design units: the commander and their standing, the equipment fitted with
/// each laser's mounts, and the cargo with the hold's tonnes.
class DockedStatus
{
public:
  /// The status of _view, with its frame, into _canvas: the key under _pointer, if any, shown as hovered.
  static void Draw(const Elite::StatusView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                   Engine::Canvas& _canvas);

  /// _item as the equipment list words it: Missiles (3), Pulse Laser (front), Beam Laser (rear, left).
  [[nodiscard]] static std::wstring ItemText(const Elite::FittedItem& _item);
};

} // namespace Outpost
