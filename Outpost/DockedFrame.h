// Outpost/DockedFrame.h
#pragma once

#include "Canvas.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Outpost
{

/// The keys of the docked screens' bar (D82): Esc and F1 to F6, left to right.
enum class DockedKey : std::uint8_t
{
  Disc,
  Launch,
  Market,
  Equip,
  Galaxy,
  Local,
  Status,
};

/// What a docked screen's text is measured by, in design units: the renderer's DirectWrite (Engine::Renderer::MeasureText).
using TextMeasure = std::function<float(const Engine::CanvasText&)>;

/// The frame every docked screen shares (D82's common frame, T1), in 1920 x 1080 design units, read off `Design/Canvas/docked/`:
/// the background and its grid, the top bar with where the ship is, the screen's title on its tab between two rules, the
/// credits, and the key bar with the open screen's key lit; and the widgets the screens are built of: a panel, a table row, a
/// message strip and a modal.
class DockedFrame
{
public:
  /// What the frame shows.
  struct Desc
  {
    std::wstring location; ///< DOCKED · CORIOLIS STATION · LAVE
    std::wstring title;    ///< LAVE MARKET
    std::uint32_t creditsTenths = 0;
    DockedKey lit = DockedKey::Market;
  };

  /// A table row's look (D82): every second row striped, the selected row lit, and a row under the pointer shaded.
  enum class RowLook : std::uint8_t
  {
    Plain,
    Striped,
    Hovered,
    Selected,
  };

  /// A message strip's kind (D82): a refusal in red with !, a success in amber with ✓.
  enum class Tone : std::uint8_t
  {
    Refusal,
    Success,
  };

  /// The frame of _desc into _canvas, under the screen, which draws over it; the key under _pointer, if any, shown as hovered.
  static void Draw(const Desc& _desc, std::optional<Engine::Point> _pointer, const TextMeasure& _measure, Engine::Canvas& _canvas);

  /// The key of the bar at _point, or nothing.
  [[nodiscard]] static std::optional<DockedKey> KeyAt(Engine::Point _point) noexcept;

  /// A panel: the dark fill, the 1-unit amber border at 35%, and the 24-unit chamfer at its top-right corner and, when
  /// _bottomLeft, at its bottom-left.
  static void Panel(const Engine::Rect& _rect, bool _topRight, bool _bottomLeft, Engine::Canvas& _canvas);

  /// The look of row _index of a table: Selected when _selected, Hovered when under the pointer, otherwise Striped for every
  /// second row of a striped table (D01, D02) and Plain for the rest.
  [[nodiscard]] static RowLook RowLookOf(std::size_t _index, bool _selected, bool _hovered, bool _striped) noexcept;

  /// A table row's fill in _rect for _look; a plain row has none, and the table's panel shows through.
  static void Row(const Engine::Rect& _rect, RowLook _look, Engine::Canvas& _canvas);

  /// A row's text for _look: light amber on the selected row, the text's colour on every other.
  [[nodiscard]] static std::uint32_t RowText(RowLook _look) noexcept;

  /// A modal in _rect over the screen drawn so far (D09, D10): the whole screen dimmed, and a panel with a full amber border and
  /// 28-unit chamfers at its top-right and bottom-left corners. The modal's own content is drawn over it.
  static void Modal(const Engine::Rect& _rect, Engine::Canvas& _canvas);

  /// A message strip in _rect: its icon, and _headline, with _detail on a second line when it is not empty.
  static void Strip(const Engine::Rect& _rect, Tone _tone, std::wstring_view _headline, std::wstring_view _detail, Engine::Canvas& _canvas);

  /// _tenths of a credit as the screens print them: 1250.0.
  [[nodiscard]] static std::wstring Tenths(std::uint32_t _tenths);

  /// Whether _point is inside _rect.
  [[nodiscard]] static bool Contains(const Engine::Rect& _rect, Engine::Point _point) noexcept;
};

} // namespace Outpost
