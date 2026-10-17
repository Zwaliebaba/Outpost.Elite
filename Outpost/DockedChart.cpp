#include "pch.h"

#include "DockedChart.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <format>
#include <string_view>
#include <vector>

namespace Outpost
{

namespace
{

// The charts' palette, read off `docked/04-galactic-chart.png` to `docked/07-short-range-chart-in-flight-locked.png`.
constexpr std::uint32_t AMBER = 0xFFFFB43C;
constexpr std::uint32_t AMBER_LIGHT = 0xFFFFCF80;
constexpr std::uint32_t STAR = 0xFFFFC04A;
constexpr std::uint32_t STAR_GLOW = 0x33FFC04A;
constexpr std::uint32_t TEXT = 0xFFF3E6CF;
constexpr std::uint32_t LABEL = 0xFFC9BCA6;
constexpr std::uint32_t GREEN = 0xFF7FD85A;
constexpr std::uint32_t GREEN_TEXT = 0xFF9FE07A;
constexpr std::uint32_t RING_EDGE = 0x997FD85A;
constexpr std::uint32_t RING_FILL = 0x107FD85A;
constexpr std::uint32_t ALERT = 0xFFFF5A2C;
constexpr std::uint32_t ALERT_LIGHT = 0xFFFFB09A;
constexpr std::uint32_t ALERT_FILL = 0xFF29160F;
constexpr std::uint32_t LINE = 0xB3FFC04A;
constexpr std::uint32_t RULE = 0xFF64491D;
constexpr std::uint32_t FOOTER_RULE = 0xFF3B2A11;
constexpr std::uint32_t LEGEND_FILL = 0xFF0E0C09;
constexpr std::uint32_t LEGEND_EDGE = 0xFF473313;
constexpr std::uint32_t BAR_TRACK = 0xFF2A2116;
constexpr std::uint32_t BAR_FILL = 0xFFC9973C;
constexpr std::uint32_t FIND_FILL = 0xFF0C0A08;

// The chart's panel, its header and its footer.
constexpr Engine::Rect MAP{.left = 60.0f, .top = 110.0f, .width = 1281.0f, .height = 852.0f};
constexpr Engine::Point HEADER{.x = 85.0f, .y = 137.5f};
constexpr float FOOTER_RULE_Y = 914.5f;
constexpr float FOOTER_Y = 938.0f;
constexpr float FOOTER_GAP = 28.0f;

// The galactic chart (D100): its 256 x 128 units, five design units each, filling the panel across and centred in it down.
constexpr float GALACTIC_LEFT = 61.0f;
constexpr float GALACTIC_TOP = 222.0f;
constexpr float GALACTIC_UNIT = 5.0f;
constexpr float GALACTIC_ROWS = 128.0f;

// The short-range chart (D100): about the current system, drawn at the middle of `docked/06`'s ring, the ring's 340 units the
// fuel's 7.0 light years, which are 61.25 of the chart's units.
constexpr Engine::Point SHORT_RANGE_MIDDLE{.x = 800.0f, .y = 537.0f};
constexpr float SHORT_RANGE_UNIT = 340.0f / 61.25f;
constexpr float SHORT_RANGE_CENTER_X = 80.0f;
constexpr float SHORT_RANGE_CENTER_ROW = 64.0f;

// The legend, its three rows, and the countdown's banner beside it in flight.
constexpr Engine::Rect LEGEND{.left = 819.0f, .top = 131.0f, .width = 497.0f, .height = 121.0f};
constexpr float LEGEND_ICON_X = 848.0f;
constexpr float LEGEND_TEXT_X = 872.0f;
constexpr std::array<float, 3> LEGEND_ROWS = {157.5f, 190.5f, 224.0f};
constexpr Engine::Rect LOCKED_BANNER{.left = 430.0f, .top = 131.0f, .width = 375.0f, .height = 57.0f};

// Find, under the chart.
constexpr Engine::Point FIND_LABEL{.x = 85.0f, .y = 871.5f};
constexpr Engine::Rect FIND_BOX{.left = 242.0f, .top = 850.0f, .width = 259.0f, .height = 43.0f};
constexpr float FIND_TEXT_X = 257.0f;
constexpr float FIND_STRIP_LEFT = 516.0f;
constexpr float FIND_STRIP_PADDING = 15.0f;

// The marks on the chart: the current system, the target's bracket and label, and the cursor.
constexpr float CURRENT_RING = 11.0f;
constexpr float CURRENT_DOT = 3.5f;
constexpr float CURRENT_NAME_DOWN = 31.0f;
constexpr float CURRENT_HERE_DOWN = 54.0f;
constexpr float BRACKET_HALF = 24.0f;
constexpr float BRACKET_ARM = 12.0f;
constexpr Engine::Point TARGET_LABEL{.x = 32.0f, .y = -29.0f};
constexpr float CURSOR_GAP = 10.0f;
constexpr float CURSOR_ARM = 22.0f;
constexpr float GALACTIC_STAR = 2.2f;
constexpr float SHORT_RANGE_STAR_LARGE = 7.0f;
constexpr float SHORT_RANGE_STAR_SMALL = 4.5f;
constexpr float STAR_NAME_RIGHT = 16.0f;

// Data on System, the side panel.
constexpr Engine::Rect SIDE{.left = 1380.0f, .top = 110.0f, .width = 479.0f, .height = 850.0f};
constexpr float SIDE_LEFT = 1415.0f;
constexpr float SIDE_RIGHT = 1825.0f;
constexpr float SIDE_VALUE_X = 1577.0f;
constexpr float SIDE_TITLE_Y = 146.0f;
constexpr float SIDE_NAME_Y = 184.0f;
constexpr float SIDE_RULE_Y = 228.5f;
constexpr float DISTANCE_Y = 267.0f;
constexpr float DISTANCE_VALUE_Y = 261.0f;
constexpr Engine::Rect BAR{.left = 1415.0f, .top = 292.0f, .width = 410.0f, .height = 10.0f};
constexpr float BAR_TENTHS = 100.0f; // the bar's whole length: 10.0 light years
constexpr float FUEL_Y = 328.5f;
constexpr float BADGE_TOP = 315.0f;
constexpr float BADGE_HEIGHT = 28.0f;
constexpr float BADGE_PADDING = 10.0f;
constexpr float SEPARATOR_Y = 359.5f;
constexpr float FACTS_TOP = 390.0f;
constexpr float FACTS_PITCH = 43.0f;
constexpr float DESCRIPTION_LAST_Y = 917.0f;
constexpr float DESCRIPTION_LINE = 30.0f;
constexpr float DESCRIPTION_RULE_ABOVE = 31.0f;
constexpr float DESCRIPTION_WIDTH = 340.0f;

constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F5 = 0x3F;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F8 = 0x42;
constexpr std::uint8_t SCAN_F9 = 0x43;

[[nodiscard]] std::wstring Wide(std::string_view _text)
{
  return std::wstring(_text.begin(), _text.end());
}

[[nodiscard]] std::wstring Upper(std::wstring _text)
{
  std::ranges::transform(_text, _text.begin(), [](wchar_t _letter) { return static_cast<wchar_t>(std::towupper(_letter)); });
  return _text;
}

// A name as the game stores it, RIEDQUAT, in capitals for the chart and the panel's title.
[[nodiscard]] std::wstring NameOf(const std::string& _name)
{
  return Upper(Wide(_name));
}

// _tenths of a light year: 6.8.
[[nodiscard]] std::wstring LightYears(std::uint32_t _tenths)
{
  return std::format(L"{}.{}", _tenths / 10, _tenths % 10);
}

[[nodiscard]] float Measured(const TextMeasure& _measure, std::wstring_view _text, Engine::Font _font, float _size, float _spacing)
{
  if (!_measure)
  {
    return 0.0f;
  }
  return _measure(Engine::CanvasText{.text = std::wstring(_text),
                                     .font = _font,
                                     .size = _size,
                                     .spacing = _spacing,
                                     .anchor = {},
                                     .align = Engine::Align::Left,
                                     .color = 0});
}

// _text broken at its spaces into lines no wider than _width.
[[nodiscard]] std::vector<std::wstring> Wrapped(const std::wstring& _text, float _width, float _size, const TextMeasure& _measure)
{
  std::vector<std::wstring> lines;
  std::wstring line;
  std::size_t at = 0;
  while (at < _text.size())
  {
    const std::size_t space = _text.find(L' ', at);
    const std::wstring word = _text.substr(at, space == std::wstring::npos ? std::wstring::npos : space - at);
    at = space == std::wstring::npos ? _text.size() : space + 1;
    std::wstring longer = line;
    if (!longer.empty())
    {
      longer += L' ';
    }
    longer += word;
    if (!line.empty() && Measured(_measure, longer, Engine::Font::Label, _size, 0.0f) > _width)
    {
      lines.push_back(line);
      line = word;
    }
    else
    {
      line = longer;
    }
  }
  if (!line.empty())
  {
    lines.push_back(line);
  }
  return lines;
}

// Whether _view's target is the system the ship is at.
[[nodiscard]] bool AtCurrent(const Elite::ChartView& _view) noexcept
{
  return _view.target.facts.distanceTenths == 0;
}

// Whether _view's target is drawn red: out of range, by how far it says (D05).
[[nodiscard]] bool Short(const Elite::ChartView& _view) noexcept
{
  return !_view.locked && !AtCurrent(_view) && !_view.target.inRange;
}

// The bracket's corners about _middle: two arms at each.
void Bracket(Engine::Point _middle, std::uint32_t _color, Engine::Canvas& _canvas)
{
  for (const float across : {-1.0f, 1.0f})
  {
    for (const float down : {-1.0f, 1.0f})
    {
      const Engine::Point corner{.x = _middle.x + across * BRACKET_HALF, .y = _middle.y + down * BRACKET_HALF};
      _canvas.Line(corner, Engine::Point{.x = corner.x - across * BRACKET_ARM, .y = corner.y}, 2.0f, _color);
      _canvas.Line(corner, Engine::Point{.x = corner.x, .y = corner.y - down * BRACKET_ARM}, 2.0f, _color);
    }
  }
}

// The cursor: a cross of four arms with its middle open.
void Cursor(Engine::Point _middle, std::uint32_t _color, float _gap, float _arm, Engine::Canvas& _canvas)
{
  _canvas.Line(Engine::Point{.x = _middle.x - _gap - _arm, .y = _middle.y}, Engine::Point{.x = _middle.x - _gap, .y = _middle.y}, 2.0f,
               _color);
  _canvas.Line(Engine::Point{.x = _middle.x + _gap, .y = _middle.y}, Engine::Point{.x = _middle.x + _gap + _arm, .y = _middle.y}, 2.0f,
               _color);
  _canvas.Line(Engine::Point{.x = _middle.x, .y = _middle.y - _gap - _arm}, Engine::Point{.x = _middle.x, .y = _middle.y - _gap}, 2.0f,
               _color);
  _canvas.Line(Engine::Point{.x = _middle.x, .y = _middle.y + _gap}, Engine::Point{.x = _middle.x, .y = _middle.y + _gap + _arm}, 2.0f,
               _color);
}

void Stars(const Elite::ChartView& _view, Engine::Canvas& _canvas)
{
  const bool galactic = _view.kind == Elite::ChartKind::Galactic;
  for (const Elite::ChartStar& star : _view.stars)
  {
    if (star.x == _view.currentX && star.row == _view.currentRow)
    {
      continue; // the current system is drawn as itself
    }
    const Engine::Point place = DockedChart::PlaceOf(_view.kind, star.x, star.row);
    if (galactic)
    {
      _canvas.Ellipse(place, GALACTIC_STAR * 2.2f, GALACTIC_STAR * 2.2f, STAR_GLOW);
      _canvas.Ellipse(place, GALACTIC_STAR, GALACTIC_STAR, STAR);
      continue;
    }
    const float radius = star.large ? SHORT_RANGE_STAR_LARGE : SHORT_RANGE_STAR_SMALL;
    _canvas.Ellipse(place, radius * 1.6f, radius * 1.6f, STAR_GLOW);
    _canvas.Ellipse(place, radius, radius, STAR);
    if (star.x == _view.target.x && star.row == _view.target.row)
    {
      continue; // the target's name is its label's
    }
    _canvas.Text(NameOf(star.name), Engine::Font::Label, 20.0f, 3.0f, Engine::Point{.x = place.x + STAR_NAME_RIGHT, .y = place.y},
                 Engine::Align::Left, TEXT);
  }
}

// The current system at the centre of the fuel ring, the line to the target, the target bracketed with its name, and the cursor's
// cross, at _pointer while it is on the chart.
void Marks(const Elite::ChartView& _view, std::optional<Engine::Point> _pointer, Engine::Canvas& _canvas)
{
  const Engine::Point current = DockedChart::PlaceOf(_view.kind, _view.currentX, _view.currentRow);
  const float scale = _view.kind == Elite::ChartKind::Galactic ? GALACTIC_UNIT : SHORT_RANGE_UNIT;
  const float ring = _view.ringRadius * scale;
  _canvas.Ellipse(current, ring, ring, RING_FILL);
  _canvas.Ring(current, ring, ring, 1.0f, RING_EDGE, 3.0f, 3.0f);
  Stars(_view, _canvas);

  const bool red = Short(_view);
  const std::uint32_t mark = red ? ALERT : STAR;
  const Engine::Point target = DockedChart::PlaceOf(_view.kind, _view.target.x, _view.target.row);
  if (!AtCurrent(_view))
  {
    if (red)
    {
      _canvas.Line(current, target, 1.5f, ALERT, 4.0f, 4.0f);
    }
    else
    {
      _canvas.Line(current, target, 1.5f, LINE);
    }
  }
  _canvas.Ring(current, CURRENT_RING, CURRENT_RING, 1.5f, GREEN);
  _canvas.Ellipse(current, CURRENT_DOT, CURRENT_DOT, GREEN);
  _canvas.Text(_view.witchSpace ? std::wstring(L"WITCH SPACE") : NameOf(_view.systemName), Engine::Font::Label, 17.0f, 3.0f,
               Engine::Point{.x = current.x + 1.5f, .y = current.y + CURRENT_NAME_DOWN}, Engine::Align::Center, GREEN_TEXT);
  _canvas.Text(L"YOU ARE HERE", Engine::Font::Label, 11.0f, 3.0f, Engine::Point{.x = current.x + 1.5f, .y = current.y + CURRENT_HERE_DOWN},
               Engine::Align::Center, GREEN_TEXT);

  if (!AtCurrent(_view))
  {
    Bracket(target, mark, _canvas);
    const std::wstring label =
      _view.locked ? std::format(L"{} \u00B7 LOCKED", NameOf(_view.target.facts.name))
                   : std::format(L"{} \u00B7 {} LY", NameOf(_view.target.facts.name), LightYears(_view.target.facts.distanceTenths));
    _canvas.Text(label, Engine::Font::Label, 19.0f, 3.0f, Engine::Point{.x = target.x + TARGET_LABEL.x, .y = target.y + TARGET_LABEL.y},
                 Engine::Align::Left, mark);
  }
  // The cross follows the pointer while it is on the chart; the cursor, and with it the target, moves only on a click (D109, D116).
  const bool following = _pointer && DockedChart::ChartAt(_view.kind, *_pointer).has_value();
  const Engine::Point cross = following ? *_pointer : DockedChart::PlaceOf(_view.kind, _view.cursorX, _view.cursorRow);
  Cursor(cross, red ? ALERT : STAR, CURSOR_GAP, CURSOR_ARM, _canvas);
}

void Legend(const Elite::ChartView& _view, Engine::Canvas& _canvas)
{
  _canvas.Box(LEGEND, LEGEND_FILL);
  DockedFrame::Edge(LEGEND, LEGEND_EDGE, _canvas);
  _canvas.Ring(Engine::Point{.x = LEGEND_ICON_X, .y = LEGEND_ROWS[0]}, 9.0f, 9.0f, 1.5f, GREEN);
  Cursor(Engine::Point{.x = LEGEND_ICON_X + 1.0f, .y = LEGEND_ROWS[1]}, STAR, 0.0f, 10.0f, _canvas);
  const Engine::Rect box{.left = LEGEND_ICON_X - 10.0f, .top = LEGEND_ROWS[2] - 10.0f, .width = 20.0f, .height = 20.0f};
  _canvas.Line(Engine::Point{.x = box.left, .y = box.top}, Engine::Point{.x = box.left + box.width, .y = box.top}, 1.0f, LABEL, 3.0f, 3.0f);
  _canvas.Line(Engine::Point{.x = box.left, .y = box.top + box.height}, Engine::Point{.x = box.left + box.width, .y = box.top + box.height},
               1.0f, LABEL, 3.0f, 3.0f);
  _canvas.Line(Engine::Point{.x = box.left, .y = box.top}, Engine::Point{.x = box.left, .y = box.top + box.height}, 1.0f, LABEL, 3.0f,
               3.0f);
  _canvas.Line(Engine::Point{.x = box.left + box.width, .y = box.top}, Engine::Point{.x = box.left + box.width, .y = box.top + box.height},
               1.0f, LABEL, 3.0f, 3.0f);
  const std::array<const wchar_t*, 3> rows = {L"CURRENT SYSTEM \u2014 FIXED, CENTRE OF FUEL RING", L"CURSOR \u2014 MOVES FREELY",
                                              _view.locked ? L"TARGET LOCKED \u2014 FIXED UNTIL THE JUMP"
                                                           : L"NEAREST SYSTEM \u2014 LIVE, TARGETED ON CLOSE"};
  for (std::size_t row = 0; row < rows.size(); ++row)
  {
    _canvas.Text(rows[row], Engine::Font::Label, 15.0f, 2.0f, Engine::Point{.x = LEGEND_TEXT_X, .y = LEGEND_ROWS[row]}, Engine::Align::Left,
                 LABEL);
  }
}

// `HYPERSPACE 7 TARGET LOCKED`, in flight while the countdown runs (D07), between the header and the legend.
void LockedBanner(const Elite::ChartView& _view, const TextMeasure& _measure, Engine::Canvas& _canvas)
{
  _canvas.Box(LOCKED_BANNER, ALERT_FILL);
  DockedFrame::Edge(LOCKED_BANNER, ALERT, _canvas);
  const std::wstring count = std::format(L"{}", _view.countdown);
  const float first = Measured(_measure, L"HYPERSPACE", Engine::Font::Label, 15.0f, 4.0f);
  const float number = Measured(_measure, count, Engine::Font::Number, 34.0f, 0.0f);
  const float last = Measured(_measure, L"TARGET LOCKED", Engine::Font::Label, 15.0f, 4.0f);
  constexpr float GAP = 16.0f;
  float x = LOCKED_BANNER.left + (LOCKED_BANNER.width - (first + number + last + 2.0f * GAP)) / 2.0f;
  const float y = LOCKED_BANNER.top + LOCKED_BANNER.height / 2.0f;
  _canvas.Text(L"HYPERSPACE", Engine::Font::Label, 15.0f, 4.0f, Engine::Point{.x = x, .y = y}, Engine::Align::Left, ALERT_LIGHT);
  x += first + GAP;
  _canvas.Text(count, Engine::Font::Number, 34.0f, 0.0f, Engine::Point{.x = x, .y = y - 1.0f}, Engine::Align::Left, AMBER_LIGHT);
  x += number + GAP;
  _canvas.Text(L"TARGET LOCKED", Engine::Font::Label, 15.0f, 4.0f, Engine::Point{.x = x, .y = y}, Engine::Align::Left, ALERT_LIGHT);
}

// Find (D05): the name being typed, with its caret, or the last one typed and that it is not on this chart.
void Find(const Elite::ChartView& _view, const TextMeasure& _measure, Engine::Canvas& _canvas)
{
  const bool missing = _view.find == Elite::FindState::NotOnChart;
  _canvas.Text(L"F \u00B7 FIND SYSTEM", Engine::Font::Label, 15.0f, 3.0f, FIND_LABEL, Engine::Align::Left, AMBER);
  _canvas.Box(FIND_BOX, FIND_FILL);
  DockedFrame::Edge(FIND_BOX, missing ? ALERT : AMBER, _canvas);
  const std::wstring typed = Upper(Wide(_view.typed));
  const float middle = FIND_BOX.top + FIND_BOX.height / 2.0f;
  _canvas.Text(typed, Engine::Font::Label, 24.0f, 3.0f, Engine::Point{.x = FIND_TEXT_X, .y = middle}, Engine::Align::Left, AMBER_LIGHT);
  if (!missing)
  {
    const float caret = FIND_TEXT_X + Measured(_measure, typed, Engine::Font::Label, 24.0f, 3.0f) + 2.0f;
    _canvas.Line(Engine::Point{.x = caret, .y = middle - 12.0f}, Engine::Point{.x = caret, .y = middle + 12.0f}, 2.0f, AMBER_LIGHT);
    return;
  }
  const std::wstring words = std::format(L"{} \u2014 NOT ON THIS CHART", typed);
  const Engine::Rect strip{.left = FIND_STRIP_LEFT,
                           .top = FIND_BOX.top,
                           .width = Measured(_measure, words, Engine::Font::Label, 18.0f, 3.0f) + 2.0f * FIND_STRIP_PADDING,
                           .height = FIND_BOX.height};
  _canvas.Box(strip, ALERT_FILL);
  DockedFrame::Edge(strip, ALERT, _canvas);
  _canvas.Text(words, Engine::Font::Label, 18.0f, 3.0f, Engine::Point{.x = strip.left + FIND_STRIP_PADDING, .y = middle},
               Engine::Align::Left, ALERT_LIGHT);
}

// The footer's keys (D83, D98), each its key in amber and what it does.
void Footer(const TextMeasure& _measure, Engine::Canvas& _canvas)
{
  _canvas.Line(Engine::Point{.x = MAP.left + 1.0f, .y = FOOTER_RULE_Y}, Engine::Point{.x = MAP.left + MAP.width - 1.0f, .y = FOOTER_RULE_Y},
               1.0f, FOOTER_RULE);
  struct Part
  {
    const wchar_t* key;
    const wchar_t* does;
  };
  constexpr std::array<Part, 4> PARTS = {{
    {L"CLICK A STAR", L" select it"},
    {L"F", L" find by name"},
    {L"D", L" distance"},
    {L"5 / SPACE", L" recentre on current system"},
  }};
  float x = HEADER.x;
  for (const Part& part : PARTS)
  {
    _canvas.Text(part.key, Engine::Font::Label, 15.0f, 1.5f, Engine::Point{.x = x, .y = FOOTER_Y}, Engine::Align::Left, AMBER_LIGHT);
    x += Measured(_measure, part.key, Engine::Font::Label, 15.0f, 1.5f);
    _canvas.Text(part.does, Engine::Font::Label, 15.0f, 1.5f, Engine::Point{.x = x, .y = FOOTER_Y}, Engine::Align::Left, LABEL);
    x += Measured(_measure, part.does, Engine::Font::Label, 15.0f, 1.5f) + FOOTER_GAP;
  }
}

// One of the panel's facts: its label, and its value.
void Fact(const wchar_t* _label, const std::wstring& _value, std::size_t _row, Engine::Canvas& _canvas)
{
  const float y = FACTS_TOP + FACTS_PITCH * static_cast<float>(_row);
  _canvas.Text(_label, Engine::Font::Label, 15.0f, 4.0f, Engine::Point{.x = SIDE_LEFT, .y = y}, Engine::Align::Left, LABEL);
  _canvas.Text(_value, Engine::Font::Label, 22.0f, 0.0f, Engine::Point{.x = SIDE_VALUE_X, .y = y - 1.5f}, Engine::Align::Left, TEXT);
}

// The species as the panel prints it in its brackets: Human Colonials, Green Fat Insects.
[[nodiscard]] std::wstring Species(const std::string& _species)
{
  const std::size_t first = _species.find_first_not_of(' ');
  const std::size_t last = _species.find_last_not_of(' ');
  return first == std::string::npos ? std::wstring() : Wide(std::string_view(_species).substr(first, last - first + 1));
}

void SidePanel(const Elite::ChartView& _view, const TextMeasure& _measure, Engine::Canvas& _canvas)
{
  const Elite::SystemFacts& facts = _view.target.facts;
  const bool red = Short(_view);
  DockedFrame::Panel(SIDE, true, false, _canvas);
  _canvas.Text(_view.locked ? L"HYPERSPACE TARGET" : L"DATA ON SELECTED SYSTEM", Engine::Font::Label, 15.0f, 4.0f,
               Engine::Point{.x = SIDE_LEFT, .y = SIDE_TITLE_Y}, Engine::Align::Left, LABEL);
  _canvas.Text(NameOf(facts.name), Engine::Font::Label, 52.0f, 5.0f, Engine::Point{.x = SIDE_LEFT, .y = SIDE_NAME_Y}, Engine::Align::Left,
               AMBER_LIGHT);
  _canvas.Line(Engine::Point{.x = SIDE_LEFT, .y = SIDE_RULE_Y}, Engine::Point{.x = SIDE_RIGHT, .y = SIDE_RULE_Y}, 1.0f, RULE);

  // The distance, its bar against 10.0 light years with the fuel's reach marked, and whether the jump would go.
  _canvas.Text(L"DISTANCE", Engine::Font::Label, 15.0f, 4.0f, Engine::Point{.x = SIDE_LEFT, .y = DISTANCE_Y}, Engine::Align::Left, LABEL);
  _canvas.Text(LightYears(facts.distanceTenths) + L" LY", Engine::Font::Number, 32.0f, 0.0f,
               Engine::Point{.x = SIDE_RIGHT, .y = DISTANCE_VALUE_Y}, Engine::Align::Right, red ? ALERT : STAR);
  _canvas.Box(BAR, BAR_TRACK);
  const float filled = std::min(static_cast<float>(facts.distanceTenths), BAR_TENTHS) / BAR_TENTHS * BAR.width;
  _canvas.Box(Engine::Rect{.left = BAR.left, .top = BAR.top, .width = filled, .height = BAR.height}, red ? ALERT : BAR_FILL);
  const float reach = BAR.left + std::min(static_cast<float>(_view.reachTenths), BAR_TENTHS) / BAR_TENTHS * BAR.width;
  _canvas.Line(Engine::Point{.x = reach, .y = BAR.top - 6.0f}, Engine::Point{.x = reach, .y = BAR.top + BAR.height + 4.0f}, 2.0f, GREEN);
  _canvas.Text(std::format(L"FUEL RANGE {} LY", LightYears(_view.reachTenths)), Engine::Font::Label, 15.0f, 2.0f,
               Engine::Point{.x = SIDE_LEFT, .y = FUEL_Y}, Engine::Align::Left, GREEN_TEXT);
  const std::wstring badge = DockedChart::BadgeOf(_view);
  const float width = Measured(_measure, badge, Engine::Font::Label, 14.0f, 2.0f) + 2.0f * BADGE_PADDING;
  const Engine::Rect badgeRect{.left = SIDE_RIGHT - width, .top = BADGE_TOP, .width = width, .height = BADGE_HEIGHT};
  const std::uint32_t badgeColor = red ? ALERT : (AtCurrent(_view) ? LABEL : STAR);
  DockedFrame::Edge(badgeRect, badgeColor, _canvas);
  _canvas.Text(badge, Engine::Font::Label, 14.0f, 2.0f,
               Engine::Point{.x = badgeRect.left + BADGE_PADDING, .y = badgeRect.top + badgeRect.height / 2.0f}, Engine::Align::Left,
               badgeColor);
  _canvas.Line(Engine::Point{.x = SIDE_LEFT, .y = SEPARATOR_Y}, Engine::Point{.x = SIDE_RIGHT, .y = SEPARATOR_Y}, 1.0f, RULE);

  Fact(L"ECONOMY", Wide(facts.economy), 0, _canvas);
  Fact(L"GOVERNMENT", Wide(facts.government), 1, _canvas);
  Fact(L"TECH LEVEL", std::format(L"{}", facts.techLevel), 2, _canvas);
  Fact(L"POPULATION", std::format(L"{} billion ({})", LightYears(facts.populationTenthsBillion), Species(facts.species)), 3, _canvas);
  Fact(L"PRODUCTIVITY", DockedChart::Thousands(facts.productivityMillionCredits) + L" M CR", 4, _canvas);
  Fact(L"RADIUS", DockedChart::Thousands(facts.radiusKm) + L" km", 5, _canvas);

  // The description, its last line at the panel's foot, and a rule over it.
  const std::vector<std::wstring> lines = Wrapped(Wide(facts.description), DESCRIPTION_WIDTH, 21.0f, _measure);
  float y = DESCRIPTION_LAST_Y - DESCRIPTION_LINE * static_cast<float>(lines.empty() ? 0 : lines.size() - 1);
  const float rule = y - DESCRIPTION_RULE_ABOVE;
  _canvas.Line(Engine::Point{.x = SIDE_LEFT, .y = rule}, Engine::Point{.x = SIDE_RIGHT, .y = rule}, 1.0f, RULE);
  for (const std::wstring& line : lines)
  {
    _canvas.Text(line, Engine::Font::Label, 21.0f, 0.0f, Engine::Point{.x = SIDE_LEFT, .y = y}, Engine::Align::Left, AMBER_LIGHT);
    y += DESCRIPTION_LINE;
  }
}

// Where the ship is, as the top bar says it.
[[nodiscard]] std::wstring Location(const Elite::ChartView& _view)
{
  if (!_view.flight)
  {
    return std::format(L"DOCKED \u00B7 {} STATION \u00B7 {}", _view.dodo ? L"DODO" : L"CORIOLIS", NameOf(_view.systemName));
  }
  const std::wstring where = _view.witchSpace ? std::wstring(L"WITCH SPACE") : NameOf(_view.systemName) + L" SYSTEM";
  return _view.locked ? std::format(L"IN FLIGHT \u00B7 {} \u00B7 HYPERSPACE COUNTDOWN", where) : std::format(L"IN FLIGHT \u00B7 {}", where);
}

} // namespace

void DockedChart::Draw(const Elite::ChartView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                       Engine::Canvas& _canvas)
{
  _canvas.Clear();
  const bool galactic = _view.kind == Elite::ChartKind::Galactic;
  DockedFrame::Draw(DockedFrame::Desc{.location = Location(_view),
                                      .title = galactic ? std::format(L"GALACTIC CHART {}", _view.galaxyNumber) : L"SHORT RANGE CHART",
                                      .creditsTenths = _view.creditsTenths,
                                      .lit = galactic ? DockedKey::Galaxy : DockedKey::Local,
                                      .flight = _view.flight,
                                      .alert = _view.flight && _view.locked},
                    _pointer, _measure, _canvas);
  DockedFrame::Panel(MAP, true, false, _canvas);
  _canvas.Text(galactic ? std::format(L"GALAXY {} \u00B7 256 SYSTEMS \u00B7 RING = FUEL RANGE", _view.galaxyNumber)
                        : std::wstring(L"LOCAL SPACE \u00B7 RING = FUEL RANGE"),
               Engine::Font::Label, 15.0f, 4.0f, HEADER, Engine::Align::Left, LABEL);
  Marks(_view, _pointer, _canvas);
  Legend(_view, _canvas);
  if (_view.flight && _view.locked)
  {
    LockedBanner(_view, _measure, _canvas);
  }
  if (_view.find != Elite::FindState::Closed)
  {
    Find(_view, _measure, _canvas);
  }
  Footer(_measure, _canvas);
  SidePanel(_view, _measure, _canvas);
}

std::optional<Elite::ChartClick> DockedChart::ChartAt(Elite::ChartKind _kind, Engine::Point _point) noexcept
{
  if (!DockedFrame::Contains(Engine::Rect{.left = MAP.left, .top = MAP.top, .width = MAP.width, .height = FOOTER_RULE_Y - MAP.top}, _point))
  {
    return std::nullopt;
  }
  float x = 0.0f;
  float row = 0.0f;
  if (_kind == Elite::ChartKind::Galactic)
  {
    x = std::floor((_point.x - GALACTIC_LEFT) / GALACTIC_UNIT);
    row = std::floor((_point.y - GALACTIC_TOP) / GALACTIC_UNIT);
  }
  else
  {
    x = std::round(SHORT_RANGE_CENTER_X + (_point.x - SHORT_RANGE_MIDDLE.x) / SHORT_RANGE_UNIT);
    row = std::round(SHORT_RANGE_CENTER_ROW + (_point.y - SHORT_RANGE_MIDDLE.y) / SHORT_RANGE_UNIT);
  }
  return Elite::ChartClick{.x = static_cast<std::uint8_t>(std::clamp(x, 0.0f, 255.0f)),
                           .row = static_cast<std::uint8_t>(std::clamp(row, 0.0f, GALACTIC_ROWS - 1.0f))};
}

Engine::Point DockedChart::PlaceOf(Elite::ChartKind _kind, float _x, float _row) noexcept
{
  if (_kind == Elite::ChartKind::Galactic)
  {
    return Engine::Point{.x = GALACTIC_LEFT + (_x + 0.5f) * GALACTIC_UNIT, .y = GALACTIC_TOP + (_row + 0.5f) * GALACTIC_UNIT};
  }
  return Engine::Point{.x = SHORT_RANGE_MIDDLE.x + (_x - SHORT_RANGE_CENTER_X) * SHORT_RANGE_UNIT,
                       .y = SHORT_RANGE_MIDDLE.y + (_row - SHORT_RANGE_CENTER_ROW) * SHORT_RANGE_UNIT};
}

std::optional<std::uint8_t> DockedChart::FlightScanCodeOf(DockedKey _key) noexcept
{
  switch (_key)
  {
  case DockedKey::Launch:
    return SCAN_F1;
  case DockedKey::Market:
    return SCAN_F8;
  case DockedKey::Galaxy:
    return SCAN_F5;
  case DockedKey::Local:
    return SCAN_F6;
  case DockedKey::Status:
    return SCAN_F9;
  case DockedKey::Disc:
  case DockedKey::Equip:
    break;
  }
  return std::nullopt;
}

std::wstring DockedChart::BadgeOf(const Elite::ChartView& _view)
{
  if (_view.locked)
  {
    return L"LOCKED \u00B7 IN RANGE";
  }
  if (AtCurrent(_view))
  {
    return L"CURRENT SYSTEM";
  }
  if (_view.target.inRange)
  {
    return L"IN RANGE";
  }
  return std::format(L"OUT OF RANGE \u00B7 {} LY SHORT", LightYears(_view.target.shortTenths));
}

std::wstring DockedChart::Thousands(std::uint32_t _value)
{
  std::wstring digits = std::format(L"{}", _value);
  for (std::size_t at = digits.size(); at > 3; at -= 3)
  {
    digits.insert(at - 3, 1, L' ');
  }
  return digits;
}

} // namespace Outpost
