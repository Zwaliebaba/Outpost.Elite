// Outpost/DockedEquip.h
#pragma once

#include "Canvas.h"
#include "DockedFrame.h"
#include "EquipView.h"

#include <optional>
#include <string>

namespace Outpost
{

/// D02 and D03, the shipyard and its mount picker (ADR-064, D82, `Design/Canvas/docked/02-…` and `03-…`), drawn from the game's
/// EquipView in the docked frame, in 1920 x 1080 design units: the items on sale with their prices and what is fitted, the selected
/// row, the Selected panel with the trades, the picker over the right-hand column while it is open, the message strip and the
/// footer. The controls a click lands on are found here (D83), and the game told which one (Elite::EquipClick).
class DockedEquip
{
public:
  /// The shipyard of _view, with its frame, into _canvas: the control under _pointer, if any, shown as hovered.
  static void Draw(const Elite::EquipView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                   Engine::Canvas& _canvas);

  /// The control of the shipyard of _view at _point, or nothing: a row, [B] BUY or [S] SELL, and with the picker open a mount or
  /// ESC CANCEL. The key bar is the frame's (DockedFrame::KeyAt).
  [[nodiscard]] static std::optional<Elite::EquipClick> ControlAt(const Elite::EquipView& _view, Engine::Point _point) noexcept;

  /// What the message strip says for _message, as D11's Equipment column words it: a headline, and what follows its dash as a
  /// second line, or none.
  struct Words
  {
    std::wstring headline;
    std::wstring detail;
  };

  [[nodiscard]] static Words WordsOf(const Elite::EquipMessage& _message);

  /// The text of the Fitted column for row _row of _view, from 1: 7.0 LY, 3 / 4, a laser's mounts, FITTED, or a dash for none.
  [[nodiscard]] static std::wstring FittedText(const Elite::EquipView& _view, std::uint8_t _row);
};

} // namespace Outpost
