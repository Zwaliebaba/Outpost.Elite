// Outpost/DockedMarket.h
#pragma once

#include "Canvas.h"
#include "DockedFrame.h"
#include "MarketView.h"

#include <optional>
#include <string>

namespace Outpost
{

/// D01, the market (ADR-058, D82, `Design/Canvas/docked/01-market.png`), drawn from the game's MarketView in the docked frame,
/// in 1920 x 1080 design units: the 17 commodities, the selected row, the Selected panel with the quantity and the trades, the
/// cargo hold's bar, the message strip and the footer. The controls a click lands on are found here (D83), and the game told
/// which one (Elite::MarketClick).
class DockedMarket
{
public:
  /// The market of _view, with its frame, into _canvas: the control under _pointer, if any, shown as hovered.
  static void Draw(const Elite::MarketView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                   Engine::Canvas& _canvas);

  /// The control of the market at _point, or nothing: a row, −, +, MAX, [B] BUY or [S] SELL. The key bar is the frame's
  /// (DockedFrame::KeyAt).
  [[nodiscard]] static std::optional<Elite::MarketClick> ControlAt(Engine::Point _point) noexcept;

  /// What the message strip says for _message, as D11's Market column words it: a headline, and the figures after its dash
  /// as a second line, or none.
  struct Words
  {
    std::wstring headline;
    std::wstring detail;
  };

  [[nodiscard]] static Words WordsOf(const Elite::MarketMessage& _message);
};

} // namespace Outpost
