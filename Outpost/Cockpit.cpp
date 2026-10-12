#include "pch.h"

#include "Cockpit.h"

#include "Gauges.h"
#include "SpaceViewLayers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace Outpost
{

namespace
{

// The design's colours, 0xAARRGGBB, display-encoded, read off `cockpit-hud-overlay/`.
constexpr std::uint32_t PANEL = 0xFF101418;       // the console and the top bar
constexpr std::uint32_t PANEL_DARK = 0xFF0B0E12;  // the side panels, outside
constexpr std::uint32_t PANEL_EDGE = 0xFF2E3842;  // the side panels, at the window
constexpr std::uint32_t GOLD = 0xFFB58A36;        // outlines
constexpr std::uint32_t GOLD_FAINT = 0x806E5A30;  // the scanner's inner rings, the console's lines
constexpr std::uint32_t AMBER = 0xFFF5A623;       // a gauge
constexpr std::uint32_t AMBER_LIGHT = 0xFFF6C56B; // its number, the title
constexpr std::uint32_t YELLOW = 0xFFF2D23A;      // a warning
constexpr std::uint32_t RED = 0xFFE8452C;         // danger
constexpr std::uint32_t TRACK = 0xFF231F1A;       // a bar's empty part
constexpr std::uint32_t LABEL = 0xFFC9D1D9;       // a label
constexpr std::uint32_t GREEN = 0xFF6CD45A;       // the condition light, the compass's dot
constexpr std::uint32_t SCANNER_FILL = 0xFF17140F;
constexpr std::uint32_t MISSILE = 0xFFA07A2A;
constexpr std::uint32_t DIM = 0x40FFFFFF; // an indicator that is off

// The gauges' rows, top to bottom, and the bars' segments.
constexpr std::array<float, 7> ROWS = {866.0f, 895.0f, 924.0f, 953.0f, 982.0f, 1011.0f, 1040.0f};
constexpr float LEFT_LABEL_X = 56.0f;
constexpr float LEFT_BAR_X = 240.0f;
constexpr float LEFT_BAR_WIDTH = 284.0f;
constexpr int LEFT_SEGMENTS = 24;
constexpr float LEFT_NUMBER_X = 596.0f;
constexpr float RIGHT_BAR_X = 1340.0f;
constexpr float RIGHT_BAR_WIDTH = 232.0f;
constexpr int RIGHT_SEGMENTS = 20;
constexpr float RIGHT_LABEL_X = 1588.0f;
constexpr float BAR_HEIGHT = 14.0f;
constexpr float SEGMENT_GAP = 3.5f;
constexpr float LABEL_SIZE = 17.0f;
constexpr float LABEL_SPACING = 2.0f;
constexpr float NUMBER_SIZE = 16.0f;
constexpr std::uint64_t FLASH_MILLISECONDS = 512; // the condition light's flash: bit 9 of the game's millisecond counter

// A blip's z or y in the scanner's rows, as XorScannerBlip shifts it: a quarter, rounded down.
[[nodiscard]] int ScannerRows(std::int8_t _value) noexcept
{
  return _value >> 2;
}

// The scanner: an ellipse under the view, its rings and spokes, and a blip's scale, the blip bytes' reach (SnapshotBlip).
constexpr Engine::Point SCANNER{.x = 960.0f, .y = 948.0f};
constexpr float SCANNER_RX = 270.0f;
constexpr float SCANNER_RY = 100.0f;
constexpr float BLIP_REACH_X = 64.0f; // x, the camera's x high byte
constexpr float BLIP_REACH_Z = 16.0f; // z / 4 and y / 4, rows as XorScannerBlip draws them

constexpr Engine::Point CONDITION{.x = 1813.0f, .y = 848.0f};
constexpr Engine::Point COMPASS{.x = 1813.0f, .y = 972.0f};
constexpr float COMPASS_RADIUS = 55.0f;
constexpr float COMPASS_REACH = 7.0f; // the dot's steps from the centre

// The sights (D65): DrawLaserSights' 16 x 16 sprite from the view's pixel 120 and row 55, a sprite pixel there in design units.
constexpr float SIGHTS_X = 120.0f;
constexpr float SIGHTS_ROW = 55.0f;
constexpr float SIGHTS_LINE = 1.0f;               // a stroke of a sight, in sprite pixels
constexpr std::uint32_t SIGHTS_FILL = 0x8CF5A623; // what the sprite fills in its colour 2: amber, more than half seen through
constexpr std::uint8_t PULSE = 0;
constexpr std::uint8_t BEAM = 1;
constexpr std::uint8_t MILITARY = 2;
constexpr std::uint8_t MINING = 3;

[[nodiscard]] std::uint32_t LevelColor(Elite::GaugeLevel _level, bool _flashOn) noexcept
{
  switch (_level)
  {
  case Elite::GaugeLevel::Warning:
    return YELLOW;
  case Elite::GaugeLevel::Danger:
    return RED;
  case Elite::GaugeLevel::Critical:
    return _flashOn ? RED : TRACK;
  case Elite::GaugeLevel::Normal:
    break;
  }
  return AMBER;
}

// A segmented bar from _left, _width across, its row's middle at _y, filled to _fill in _color.
void Bar(Engine::Canvas& _canvas, float _left, float _width, int _segments, float _y, float _fill, std::uint32_t _color)
{
  const float pitch = _width / static_cast<float>(_segments);
  const float lit = std::clamp(_fill, 0.0f, 1.0f) * static_cast<float>(_segments);
  for (int segment = 0; segment < _segments; ++segment)
  {
    const Engine::Rect rect{.left = _left + pitch * static_cast<float>(segment),
                            .top = _y - BAR_HEIGHT / 2.0f,
                            .width = pitch - SEGMENT_GAP,
                            .height = BAR_HEIGHT};
    const float share = std::clamp(lit - static_cast<float>(segment), 0.0f, 1.0f);
    _canvas.Box(rect, TRACK, 1.5f);
    if (share > 0.0f)
    {
      // A part-lit segment is lit across its share, as the design's last segment fades.
      _canvas.Box(Engine::Rect{.left = rect.left, .top = rect.top, .width = rect.width * share, .height = rect.height}, _color, 1.5f);
    }
  }
}

void Label(Engine::Canvas& _canvas, const wchar_t* _text, float _x, float _y, Engine::Align _align = Engine::Align::Left)
{
  _canvas.Text(_text, Engine::Font::Label, LABEL_SIZE, LABEL_SPACING, Engine::Point{.x = _x, .y = _y}, _align, LABEL);
}

void LeftGauge(Engine::Canvas& _canvas, const wchar_t* _label, float _y, const Elite::Gauge& _gauge, bool _flashOn)
{
  Label(_canvas, _label, LEFT_LABEL_X, _y);
  Bar(_canvas, LEFT_BAR_X, LEFT_BAR_WIDTH, LEFT_SEGMENTS, _y, _gauge.fill, LevelColor(_gauge.level, _flashOn));
  if (_gauge.number.empty())
  {
    return;
  }
  const std::wstring number(_gauge.number.begin(), _gauge.number.end());
  _canvas.Text(number, Engine::Font::Number, NUMBER_SIZE, 0.0f, Engine::Point{.x = LEFT_NUMBER_X, .y = _y}, Engine::Align::Right,
               AMBER_LIGHT);
}

// The roll or the dive and climb: a track with a marker at _travel, -1 to 1, of its half-width from its middle, while _live.
void Indicator(Engine::Canvas& _canvas, const wchar_t* _label, float _y, float _travel, bool _live)
{
  _canvas.Box(Engine::Rect{.left = RIGHT_BAR_X, .top = _y - BAR_HEIGHT / 2.0f, .width = RIGHT_BAR_WIDTH, .height = BAR_HEIGHT}, TRACK,
              1.5f);
  if (_live)
  {
    const float middle = RIGHT_BAR_X + RIGHT_BAR_WIDTH / 2.0f;
    const float x = middle + _travel * (RIGHT_BAR_WIDTH / 2.0f - 4.0f);
    _canvas.Box(Engine::Rect{.left = x - 3.0f, .top = _y - 11.0f, .width = 6.0f, .height = 22.0f}, AMBER, 1.5f);
  }
  Label(_canvas, _label, RIGHT_LABEL_X, _y);
}

void Frame(Engine::Canvas& _canvas)
{
  // The side panels, darker outside and lighter at the window's slanted edge, and the top bar with its notch.
  _canvas.Quad({Engine::Point{.x = 0.0f, .y = 40.0f}, Engine::Point{.x = 160.0f, .y = 40.0f}, Engine::Point{.x = 96.0f, .y = 810.0f},
                Engine::Point{.x = 0.0f, .y = 810.0f}},
               PANEL_DARK, PANEL_DARK);
  _canvas.Quad({Engine::Point{.x = 160.0f, .y = 40.0f}, Engine::Point{.x = 186.0f, .y = 40.0f}, Engine::Point{.x = 122.0f, .y = 810.0f},
                Engine::Point{.x = 96.0f, .y = 810.0f}},
               PANEL_EDGE, PANEL_DARK);
  _canvas.Quad({Engine::Point{.x = 1760.0f, .y = 40.0f}, Engine::Point{.x = 1920.0f, .y = 40.0f}, Engine::Point{.x = 1920.0f, .y = 810.0f},
                Engine::Point{.x = 1824.0f, .y = 810.0f}},
               PANEL_DARK, PANEL_DARK);
  _canvas.Quad({Engine::Point{.x = 1734.0f, .y = 40.0f}, Engine::Point{.x = 1760.0f, .y = 40.0f}, Engine::Point{.x = 1824.0f, .y = 810.0f},
                Engine::Point{.x = 1798.0f, .y = 810.0f}},
               PANEL_EDGE, PANEL_DARK);
  _canvas.Box(Engine::Rect{.left = 0.0f, .top = 0.0f, .width = Engine::DESIGN_WIDTH, .height = 40.0f}, PANEL);
  _canvas.Quad({Engine::Point{.x = 740.0f, .y = 40.0f}, Engine::Point{.x = 1180.0f, .y = 40.0f}, Engine::Point{.x = 1150.0f, .y = 64.0f},
                Engine::Point{.x = 770.0f, .y = 64.0f}},
               PANEL, 0xFF222A33);
  // The console: its floor, its raised middle and their gold edges.
  _canvas.Box(Engine::Rect{.left = 0.0f, .top = 810.0f, .width = Engine::DESIGN_WIDTH, .height = 270.0f}, PANEL);
  _canvas.Quad({Engine::Point{.x = 660.0f, .y = 740.0f}, Engine::Point{.x = 1260.0f, .y = 740.0f}, Engine::Point{.x = 1320.0f, .y = 810.0f},
                Engine::Point{.x = 600.0f, .y = 810.0f}},
               0xFF1A2027, PANEL);
  _canvas.Line(Engine::Point{.x = 0.0f, .y = 810.0f}, Engine::Point{.x = 600.0f, .y = 810.0f}, 2.0f, GOLD);
  _canvas.Line(Engine::Point{.x = 600.0f, .y = 810.0f}, Engine::Point{.x = 660.0f, .y = 740.0f}, 2.0f, GOLD);
  _canvas.Line(Engine::Point{.x = 660.0f, .y = 740.0f}, Engine::Point{.x = 1260.0f, .y = 740.0f}, 2.0f, GOLD);
  _canvas.Line(Engine::Point{.x = 1260.0f, .y = 740.0f}, Engine::Point{.x = 1320.0f, .y = 810.0f}, 2.0f, GOLD);
  _canvas.Line(Engine::Point{.x = 1320.0f, .y = 810.0f}, Engine::Point{.x = Engine::DESIGN_WIDTH, .y = 810.0f}, 2.0f, GOLD);
}

// The message line in the top bar, with a rule each side of it: the view's name when no message is up.
void Title(const Elite::SnapshotDashboard& _dashboard, Engine::Renderer& _renderer, Engine::Canvas& _canvas)
{
  std::wstring message;
  for (const char letter : _dashboard.message)
  {
    if (letter == '\0')
    {
      break;
    }
    message.push_back(static_cast<wchar_t>(static_cast<unsigned char>(letter)));
  }
  const Engine::Point anchor{.x = 960.0f, .y = 28.0f};
  constexpr float SIZE = 22.0f;
  constexpr float SPACING = 7.0f;
  _canvas.Text(message, Engine::Font::Label, SIZE, SPACING, anchor, Engine::Align::Center, AMBER_LIGHT);
  Engine::CanvasText measured;
  measured.text = message;
  measured.size = SIZE;
  measured.spacing = SPACING;
  const float half = _renderer.MeasureText(measured) / 2.0f;
  _canvas.Line(Engine::Point{.x = anchor.x - half - 56.0f, .y = 30.0f}, Engine::Point{.x = anchor.x - half - 16.0f, .y = 30.0f}, 2.0f,
               AMBER);
  _canvas.Line(Engine::Point{.x = anchor.x + half + 16.0f, .y = 30.0f}, Engine::Point{.x = anchor.x + half + 56.0f, .y = 30.0f}, 2.0f,
               AMBER);
}

// The safe zone's S, and, beside it while the ECM fires, its E (a placeholder until the owner draws it, D54).
void Indicators(const Elite::SnapshotDashboard& _dashboard, Engine::Canvas& _canvas)
{
  _canvas.Line(Engine::Point{.x = 700.0f, .y = 783.0f}, Engine::Point{.x = _dashboard.ecm ? 786.0f : 900.0f, .y = 783.0f}, 1.0f,
               GOLD_FAINT);
  _canvas.Line(Engine::Point{.x = 1020.0f, .y = 783.0f}, Engine::Point{.x = 1220.0f, .y = 783.0f}, 1.0f, GOLD_FAINT);
  const auto box = [&_canvas](float _left, const wchar_t* _glyph, const wchar_t* _top, const wchar_t* _bottom, bool _on)
  {
    _canvas.Box(Engine::Rect{.left = _left, .top = 758.0f, .width = 102.0f, .height = 52.0f}, PANEL);
    _canvas.BoxEdge(Engine::Rect{.left = _left, .top = 758.0f, .width = 102.0f, .height = 52.0f}, 1.5f, _on ? GOLD : GOLD_FAINT);
    _canvas.Text(_glyph, Engine::Font::Number, 30.0f, 0.0f, Engine::Point{.x = _left + 32.0f, .y = 783.0f}, Engine::Align::Center,
                 _on ? AMBER : DIM);
    _canvas.Text(_top, Engine::Font::Label, 10.0f, 1.0f, Engine::Point{.x = _left + 51.0f, .y = 777.0f}, Engine::Align::Left,
                 _on ? LABEL : DIM);
    _canvas.Text(_bottom, Engine::Font::Label, 10.0f, 1.0f, Engine::Point{.x = _left + 51.0f, .y = 789.0f}, Engine::Align::Left,
                 _on ? LABEL : DIM);
  };
  box(910.0f, L"S", L"SAFE", L"ZONE", _dashboard.safeZone);
  if (_dashboard.ecm)
  {
    box(796.0f, L"E", L"ECM", L"", true);
  }
}

// The missiles: a box each, filled while it is there; the next to fire outlined yellow while armed, filled red while locked on
// (a placeholder until the owner draws it, D54).
void Missiles(const Elite::Gauges& _gauges, Engine::Canvas& _canvas)
{
  constexpr int MISSILE_BOXES = 4;
  const float y = ROWS[6];
  Label(_canvas, L"MISSILES", LEFT_LABEL_X, y);
  for (int missile = 0; missile < MISSILE_BOXES; ++missile)
  {
    const Engine::Rect rect{.left = LEFT_BAR_X + 56.0f * static_cast<float>(missile), .top = y - 7.0f, .width = 46.0f, .height = 14.0f};
    const bool here = missile < _gauges.missiles;
    const bool next = missile == _gauges.missiles - 1;
    if (here)
    {
      _canvas.Box(rect, next && _gauges.missileState == Elite::MissileState::Locked ? RED : MISSILE);
    }
    _canvas.BoxEdge(rect, 1.5f, next && _gauges.missileState != Elite::MissileState::Idle ? YELLOW : here ? MISSILE : LABEL);
  }
  if (!_gauges.live)
  {
    return;
  }
  _canvas.Text(std::to_wstring(_gauges.missiles), Engine::Font::Number, NUMBER_SIZE, 0.0f, Engine::Point{.x = LEFT_NUMBER_X, .y = y},
               Engine::Align::Right, AMBER_LIGHT);
}

void Scanner(const Elite::SnapshotDashboard& _dashboard, Engine::Canvas& _canvas)
{
  _canvas.Ellipse(SCANNER, SCANNER_RX, SCANNER_RY, SCANNER_FILL);
  _canvas.Ring(SCANNER, 180.0f, 67.0f, 1.0f, GOLD_FAINT, 4.0f, 4.0f);
  _canvas.Ring(SCANNER, 90.0f, 34.0f, 1.0f, GOLD_FAINT, 4.0f, 4.0f);
  for (const Engine::Point end :
       {Engine::Point{.x = 705.0f, .y = 972.0f}, Engine::Point{.x = 1215.0f, .y = 972.0f}, Engine::Point{.x = 780.0f, .y = 880.0f},
        Engine::Point{.x = 1140.0f, .y = 880.0f}, Engine::Point{.x = 960.0f, .y = 1046.0f}})
  {
    _canvas.Line(SCANNER, end, 1.0f, GOLD_FAINT, 4.0f, 4.0f);
  }
  _canvas.Ring(SCANNER, SCANNER_RX, SCANNER_RY, 2.0f, GOLD);
  _canvas.Line(Engine::Point{.x = SCANNER.x - 8.0f, .y = SCANNER.y}, Engine::Point{.x = SCANNER.x + 8.0f, .y = SCANNER.y}, 1.5f,
               AMBER_LIGHT);
  _canvas.Line(Engine::Point{.x = SCANNER.x, .y = SCANNER.y - 8.0f}, Engine::Point{.x = SCANNER.x, .y = SCANNER.y + 8.0f}, 1.5f,
               AMBER_LIGHT);
  // Each blip where XorScannerBlip puts it: x across, z up the scanner, and a stick of y down to the blip.
  for (std::size_t index = 0; index < _dashboard.blipCount; ++index)
  {
    const Elite::SnapshotBlip& blip = _dashboard.blips[index];
    const float x = SCANNER.x + static_cast<float>(blip.x) / BLIP_REACH_X * SCANNER_RX;
    const float base = SCANNER.y - static_cast<float>(ScannerRows(blip.z)) / BLIP_REACH_Z * SCANNER_RY;
    const float end = base + static_cast<float>(ScannerRows(blip.y)) / BLIP_REACH_Z * SCANNER_RY;
    const std::uint32_t color = blip.hostile ? RED : AMBER;
    _canvas.Line(Engine::Point{.x = x, .y = base}, Engine::Point{.x = x, .y = end}, 2.0f, color);
    _canvas.Box(Engine::Rect{.left = x - 1.0f, .top = end - 3.0f, .width = 8.0f, .height = 6.0f}, color, 1.0f);
  }
}

void ConditionAndCompass(const Elite::SnapshotDashboard& _dashboard, const Elite::Gauges& _gauges, bool _flashOn, Engine::Canvas& _canvas)
{
  // Dark with nothing live (the title's dashboard, D64).
  std::uint32_t light = _gauges.live ? GREEN : PANEL;
  switch (_gauges.condition)
  {
  case Elite::Condition::Yellow:
    light = YELLOW;
    break;
  case Elite::Condition::Red:
    light = RED;
    break;
  case Elite::Condition::Flashing:
    light = _flashOn ? RED : PANEL;
    break;
  case Elite::Condition::Green:
    break;
  }
  _canvas.Ring(CONDITION, 24.0f, 24.0f, 1.5f, 0x80345A30);
  _canvas.Ellipse(CONDITION, 17.0f, 17.0f, light);
  Label(_canvas, L"CONDITION", CONDITION.x, 886.0f, Engine::Align::Center);

  _canvas.Ring(COMPASS, COMPASS_RADIUS, COMPASS_RADIUS, 1.5f, 0xFF5A4A2A);
  _canvas.Line(Engine::Point{.x = COMPASS.x - COMPASS_RADIUS, .y = COMPASS.y},
               Engine::Point{.x = COMPASS.x + COMPASS_RADIUS, .y = COMPASS.y}, 1.0f, 0x806E5A30);
  _canvas.Line(Engine::Point{.x = COMPASS.x, .y = COMPASS.y - COMPASS_RADIUS},
               Engine::Point{.x = COMPASS.x, .y = COMPASS.y + COMPASS_RADIUS}, 1.0f, 0x806E5A30);
  if (_dashboard.compass)
  {
    const float reach = (COMPASS_RADIUS - 8.0f) / COMPASS_REACH;
    const Engine::Point dot{.x = COMPASS.x + static_cast<float>(_dashboard.compassX) * reach,
                            .y = COMPASS.y + static_cast<float>(_dashboard.compassY) * reach};
    if (_dashboard.compassInFront)
    {
      _canvas.Ellipse(dot, 5.0f, 5.0f, GREEN);
    }
    else
    {
      _canvas.Ring(dot, 5.0f, 5.0f, 1.5f, GREEN);
    }
  }
  Label(_canvas, L"COMPASS", COMPASS.x, 1042.0f, Engine::Align::Center);
}

// The sights of laser type _sight, the four of DrawLaserSights' sprites traced in the canvas's amber (D65): pulse, two arrows at the
// middle and ticks at the sides; beam, the same, bolder and filled; military, a pentagon each side of the middle and a foot at each
// corner; mining, a bracket at each corner, ticks at the sides and a box in the middle with a bar across it.
void Sights(std::uint8_t _sight, Engine::Canvas& _canvas)
{
  const float across = Cockpit::VIEW.width / static_cast<float>(Elite::SPACE_VIEW_WIDTH_PIXELS);
  const float down = Cockpit::VIEW.height / static_cast<float>(Elite::SPACE_VIEW_HEIGHT_PIXELS);
  // A point of the sprite, x right and y down from its top left corner, in its pixels.
  const auto at = [across, down](float _x, float _y)
  { return Engine::Point{.x = Cockpit::VIEW.left + (SIGHTS_X + _x) * across, .y = Cockpit::VIEW.top + (SIGHTS_ROW + _y) * down}; };
  const float width = SIGHTS_LINE * across;
  const auto line = [&_canvas, &at, width](float _fromX, float _fromY, float _toX, float _toY)
  { _canvas.Line(at(_fromX, _fromY), at(_toX, _toY), width, AMBER); };
  // A trapezoid, its top edge from _left to _right at _top and its bottom from _bottomLeft to _bottomRight at _bottom.
  const auto trapezoid =
    [&_canvas, &at](float _left, float _right, float _top, float _bottomLeft, float _bottomRight, float _bottom, std::uint32_t _color)
  { _canvas.Quad({at(_left, _top), at(_right, _top), at(_bottomRight, _bottom), at(_bottomLeft, _bottom)}, _color, _color); };
  // The same, mirrored top to bottom about the sprite's middle.
  const auto mirrored =
    [&trapezoid](float _left, float _right, float _top, float _bottomLeft, float _bottomRight, float _bottom, std::uint32_t _color)
  {
    constexpr float SIDE = 16.0f;
    trapezoid(_bottomLeft, _bottomRight, SIDE - _bottom, _left, _right, SIDE - _top, _color);
  };
  switch (_sight)
  {
  case PULSE:
    trapezoid(5.0f, 11.0f, 2.0f, 7.0f, 9.0f, 5.0f, AMBER);
    mirrored(5.0f, 11.0f, 2.0f, 7.0f, 9.0f, 5.0f, AMBER);
    line(6.5f, 1.0f, 6.5f, 2.0f);
    line(9.5f, 1.0f, 9.5f, 2.0f);
    line(6.5f, 14.0f, 6.5f, 15.0f);
    line(9.5f, 14.0f, 9.5f, 15.0f);
    for (const float x : {0.5f, 15.5f})
    {
      line(x, 3.0f, x, 5.0f);
      line(x, 11.0f, x, 13.0f);
    }
    break;
  case BEAM:
    trapezoid(5.0f, 11.0f, 1.0f, 7.0f, 9.0f, 6.0f, SIGHTS_FILL);
    mirrored(5.0f, 11.0f, 1.0f, 7.0f, 9.0f, 6.0f, SIGHTS_FILL);
    line(5.0f, 1.0f, 11.0f, 1.0f);
    line(5.0f, 1.0f, 7.0f, 6.0f);
    line(11.0f, 1.0f, 9.0f, 6.0f);
    line(5.0f, 15.0f, 11.0f, 15.0f);
    line(5.0f, 15.0f, 7.0f, 10.0f);
    line(11.0f, 15.0f, 9.0f, 10.0f);
    for (const float x : {1.0f, 15.0f})
    {
      _canvas.Box(Engine::Rect{.left = at(x - 1.0f, 2.0f).x, .top = at(0.0f, 2.0f).y, .width = 2.0f * across, .height = 4.0f * down},
                  AMBER);
      _canvas.Box(Engine::Rect{.left = at(x - 1.0f, 10.0f).x, .top = at(0.0f, 10.0f).y, .width = 2.0f * across, .height = 4.0f * down},
                  AMBER);
    }
    break;
  case MILITARY:
    for (const bool lower : {false, true})
    {
      const auto y = [lower](float _y) { return lower ? 16.0f - _y : _y; };
      line(8.0f, y(1.0f), 10.5f, y(3.5f));
      line(10.5f, y(3.5f), 10.0f, y(5.5f));
      line(10.0f, y(5.5f), 6.0f, y(5.5f));
      line(6.0f, y(5.5f), 5.5f, y(3.5f));
      line(5.5f, y(3.5f), 8.0f, y(1.0f));
      line(0.5f, y(1.5f), 2.5f, y(5.5f));
      line(0.0f, y(6.5f), 3.0f, y(6.5f));
      line(15.5f, y(1.5f), 13.5f, y(5.5f));
      line(13.0f, y(6.5f), 16.0f, y(6.5f));
    }
    break;
  case MINING:
    for (const bool lower : {false, true})
    {
      const auto y = [lower](float _y) { return lower ? 16.0f - _y : _y; };
      line(1.0f, y(1.5f), 4.0f, y(1.5f));
      line(3.5f, y(1.5f), 3.5f, y(4.0f));
      line(12.0f, y(1.5f), 15.0f, y(1.5f));
      line(12.5f, y(1.5f), 12.5f, y(4.0f));
    }
    _canvas.BoxEdge(Engine::Rect{.left = at(5.0f, 5.0f).x, .top = at(5.0f, 5.0f).y, .width = 6.0f * across, .height = 6.0f * down}, width,
                    AMBER);
    _canvas.Box(Engine::Rect{.left = at(6.0f, 7.0f).x, .top = at(6.0f, 7.0f).y, .width = 4.0f * across, .height = 2.0f * down},
                SIGHTS_FILL);
    line(0.5f, 7.0f, 0.5f, 9.0f);
    line(15.5f, 7.0f, 15.5f, 9.0f);
    break;
  default:
    break;
  }
}

} // namespace

void Cockpit::Draw(const Elite::SnapshotDashboard& _dashboard, std::uint8_t _sight, std::uint64_t _milliseconds,
                   Engine::Renderer& _renderer, Engine::Canvas& _canvas)
{
  const Elite::Gauges gauges = Elite::GaugesOf(_dashboard);
  const bool flashOn = (_milliseconds / FLASH_MILLISECONDS) % 2 == 0;
  _canvas.Clear();
  Sights(_sight, _canvas);
  Frame(_canvas);
  Title(_dashboard, _renderer, _canvas);
  Indicators(_dashboard, _canvas);

  LeftGauge(_canvas, L"FORE SHIELD", ROWS[0], gauges.foreShield, flashOn);
  LeftGauge(_canvas, L"AFT SHIELD", ROWS[1], gauges.aftShield, flashOn);
  LeftGauge(_canvas, L"FUEL", ROWS[2], gauges.fuel, flashOn);
  LeftGauge(_canvas, L"CABIN TEMP", ROWS[3], gauges.cabinTemperature, flashOn);
  LeftGauge(_canvas, L"LASER TEMP", ROWS[4], gauges.laserTemperature, flashOn);
  LeftGauge(_canvas, L"ALTITUDE", ROWS[5], gauges.altitude, flashOn);
  Missiles(gauges, _canvas);

  Bar(_canvas, RIGHT_BAR_X, RIGHT_BAR_WIDTH, RIGHT_SEGMENTS, ROWS[0], gauges.speed.fill, AMBER);
  Label(_canvas, L"SPEED", RIGHT_LABEL_X, ROWS[0]);
  Indicator(_canvas, L"ROLL", ROWS[1], gauges.roll, gauges.live);
  Indicator(_canvas, L"DIVE / CLIMB", ROWS[2], gauges.pitch, gauges.live);
  constexpr std::array<const wchar_t*, 4> BANKS = {L"ENERGY 1", L"ENERGY 2", L"ENERGY 3", L"ENERGY 4"};
  for (std::size_t bank = 0; bank < BANKS.size(); ++bank)
  {
    const Elite::Gauge& gauge = gauges.energyBanks[bank];
    Bar(_canvas, RIGHT_BAR_X, RIGHT_BAR_WIDTH, RIGHT_SEGMENTS, ROWS[3 + bank], gauge.fill, LevelColor(gauge.level, flashOn));
    Label(_canvas, BANKS[bank], RIGHT_LABEL_X, ROWS[3 + bank]);
  }

  Scanner(_dashboard, _canvas);
  ConditionAndCompass(_dashboard, gauges, flashOn, _canvas);
}

} // namespace Outpost
