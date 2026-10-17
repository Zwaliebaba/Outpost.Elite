// Outpost/DockedDisc.h
#pragma once

#include "Canvas.h"
#include "DiscView.h"
#include "DockedFrame.h"

#include <optional>
#include <string>

namespace Outpost
{

/// D10, Disc access (ADR-067, D82, D102, `Design/Canvas/docked/10-disc-menu.png`), drawn from the game's DiscView as a modal in
/// the docked frame, in 1920 x 1080 design units: the menu of six items, and beside it the item's panel: the catalogue with each
/// file's saved day and cash, the name Save commander types, the input devices, or the question it asks; the strip, and the panel's
/// two buttons. The controls a click lands on are found here (D83), and the game told which one (Elite::DiscClick).
class DockedDisc
{
public:
  /// D10 of _view, with its frame, into _canvas: the control under _pointer, if any, shown as hovered.
  static void Draw(const Elite::DiscView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                   Engine::Canvas& _canvas);

  /// The control of D10 of _view at _point, or nothing: an item, a row of the panel's list, or one of its two buttons. The key bar
  /// is the frame's (DockedFrame::KeyAt).
  [[nodiscard]] static std::optional<Elite::DiscClick> ControlAt(const Elite::DiscView& _view, Engine::Point _point) noexcept;

  /// What the strip says for _view's note or question: a headline, and a second line or none.
  struct Words
  {
    std::wstring headline;
    std::wstring detail;
  };

  [[nodiscard]] static Words WordsOf(const Elite::DiscView& _view);

  /// The cash _tenths as the catalogue prints it, its thousands set apart: 23 418.6.
  [[nodiscard]] static std::wstring Credits(std::uint32_t _tenths);

  /// The day _file was saved, as the catalogue prints it: 14-10-87.
  [[nodiscard]] static std::wstring Saved(const Elite::DiscFile& _file);

  /// The first row of _view's list shown: the list scrolls to keep the selected row in sight.
  [[nodiscard]] static std::size_t FirstRowShown(const Elite::DiscView& _view) noexcept;
};

} // namespace Outpost
