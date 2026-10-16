#include "pch.h"

#include "DockedFrame.h"

#include <array>
#include <format>

namespace Outpost
{

namespace
{

// The palette of D82's common frame, 0xAARRGGBB, display-encoded, read off `docked/`.
constexpr std::uint32_t BACKGROUND = 0xFF070605;
constexpr std::uint32_t GRID = 0xFF0D0A06;
constexpr std::uint32_t BAR_TOP = 0xFF0B0A09;
constexpr std::uint32_t BAR_BOTTOM = 0xFF1B1E21;
constexpr std::uint32_t TAB_TOP = 0xFF1D2125;
constexpr std::uint32_t TAB_BOTTOM = 0xFF58616C;
constexpr std::uint32_t KEY_TAB_TOP = 0xFF2D333A;
constexpr std::uint32_t KEY_TAB_BOTTOM = 0xFF141719;
constexpr std::uint32_t KEY_BAR_TOP = 0xFF131518;
constexpr std::uint32_t KEY_BAR_BOTTOM = 0xFF0C0D0E;
constexpr std::uint32_t AMBER = 0xFFFFB43C;
constexpr std::uint32_t AMBER_LIGHT = 0xFFFFCF80;
constexpr std::uint32_t TEXT = 0xFFF3E6CF;
constexpr std::uint32_t LABEL = 0xFFC9BCA6;
constexpr std::uint32_t ALERT = 0xFFFF5A2C;
constexpr std::uint32_t ALERT_LIGHT = 0xFFFFB09A;
constexpr std::uint32_t PANEL_FILL = 0xFF110F0C;
constexpr std::uint32_t PANEL_EDGE = 0xFF64491D; // amber at 35% over the background
constexpr std::uint32_t MODAL_FILL = 0xFF14110D;
constexpr std::uint32_t MODAL_BACKDROP = 0xC7050403; // the screen under a modal, dimmed to 22% (D09, D10)
constexpr std::uint32_t ROW_STRIPE = 0xFF161411;
constexpr std::uint32_t ROW_SELECTED = 0xFF453416;
constexpr std::uint32_t ROW_HOVER = 0xFF221C14;
constexpr std::uint32_t KEY_FILL = 0xFF0C0E10;
constexpr std::uint32_t KEY_EDGE = 0xFF49371B;
constexpr std::uint32_t KEY_NAME = 0xFF917449;
constexpr std::uint32_t KEY_LIT_FILL = 0xFF3E321F;
constexpr std::uint32_t KEY_HOVER_FILL = 0xFF1C1A16;
constexpr std::uint32_t KEY_HOVER_EDGE = 0xFF886124;
constexpr std::uint32_t STRIP_ALERT_FILL = 0xFF29160F;
constexpr std::uint32_t STRIP_SUCCESS_FILL = 0xFF291F11;
constexpr std::uint32_t STRIP_MARK = 0xFF2E160A; // the ! and the ✓ on their square
constexpr std::uint32_t DOT_GLOW = 0x30C9BCA6;

// The frame's measures, in design units.
constexpr float BAR_HEIGHT = 44.0f;
constexpr float TAB_BOTTOM_Y = 72.0f;
constexpr float TAB_TOP_LEFT = 702.0f;
constexpr float TAB_TOP_RIGHT = 1218.0f;
constexpr float TAB_SLANT = 28.0f;
constexpr float TITLE_Y = 32.0f;
constexpr float TITLE_SIZE = 24.0f;
constexpr float TITLE_SPACING = 8.0f;
constexpr float TITLE_RULE = 40.0f;     // each rule's length
constexpr float TITLE_RULE_GAP = 18.0f; // between a rule and the title
constexpr float STATUS_Y = 20.0f;
constexpr float CREDITS_RIGHT = 1777.0f;
constexpr float CREDITS_LABEL_LEFT = 1790.0f;
constexpr float GRID_PITCH = 48.0f;
constexpr float GRID_FIRST_ROW = 23.0f;
constexpr float KEY_TAB_TOP_Y = 984.0f;
constexpr float KEY_BAR_TOP_Y = 1006.0f;
constexpr float KEY_TAB_LEFT = 450.0f;
constexpr float KEY_TAB_RIGHT = 1370.0f;
constexpr float KEY_TAB_SLANT = 22.0f;
constexpr float KEY_HEIGHT = 60.0f;
constexpr float CHAMFER = 24.0f;
constexpr float MODAL_CHAMFER = 28.0f;
constexpr float DESIGN_WIDTH = 1920.0f;
constexpr float DESIGN_HEIGHT = 1080.0f;
constexpr float STRIP_ICON = 26.0f;
constexpr float STRIP_ICON_LEFT = 17.0f;
constexpr float STRIP_TEXT_LEFT = 58.0f;

// The bar's keys: where each is across, its key and its name.
struct KeyPlace
{
  float left;
  float width;
  const wchar_t* key;
  const wchar_t* name;
};

constexpr std::array<KeyPlace, 7> KEYS = {{
  {460.0f, 86.0f, L"ESC", L"DISC"},
  {554.0f, 128.0f, L"F1", L"LAUNCH"},
  {690.0f, 127.0f, L"F2", L"MARKET"},
  {825.0f, 128.0f, L"F3", L"EQUIP"},
  {961.0f, 128.0f, L"F4", L"GALAXY"},
  {1097.0f, 127.0f, L"F5", L"LOCAL"},
  {1232.0f, 128.0f, L"F6", L"STATUS"},
}};

[[nodiscard]] Engine::Rect KeyRect(const KeyPlace& _place) noexcept
{
  return Engine::Rect{.left = _place.left, .top = KEY_BAR_TOP_Y, .width = _place.width, .height = KEY_HEIGHT};
}

// The four edges of _rect, 1 unit wide, inside it.
void Edges(const Engine::Rect& _rect, std::uint32_t _color, Engine::Canvas& _canvas)
{
  _canvas.BoxEdge(
    Engine::Rect{.left = _rect.left + 0.5f, .top = _rect.top + 0.5f, .width = _rect.width - 1.0f, .height = _rect.height - 1.0f}, 1.0f,
    _color);
}

// A panel in _rect: _fill, a 1-unit border of _edge, and a chamfer of _chamfer at its top-right corner when _topRight and its
// bottom-left when _bottomLeft.
void ChamferedPanel(const Engine::Rect& _rect, float _chamfer, std::uint32_t _fill, std::uint32_t _edge, bool _topRight, bool _bottomLeft,
                    Engine::Canvas& _canvas)
{
  const float left = _rect.left;
  const float top = _rect.top;
  const float right = _rect.left + _rect.width;
  const float bottom = _rect.top + _rect.height;
  const float upper = _topRight ? top + _chamfer : top;
  const float lower = _bottomLeft ? bottom - _chamfer : bottom;
  // The fill: the body, then the bands the chamfers cut.
  _canvas.Box(Engine::Rect{.left = left, .top = upper, .width = _rect.width, .height = lower - upper}, _fill);
  if (_topRight)
  {
    _canvas.Quad({Engine::Point{.x = left, .y = top}, Engine::Point{.x = right - _chamfer, .y = top}, Engine::Point{.x = right, .y = upper},
                  Engine::Point{.x = left, .y = upper}},
                 _fill, _fill);
  }
  if (_bottomLeft)
  {
    _canvas.Quad({Engine::Point{.x = left, .y = lower}, Engine::Point{.x = right, .y = lower}, Engine::Point{.x = right, .y = bottom},
                  Engine::Point{.x = left + _chamfer, .y = bottom}},
                 _fill, _fill);
  }
  // The border, on the half unit inside each edge, and across each chamfer.
  const float inLeft = left + 0.5f;
  const float inTop = top + 0.5f;
  const float inRight = right - 0.5f;
  const float inBottom = bottom - 0.5f;
  _canvas.Line(Engine::Point{.x = inLeft, .y = inTop}, Engine::Point{.x = _topRight ? right - _chamfer : inRight, .y = inTop}, 1.0f, _edge);
  _canvas.Line(Engine::Point{.x = inRight, .y = upper}, Engine::Point{.x = inRight, .y = inBottom}, 1.0f, _edge);
  _canvas.Line(Engine::Point{.x = _bottomLeft ? left + _chamfer : inLeft, .y = inBottom}, Engine::Point{.x = inRight, .y = inBottom}, 1.0f,
               _edge);
  _canvas.Line(Engine::Point{.x = inLeft, .y = inTop}, Engine::Point{.x = inLeft, .y = lower}, 1.0f, _edge);
  if (_topRight)
  {
    _canvas.Line(Engine::Point{.x = right - _chamfer, .y = inTop}, Engine::Point{.x = inRight, .y = upper}, 1.0f, _edge);
  }
  if (_bottomLeft)
  {
    _canvas.Line(Engine::Point{.x = inLeft, .y = lower}, Engine::Point{.x = left + _chamfer, .y = inBottom}, 1.0f, _edge);
  }
}

} // namespace

void DockedFrame::Draw(const Desc& _desc, std::optional<Engine::Point> _pointer, const TextMeasure& _measure, Engine::Canvas& _canvas)
{
  _canvas.Box(Engine::Rect{.left = 0.0f, .top = 0.0f, .width = Engine::DESIGN_WIDTH, .height = Engine::DESIGN_HEIGHT}, BACKGROUND);
  for (int column = 0; GRID_PITCH * static_cast<float>(column) < Engine::DESIGN_WIDTH; ++column)
  {
    const float x = GRID_PITCH * static_cast<float>(column) + 0.5f;
    _canvas.Line(Engine::Point{.x = x, .y = BAR_HEIGHT}, Engine::Point{.x = x, .y = KEY_BAR_TOP_Y}, 1.0f, GRID);
  }
  for (int row = 1; GRID_FIRST_ROW + GRID_PITCH * static_cast<float>(row) < KEY_BAR_TOP_Y; ++row)
  {
    const float y = GRID_FIRST_ROW + GRID_PITCH * static_cast<float>(row) + 0.5f;
    _canvas.Line(Engine::Point{.x = 0.0f, .y = y}, Engine::Point{.x = Engine::DESIGN_WIDTH, .y = y}, 1.0f, GRID);
  }

  // The top bar, and the title's tab under it.
  _canvas.Quad({Engine::Point{.x = 0.0f, .y = 0.0f}, Engine::Point{.x = Engine::DESIGN_WIDTH, .y = 0.0f},
                Engine::Point{.x = Engine::DESIGN_WIDTH, .y = BAR_HEIGHT}, Engine::Point{.x = 0.0f, .y = BAR_HEIGHT}},
               BAR_TOP, BAR_BOTTOM);
  _canvas.Quad({Engine::Point{.x = TAB_TOP_LEFT, .y = BAR_HEIGHT}, Engine::Point{.x = TAB_TOP_RIGHT, .y = BAR_HEIGHT},
                Engine::Point{.x = TAB_TOP_RIGHT - TAB_SLANT, .y = TAB_BOTTOM_Y},
                Engine::Point{.x = TAB_TOP_LEFT + TAB_SLANT, .y = TAB_BOTTOM_Y}},
               TAB_TOP, TAB_BOTTOM);
  const Engine::CanvasText title{.text = _desc.title,
                                 .font = Engine::Font::Label,
                                 .size = TITLE_SIZE,
                                 .spacing = TITLE_SPACING,
                                 .anchor = Engine::Point{.x = Engine::DESIGN_WIDTH / 2.0f, .y = TITLE_Y},
                                 .align = Engine::Align::Center,
                                 .color = AMBER_LIGHT};
  // The title's width, less the spacing after its last letter, which the rules do not count.
  const float half = (_measure(title) - TITLE_SPACING) / 2.0f;
  const float middle = Engine::DESIGN_WIDTH / 2.0f;
  _canvas.Line(Engine::Point{.x = middle - half - TITLE_RULE_GAP - TITLE_RULE, .y = TITLE_Y + 1.0f},
               Engine::Point{.x = middle - half - TITLE_RULE_GAP, .y = TITLE_Y + 1.0f}, 2.0f, AMBER);
  _canvas.Line(Engine::Point{.x = middle + half + TITLE_RULE_GAP, .y = TITLE_Y + 1.0f},
               Engine::Point{.x = middle + half + TITLE_RULE_GAP + TITLE_RULE, .y = TITLE_Y + 1.0f}, 2.0f, AMBER);
  _canvas.Text(title.text, title.font, title.size, title.spacing, title.anchor, title.align, title.color);

  // Where the ship is, and the credits.
  _canvas.Ellipse(Engine::Point{.x = 65.0f, .y = 21.0f}, 8.0f, 8.0f, DOT_GLOW);
  _canvas.Ellipse(Engine::Point{.x = 65.0f, .y = 21.0f}, 4.5f, 4.5f, LABEL);
  _canvas.Text(_desc.location, Engine::Font::Label, 17.0f, 4.0f, Engine::Point{.x = 83.0f, .y = STATUS_Y}, Engine::Align::Left, LABEL);
  _canvas.Text(Tenths(_desc.creditsTenths), Engine::Font::Number, 25.0f, 0.0f, Engine::Point{.x = CREDITS_RIGHT, .y = 19.5f},
               Engine::Align::Right, AMBER_LIGHT);
  _canvas.Text(L"CREDITS", Engine::Font::Label, 14.0f, 3.0f, Engine::Point{.x = CREDITS_LABEL_LEFT, .y = 23.0f}, Engine::Align::Left,
               LABEL);

  // The key bar, its tab, and the keys, the open screen's lit and the one under the pointer hovered.
  _canvas.Quad({Engine::Point{.x = KEY_TAB_LEFT, .y = KEY_TAB_TOP_Y}, Engine::Point{.x = KEY_TAB_RIGHT, .y = KEY_TAB_TOP_Y},
                Engine::Point{.x = KEY_TAB_RIGHT + KEY_TAB_SLANT, .y = KEY_BAR_TOP_Y},
                Engine::Point{.x = KEY_TAB_LEFT - KEY_TAB_SLANT, .y = KEY_BAR_TOP_Y}},
               KEY_TAB_TOP, KEY_TAB_BOTTOM);
  _canvas.Quad({Engine::Point{.x = 0.0f, .y = KEY_BAR_TOP_Y}, Engine::Point{.x = Engine::DESIGN_WIDTH, .y = KEY_BAR_TOP_Y},
                Engine::Point{.x = Engine::DESIGN_WIDTH, .y = Engine::DESIGN_HEIGHT}, Engine::Point{.x = 0.0f, .y = Engine::DESIGN_HEIGHT}},
               KEY_BAR_TOP, KEY_BAR_BOTTOM);
  const std::optional<DockedKey> hovered = _pointer ? KeyAt(*_pointer) : std::nullopt;
  for (std::size_t index = 0; index < KEYS.size(); ++index)
  {
    const KeyPlace& place = KEYS[index];
    const Engine::Rect rect = KeyRect(place);
    const bool lit = static_cast<std::size_t>(_desc.lit) == index;
    const bool hover = !lit && hovered == static_cast<DockedKey>(index);
    _canvas.Box(rect, lit ? KEY_LIT_FILL : (hover ? KEY_HOVER_FILL : KEY_FILL));
    Edges(rect, lit ? AMBER : (hover ? KEY_HOVER_EDGE : KEY_EDGE), _canvas);
    const float center = place.left + place.width / 2.0f;
    _canvas.Text(place.key, Engine::Font::Label, 14.0f, 0.0f, Engine::Point{.x = center, .y = 1024.5f}, Engine::Align::Center,
                 lit ? AMBER_LIGHT : KEY_NAME);
    _canvas.Text(place.name, Engine::Font::Label, 15.0f, 2.0f, Engine::Point{.x = center + 1.0f, .y = 1044.0f}, Engine::Align::Center,
                 lit ? TEXT : LABEL);
  }
}

std::optional<DockedKey> DockedFrame::KeyAt(Engine::Point _point) noexcept
{
  for (std::size_t index = 0; index < KEYS.size(); ++index)
  {
    if (Contains(KeyRect(KEYS[index]), _point))
    {
      return static_cast<DockedKey>(index);
    }
  }
  return std::nullopt;
}

void DockedFrame::Panel(const Engine::Rect& _rect, bool _topRight, bool _bottomLeft, Engine::Canvas& _canvas)
{
  ChamferedPanel(_rect, CHAMFER, PANEL_FILL, PANEL_EDGE, _topRight, _bottomLeft, _canvas);
}

DockedFrame::RowLook DockedFrame::RowLookOf(std::size_t _index, bool _selected, bool _hovered, bool _striped) noexcept
{
  if (_selected)
  {
    return RowLook::Selected;
  }
  if (_hovered)
  {
    return RowLook::Hovered;
  }
  return _striped && _index % 2 == 1 ? RowLook::Striped : RowLook::Plain;
}

void DockedFrame::Row(const Engine::Rect& _rect, RowLook _look, Engine::Canvas& _canvas)
{
  switch (_look)
  {
  case RowLook::Plain:
    return;
  case RowLook::Striped:
    _canvas.Box(_rect, ROW_STRIPE);
    return;
  case RowLook::Hovered:
    _canvas.Box(_rect, ROW_HOVER);
    return;
  case RowLook::Selected:
    _canvas.Box(_rect, ROW_SELECTED);
    return;
  }
}

std::uint32_t DockedFrame::RowText(RowLook _look) noexcept
{
  return _look == RowLook::Selected ? AMBER_LIGHT : TEXT;
}

void DockedFrame::Modal(const Engine::Rect& _rect, Engine::Canvas& _canvas)
{
  _canvas.Box(Engine::Rect{.left = 0.0f, .top = 0.0f, .width = DESIGN_WIDTH, .height = DESIGN_HEIGHT}, MODAL_BACKDROP);
  ChamferedPanel(_rect, MODAL_CHAMFER, MODAL_FILL, AMBER, true, true, _canvas);
}

void DockedFrame::Strip(const Engine::Rect& _rect, Tone _tone, std::wstring_view _headline, std::wstring_view _detail,
                        Engine::Canvas& _canvas)
{
  const bool refusal = _tone == Tone::Refusal;
  const std::uint32_t edge = refusal ? ALERT : AMBER;
  _canvas.Box(_rect, refusal ? STRIP_ALERT_FILL : STRIP_SUCCESS_FILL);
  Edges(_rect, edge, _canvas);
  const float middle = _rect.top + _rect.height / 2.0f;
  const Engine::Rect icon{
    .left = _rect.left + STRIP_ICON_LEFT, .top = middle - STRIP_ICON / 2.0f, .width = STRIP_ICON, .height = STRIP_ICON};
  _canvas.Box(icon, edge);
  const Engine::Point mark{.x = icon.left + STRIP_ICON / 2.0f, .y = middle};
  if (refusal)
  {
    _canvas.Line(Engine::Point{.x = mark.x, .y = mark.y - 7.0f}, Engine::Point{.x = mark.x, .y = mark.y + 2.0f}, 2.5f, STRIP_MARK);
    _canvas.Ellipse(Engine::Point{.x = mark.x, .y = mark.y + 6.0f}, 1.5f, 1.5f, STRIP_MARK);
  }
  else
  {
    _canvas.Line(Engine::Point{.x = mark.x - 6.0f, .y = mark.y}, Engine::Point{.x = mark.x - 2.0f, .y = mark.y + 5.0f}, 2.5f, STRIP_MARK);
    _canvas.Line(Engine::Point{.x = mark.x - 2.0f, .y = mark.y + 5.0f}, Engine::Point{.x = mark.x + 6.0f, .y = mark.y - 7.0f}, 2.5f,
                 STRIP_MARK);
  }
  const std::uint32_t text = refusal ? ALERT_LIGHT : AMBER_LIGHT;
  const float left = _rect.left + STRIP_TEXT_LEFT;
  if (_detail.empty())
  {
    _canvas.Text(_headline, Engine::Font::Label, 20.0f, 1.0f, Engine::Point{.x = left, .y = middle}, Engine::Align::Left, text);
    return;
  }
  _canvas.Text(_headline, Engine::Font::Label, 20.0f, 1.0f, Engine::Point{.x = left, .y = middle - 11.0f}, Engine::Align::Left, text);
  _canvas.Text(_detail, Engine::Font::Label, 17.0f, 1.0f, Engine::Point{.x = left, .y = middle + 12.0f}, Engine::Align::Left, text);
}

std::wstring DockedFrame::Tenths(std::uint32_t _tenths)
{
  return std::format(L"{}.{}", _tenths / 10, _tenths % 10);
}

bool DockedFrame::Contains(const Engine::Rect& _rect, Engine::Point _point) noexcept
{
  return _point.x >= _rect.left && _point.x < _rect.left + _rect.width && _point.y >= _rect.top && _point.y < _rect.top + _rect.height;
}

} // namespace Outpost
