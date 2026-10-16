#include "pch.h"

#include "DockedEquip.h"

#include "DockedNames.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>
#include <string_view>
#include <vector>

namespace Outpost
{

namespace
{

// The shipyard's palette, read off `docked/02-equip-ship-tl6.png` and `docked/03-equip-ship-tl10-laser-mount.png`.
constexpr std::uint32_t AMBER = 0xFFFFB43C;
constexpr std::uint32_t AMBER_LIGHT = 0xFFFFCF80;
constexpr std::uint32_t TEXT = 0xFFF3E6CF;
constexpr std::uint32_t LABEL = 0xFFC9BCA6;
constexpr std::uint32_t RULE = 0xFF64491D;
constexpr std::uint32_t PICKER_FILL = 0xFF181409;
constexpr std::uint32_t PICKER_GLOW = 0x18FFB43C;
constexpr std::uint32_t MOUNT_FILL = 0xFF332712;
constexpr std::uint32_t MOUNT_HOVER_FILL = 0xFF453416;
constexpr std::uint32_t MOUNT_OFF_EDGE = 0xFF3A2E1C;
constexpr std::uint32_t MOUNT_OFF_TEXT = 0xFF6E6150;
constexpr std::uint32_t CANCEL_EDGE = 0xFF745320;
constexpr std::uint32_t SHIP_EDGE = 0xFF5D431B;

// The list: its panel, its header and its rows, which share D01's table and are 64 units apart, or closer to fit the 728 units the
// fourteen take.
constexpr Engine::Rect TABLE{.left = 60.0f, .top = 110.0f, .width = 1281.0f, .height = 852.0f};
constexpr float HEADER_Y = 141.5f;
constexpr float HEADER_RULE_Y = 166.5f;
constexpr float ROWS_TOP = 175.0f;
constexpr float ROWS_LEFT = 81.0f;
constexpr float ROWS_WIDTH = 1240.0f;
constexpr float ROWS_HEIGHT_MOST = 728.0f;
constexpr float ROW_PITCH_MOST = 64.0f;
constexpr float NAME_X = 97.0f;
constexpr float BUY_RIGHT = 884.0f;
constexpr float SELL_RIGHT = 1084.0f;
constexpr float FITTED_RIGHT = 1305.0f;
constexpr float ROW_SIZE_TALL = 25.0f;  // D02's rows, 64 units apart
constexpr float ROW_SIZE_DENSE = 23.5f; // D03's, 52
constexpr float HEADER_SIZE = 16.0f;
constexpr float HEADER_SPACING = 3.0f;

// The Selected panel and what is in it.
constexpr Engine::Rect SELECTED{.left = 1380.0f, .top = 110.0f, .width = 479.0f, .height = 322.0f};
constexpr float INSIDE_LEFT = 1413.0f;
constexpr float INSIDE_RIGHT = 1827.0f;
constexpr float SMALL_LABEL = 15.0f;
constexpr float SMALL_SPACING = 4.0f;
constexpr Engine::Rect BUY{.left = 1413.0f, .top = 340.0f, .width = 200.0f, .height = 64.0f};
constexpr Engine::Rect SELL{.left = 1627.0f, .top = 340.0f, .width = 200.0f, .height = 64.0f};

// The mount picker (D03), its boxes in the order D104 numbers them: front, left, right and rear; and the ship's between them.
constexpr Engine::Rect PICKER{.left = 1380.0f, .top = 453.0f, .width = 480.0f, .height = 351.0f};
constexpr float PICKER_LEFT = 1405.0f;
constexpr float PICKER_TITLE_Y = 485.0f;
constexpr float PICKER_TITLE_LINE = 22.0f;
constexpr float PICKER_TITLE_WIDTH = 311.0f; // up to ESC CANCEL
constexpr float PICKER_TITLE_SIZE = 17.0f;
constexpr float PICKER_TITLE_SPACING = 3.0f;
constexpr Engine::Rect CANCEL{.left = 1730.0f, .top = 482.0f, .width = 105.0f, .height = 32.0f};
constexpr std::array<Engine::Rect, Elite::LASER_MOUNT_COUNT> MOUNT_BOXES = {{
  {.left = 1551.0f, .top = 537.0f, .width = 138.0f, .height = 64.0f},
  {.left = 1405.0f, .top = 609.0f, .width = 138.0f, .height = 64.0f},
  {.left = 1697.0f, .top = 609.0f, .width = 138.0f, .height = 64.0f},
  {.left = 1551.0f, .top = 681.0f, .width = 138.0f, .height = 64.0f},
}};
constexpr Engine::Rect SHIP{.left = 1551.0f, .top = 609.0f, .width = 138.0f, .height = 64.0f};
constexpr float MOUNT_NAME_DOWN = 20.5f;
constexpr float MOUNT_LASER_DOWN = 41.0f;
constexpr float NOTE_Y = 770.5f;

// The strip, under the Selected panel, or under the picker while it is open; and the footer's two lines.
constexpr Engine::Rect STRIP{.left = 1380.0f, .top = 453.0f, .width = 479.0f, .height = 64.0f};
constexpr Engine::Rect STRIP_UNDER_PICKER{.left = 1380.0f, .top = 823.0f, .width = 479.0f, .height = 64.0f};
constexpr Engine::Point FOOTER{.x = 1380.0f, .y = 921.0f};
constexpr float FOOTER_LINE = 26.0f;

// The rows the screen treats apart, from 1.
constexpr std::uint8_t FUEL_ROW = 1;
constexpr std::uint8_t MISSILE_ROW = 2;
constexpr std::uint8_t MISSILES_MOST = 4;
constexpr std::array<std::uint8_t, 4> LASER_ROWS = {5, 6, 13, 14}; // pulse, beam, mining and military

[[nodiscard]] std::wstring Upper(std::wstring _text)
{
  std::ranges::transform(_text, _text.begin(), [](wchar_t _letter) { return static_cast<wchar_t>(std::towupper(_letter)); });
  return _text;
}

// Row _row's name in a strip's capitals: FUEL for fuel's.
[[nodiscard]] std::wstring NameOf(std::uint8_t _row)
{
  return _row == FUEL_ROW ? std::wstring(L"FUEL") : Upper(DockedNames::ListItem(_row));
}

// Mount _mount in a strip's capitals: REAR MOUNT.
[[nodiscard]] std::wstring MountOf(std::uint8_t _mount)
{
  return Upper(DockedNames::Mount(_mount)) + L" MOUNT";
}

// _fuelTenths of a light year: 7.0.
[[nodiscard]] std::wstring LightYears(std::uint8_t _fuelTenths)
{
  return std::format(L"{}.{}", _fuelTenths / 10, _fuelTenths % 10);
}

[[nodiscard]] bool IsLaserRow(std::uint8_t _row) noexcept
{
  return std::ranges::find(LASER_ROWS, _row) != LASER_ROWS.end();
}

// The distance between the rows of _view's list: 64 units, or less for as many as the fourteen.
[[nodiscard]] float RowPitch(const Elite::EquipView& _view) noexcept
{
  return _view.rows == 0 ? ROW_PITCH_MOST : std::min(ROW_PITCH_MOST, ROWS_HEIGHT_MOST / static_cast<float>(_view.rows));
}

[[nodiscard]] Engine::Rect RowRect(std::size_t _row, float _pitch) noexcept
{
  return Engine::Rect{.left = ROWS_LEFT, .top = ROWS_TOP + _pitch * static_cast<float>(_row), .width = ROWS_WIDTH, .height = _pitch};
}

[[nodiscard]] Engine::Point Middle(const Engine::Rect& _rect) noexcept
{
  return Engine::Point{.x = _rect.left + _rect.width / 2.0f, .y = _rect.top + _rect.height / 2.0f};
}

void Header(std::wstring_view _text, float _x, Engine::Align _align, Engine::Canvas& _canvas)
{
  _canvas.Text(_text, Engine::Font::Label, HEADER_SIZE, HEADER_SPACING, Engine::Point{.x = _x, .y = HEADER_Y}, _align, AMBER);
}

void List(const Elite::EquipView& _view, std::optional<Engine::Point> _pointer, Engine::Canvas& _canvas)
{
  DockedFrame::Panel(TABLE, true, true, _canvas);
  Header(std::format(L"EQUIPMENT · TECH LEVEL {} · {} ITEMS", _view.techLevel, _view.rows), NAME_X, Engine::Align::Left, _canvas);
  // A right-aligned header's spacing after its last letter is outside the column: its anchor is moved out by it.
  Header(L"BUY", BUY_RIGHT + HEADER_SPACING, Engine::Align::Right, _canvas);
  Header(L"SELL", SELL_RIGHT + HEADER_SPACING, Engine::Align::Right, _canvas);
  Header(L"FITTED", FITTED_RIGHT + HEADER_SPACING, Engine::Align::Right, _canvas);
  _canvas.Line(Engine::Point{.x = TABLE.left, .y = HEADER_RULE_Y}, Engine::Point{.x = TABLE.left + TABLE.width, .y = HEADER_RULE_Y}, 1.0f,
               RULE);

  const float pitch = RowPitch(_view);
  const float size = pitch < ROW_PITCH_MOST ? ROW_SIZE_DENSE : ROW_SIZE_TALL;
  for (std::uint8_t index = 0; index < _view.rows && index < Elite::EQUIP_ROWS_MOST; ++index)
  {
    const auto row = static_cast<std::uint8_t>(index + 1);
    const Elite::EquipRowView& item = _view.items[index];
    const Engine::Rect rect = RowRect(index, pitch);
    const DockedFrame::RowLook look =
      DockedFrame::RowLookOf(index, index == _view.selected, _pointer && DockedFrame::Contains(rect, *_pointer), true);
    DockedFrame::Row(rect, look, _canvas);
    const float y = rect.top + pitch / 2.0f - 1.0f;
    const std::uint32_t text = DockedFrame::RowText(look);
    _canvas.Text(DockedNames::ListItem(row), Engine::Font::Label, size, 0.0f, Engine::Point{.x = NAME_X, .y = y}, Engine::Align::Left,
                 text);
    _canvas.Text(DockedFrame::Tenths(item.buyTenths), Engine::Font::Number, size, 0.0f, Engine::Point{.x = BUY_RIGHT, .y = y},
                 Engine::Align::Right, text);
    _canvas.Text(row == FUEL_ROW ? std::wstring(L"—") : DockedFrame::Tenths(item.sellTenths), Engine::Font::Number, size, 0.0f,
                 Engine::Point{.x = SELL_RIGHT, .y = y}, Engine::Align::Right, text);
    _canvas.Text(DockedEquip::FittedText(_view, row), Engine::Font::Number, size, 0.0f, Engine::Point{.x = FITTED_RIGHT, .y = y},
                 Engine::Align::Right, text);
  }
}

// One of the Selected panel's rows: its label, and its value at the right.
void Detail(const wchar_t* _label, const std::wstring& _value, float _y, Engine::Canvas& _canvas)
{
  _canvas.Text(_label, Engine::Font::Label, 20.0f, 0.0f, Engine::Point{.x = INSIDE_LEFT, .y = _y}, Engine::Align::Left, LABEL);
  _canvas.Text(_value, Engine::Font::Number, 20.0f, 0.0f, Engine::Point{.x = INSIDE_RIGHT, .y = _y}, Engine::Align::Right, TEXT);
}

void SelectedPanel(const Elite::EquipView& _view, std::optional<Engine::Point> _pointer, Engine::Canvas& _canvas)
{
  const auto row = static_cast<std::uint8_t>(_view.selected + 1);
  const Elite::EquipRowView& item = _view.items[std::min<std::size_t>(_view.selected, Elite::EQUIP_ROWS_MOST - 1)];
  DockedFrame::Panel(SELECTED, true, false, _canvas);
  _canvas.Text(L"SELECTED", Engine::Font::Label, SMALL_LABEL, SMALL_SPACING, Engine::Point{.x = INSIDE_LEFT, .y = 145.5f},
               Engine::Align::Left, LABEL);
  _canvas.Text(DockedNames::ListItem(row), Engine::Font::Label, 40.0f, 0.0f, Engine::Point{.x = INSIDE_LEFT, .y = 189.5f},
               Engine::Align::Left, AMBER_LIGHT);
  Detail(L"Buy", DockedFrame::Tenths(item.buyTenths), 238.5f, _canvas);
  Detail(L"Sell", row == FUEL_ROW ? std::wstring(L"—") : DockedFrame::Tenths(item.sellTenths), 270.5f, _canvas);
  Detail(L"Fitted", DockedEquip::FittedText(_view, row), 302.5f, _canvas);

  // [B] BUY is lit as the screen's first action, as drawn, unless the pointer is on [S] SELL; each is off when it would be refused.
  using enum DockedFrame::ButtonLook;
  const bool onSell = _pointer && DockedFrame::Contains(SELL, *_pointer);
  DockedFrame::Button(BUY, !_view.buyAllowed ? Off : (onSell ? Plain : Lit), L"[B] BUY", Engine::Font::Label, 22.0f, 4.0f, _canvas);
  DockedFrame::Button(SELL, !_view.sellAllowed ? Off : (onSell ? Lit : Plain), L"[S] SELL", Engine::Font::Label, 22.0f, 4.0f, _canvas);
}

// _text broken at its spaces into lines no wider than _width, as _measure measures them in _font at _size and _spacing.
[[nodiscard]] std::vector<std::wstring> Wrapped(const std::wstring& _text, float _width, float _size, float _spacing,
                                                const TextMeasure& _measure)
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
    Engine::CanvasText measured;
    measured.text = longer;
    measured.size = _size;
    measured.spacing = _spacing;
    if (!line.empty() && _measure && _measure(measured) > _width)
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

void Picker(const Elite::EquipView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure, Engine::Canvas& _canvas)
{
  const Elite::PickerView& picker = _view.picker;
  _canvas.Box(
    Engine::Rect{.left = PICKER.left - 6.0f, .top = PICKER.top - 6.0f, .width = PICKER.width + 12.0f, .height = PICKER.height + 12.0f},
    PICKER_GLOW, 8.0f);
  _canvas.Box(PICKER, PICKER_FILL);
  DockedFrame::Edge(PICKER, AMBER, _canvas);

  // The title (D03, D103), broken to fit beside ESC CANCEL.
  const std::wstring title =
    std::format(L"{} {} — CHOOSE MOUNT", picker.selling ? L"SELL" : L"BUY", Upper(DockedNames::ListItem(picker.row)));
  float y = PICKER_TITLE_Y;
  for (const std::wstring& line : Wrapped(title, PICKER_TITLE_WIDTH, PICKER_TITLE_SIZE, PICKER_TITLE_SPACING, _measure))
  {
    _canvas.Text(line, Engine::Font::Label, PICKER_TITLE_SIZE, PICKER_TITLE_SPACING, Engine::Point{.x = PICKER_LEFT, .y = y},
                 Engine::Align::Left, AMBER);
    y += PICKER_TITLE_LINE;
  }
  const bool onCancel = _pointer && DockedFrame::Contains(CANCEL, *_pointer);
  DockedFrame::Edge(CANCEL, onCancel ? AMBER : CANCEL_EDGE, _canvas);
  Engine::Point cancel = Middle(CANCEL);
  cancel.x += 1.0f;
  _canvas.Text(L"ESC CANCEL", Engine::Font::Label, 13.0f, 2.0f, cancel, Engine::Align::Center, LABEL);

  // The ship's place between its mounts, then each mount: its key and name, and the laser it carries.
  _canvas.Line(Engine::Point{.x = SHIP.left + 0.5f, .y = SHIP.top + 0.5f},
               Engine::Point{.x = SHIP.left + SHIP.width - 0.5f, .y = SHIP.top + 0.5f}, 1.0f, SHIP_EDGE, 3.0f, 3.0f);
  _canvas.Line(Engine::Point{.x = SHIP.left + 0.5f, .y = SHIP.top + SHIP.height - 0.5f},
               Engine::Point{.x = SHIP.left + SHIP.width - 0.5f, .y = SHIP.top + SHIP.height - 0.5f}, 1.0f, SHIP_EDGE, 3.0f, 3.0f);
  _canvas.Line(Engine::Point{.x = SHIP.left + 0.5f, .y = SHIP.top + 0.5f},
               Engine::Point{.x = SHIP.left + 0.5f, .y = SHIP.top + SHIP.height - 0.5f}, 1.0f, SHIP_EDGE, 3.0f, 3.0f);
  _canvas.Line(Engine::Point{.x = SHIP.left + SHIP.width - 0.5f, .y = SHIP.top + 0.5f},
               Engine::Point{.x = SHIP.left + SHIP.width - 0.5f, .y = SHIP.top + SHIP.height - 0.5f}, 1.0f, SHIP_EDGE, 3.0f, 3.0f);
  const Engine::Point ship = Middle(SHIP);
  _canvas.Text(L"SHIP", Engine::Font::Label, 12.0f, 2.0f, Engine::Point{.x = ship.x + 1.0f, .y = ship.y - 7.5f}, Engine::Align::Center,
               LABEL);
  _canvas.Text(L"MOUNTS", Engine::Font::Label, 12.0f, 2.0f, Engine::Point{.x = ship.x + 1.0f, .y = ship.y + 7.5f}, Engine::Align::Center,
               LABEL);
  for (std::size_t key = 0; key < Elite::LASER_MOUNT_COUNT; ++key)
  {
    const std::uint8_t mount = Elite::MOUNT_OF_PICKER_KEY[key];
    const Elite::MountView& shown = picker.mounts[mount];
    const Engine::Rect& box = MOUNT_BOXES[key];
    if (shown.allowed)
    {
      _canvas.Box(box, _pointer && DockedFrame::Contains(box, *_pointer) ? MOUNT_HOVER_FILL : MOUNT_FILL);
    }
    DockedFrame::Edge(box, shown.allowed ? AMBER : MOUNT_OFF_EDGE, _canvas);
    const float middle = box.left + box.width / 2.0f;
    _canvas.Text(std::format(L"{} {}", key + 1, Upper(DockedNames::Mount(mount))), Engine::Font::Label, 13.0f, 2.0f,
                 Engine::Point{.x = middle + 1.0f, .y = box.top + MOUNT_NAME_DOWN}, Engine::Align::Center,
                 shown.allowed ? AMBER_LIGHT : MOUNT_OFF_TEXT);
    _canvas.Text(shown.laser ? Upper(DockedNames::LaserType(shown.type)) : std::wstring(L"EMPTY"), Engine::Font::Label, 14.0f, 1.0f,
                 Engine::Point{.x = middle + 0.5f, .y = box.top + MOUNT_LASER_DOWN}, Engine::Align::Center,
                 shown.allowed ? TEXT : MOUNT_OFF_TEXT);
  }
  if (!picker.selling)
  {
    _canvas.Text(L"A mount carrying another laser trades it in at its sell price.", Engine::Font::Label, 15.0f, 0.0f,
                 Engine::Point{.x = PICKER_LEFT, .y = NOTE_Y}, Engine::Align::Left, LABEL);
  }
}

// Whether _notice is a trade that happened, drawn in amber with its ✓.
[[nodiscard]] bool IsSuccess(Elite::EquipNotice _notice) noexcept
{
  using enum Elite::EquipNotice;
  return _notice == Bought || _notice == Sold || _notice == LaserFitted || _notice == LaserSold;
}

} // namespace

void DockedEquip::Draw(const Elite::EquipView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                       Engine::Canvas& _canvas)
{
  _canvas.Clear();
  const std::wstring system = Upper(std::wstring(_view.systemName.begin(), _view.systemName.end()));
  DockedFrame::Draw(DockedFrame::Desc{.location = std::format(L"DOCKED · {} STATION · {}", _view.dodo ? L"DODO" : L"CORIOLIS", system),
                                      .title = L"EQUIP SHIP",
                                      .creditsTenths = _view.creditsTenths,
                                      .lit = DockedKey::Equip},
                    _pointer, _measure, _canvas);
  List(_view, _pointer, _canvas);
  SelectedPanel(_view, _pointer, _canvas);
  if (_view.picker.open)
  {
    Picker(_view, _pointer, _measure, _canvas);
  }
  if (_view.message.notice != Elite::EquipNotice::None)
  {
    const Words words = WordsOf(_view.message);
    DockedFrame::Strip(_view.picker.open ? STRIP_UNDER_PICKER : STRIP,
                       IsSuccess(_view.message.notice) ? DockedFrame::Tone::Success : DockedFrame::Tone::Refusal, words.headline,
                       words.detail, _canvas);
  }
  _canvas.Text(L"↑ ↓ or click to select · B buy · S sell · 1–4 choose laser mount ·", Engine::Font::Label, 17.0f, 0.0f, FOOTER,
               Engine::Align::Left, LABEL);
  _canvas.Text(L"Esc cancel", Engine::Font::Label, 17.0f, 0.0f, Engine::Point{.x = FOOTER.x, .y = FOOTER.y + FOOTER_LINE},
               Engine::Align::Left, LABEL);
}

std::optional<Elite::EquipClick> DockedEquip::ControlAt(const Elite::EquipView& _view, Engine::Point _point) noexcept
{
  if (_view.picker.open)
  {
    for (std::size_t key = 0; key < Elite::LASER_MOUNT_COUNT; ++key)
    {
      if (DockedFrame::Contains(MOUNT_BOXES[key], _point))
      {
        return Elite::EquipClick{.control = Elite::EquipControl::Mount, .index = Elite::MOUNT_OF_PICKER_KEY[key]};
      }
    }
    if (DockedFrame::Contains(CANCEL, _point))
    {
      return Elite::EquipClick{.control = Elite::EquipControl::Cancel, .index = 0};
    }
  }
  const float pitch = RowPitch(_view);
  for (std::uint8_t row = 0; row < _view.rows && row < Elite::EQUIP_ROWS_MOST; ++row)
  {
    if (DockedFrame::Contains(RowRect(row, pitch), _point))
    {
      return Elite::EquipClick{.control = Elite::EquipControl::Row, .index = row};
    }
  }
  if (DockedFrame::Contains(BUY, _point))
  {
    return Elite::EquipClick{.control = Elite::EquipControl::Buy, .index = 0};
  }
  if (DockedFrame::Contains(SELL, _point))
  {
    return Elite::EquipClick{.control = Elite::EquipControl::Sell, .index = 0};
  }
  return std::nullopt;
}

std::wstring DockedEquip::FittedText(const Elite::EquipView& _view, std::uint8_t _row)
{
  if (_row == 0 || _row > Elite::EQUIP_ROWS_MOST)
  {
    return {};
  }
  const Elite::EquipRowView& item = _view.items[std::size_t{_row} - 1];
  if (_row == FUEL_ROW)
  {
    return LightYears(_view.fuelTenths) + L" LY";
  }
  if (_row == MISSILE_ROW)
  {
    return std::format(L"{} / {}", item.count, MISSILES_MOST);
  }
  if (IsLaserRow(_row))
  {
    std::wstring mounts;
    for (const std::uint8_t mount : Elite::MOUNT_OF_PICKER_KEY)
    {
      if ((item.mounts & (1u << mount)) != 0)
      {
        mounts += (mounts.empty() ? L"" : L" · ") + Upper(DockedNames::Mount(mount));
      }
    }
    return mounts.empty() ? std::wstring(L"—") : mounts;
  }
  return item.count != 0 ? std::wstring(L"FITTED") : std::wstring(L"—");
}

DockedEquip::Words DockedEquip::WordsOf(const Elite::EquipMessage& _message)
{
  const std::wstring name = NameOf(_message.row);
  switch (_message.notice)
  {
  case Elite::EquipNotice::None:
    break;
  case Elite::EquipNotice::Bought:
    if (_message.row == FUEL_ROW)
    {
      return Words{L"FUEL TANKS FILLED",
                   std::format(L"{} LIGHT YEARS · {} CR", LightYears(_message.fuelTenths), DockedFrame::Tenths(_message.tenths))};
    }
    return Words{std::format(L"BOUGHT {}", name), std::format(L"{} CR", DockedFrame::Tenths(_message.tenths))};
  case Elite::EquipNotice::Sold:
    return Words{std::format(L"SOLD {}", name), std::format(L"{} CR", DockedFrame::Tenths(_message.tenths))};
  case Elite::EquipNotice::LaserFitted:
    return Words{std::format(L"{} FITTED", name), _message.tradedIn
                                                    ? std::format(L"{} · {} TRADED IN", MountOf(_message.mount), NameOf(_message.tradedRow))
                                                    : MountOf(_message.mount)};
  case Elite::EquipNotice::LaserSold:
    return Words{std::format(L"SOLD {}", name), std::format(L"{} · {} CR", MountOf(_message.mount), DockedFrame::Tenths(_message.tenths))};
  case Elite::EquipNotice::FuelFull:
    return Words{L"FUEL TANKS FULL", std::format(L"{} LIGHT YEARS", LightYears(_message.fuelTenths))};
  case Elite::EquipNotice::NoFuel:
    return Words{L"NO FUEL ON SALE", L"DURING THE FIRST MISSION"};
  case Elite::EquipNotice::FourMissiles:
    return Words{L"FOUR MISSILES ONLY", L"ALL PYLONS LOADED"};
  case Elite::EquipNotice::AlreadyFitted:
    return Words{name, L"ALREADY FITTED"};
  case Elite::EquipNotice::FourLasers:
    return Words{L"FOUR LASERS", std::format(L"EVERY MOUNT ALREADY CARRIES A {}", name)};
  case Elite::EquipNotice::ScoopsNeeded:
    return Words{L"FUEL SCOOPS NEEDED", L"BEFORE A MINING LASER CAN BE FITTED"};
  case Elite::EquipNotice::TooMuchCargo:
    return Words{L"TOO MUCH CARGO TO SELL THE EXTENSION", std::format(L"UNLOAD {} T FIRST", _message.tonnes)};
  case Elite::EquipNotice::MiningLaserFitted:
    return Words{L"MINING LASER FITTED", L"FUEL SCOOPS CANNOT BE SOLD"};
  case Elite::EquipNotice::NotEnoughCredits:
    return Words{L"NOT ENOUGH CREDITS", std::format(L"{} CR NEEDED, {} CR AVAILABLE", DockedFrame::Tenths(_message.tenths),
                                                    DockedFrame::Tenths(_message.creditsTenths))};
  case Elite::EquipNotice::FuelNotBought:
    return Words{L"FUEL IS NOT BOUGHT BACK", {}};
  case Elite::EquipNotice::NoneFitted:
    return Words{L"NOTHING TO SELL", std::format(L"NO {} FITTED", name)};
  case Elite::EquipNotice::MountTaken:
    return Words{MountOf(_message.mount), std::format(L"ALREADY CARRIES A {}", name)};
  case Elite::EquipNotice::MountWithout:
    return Words{MountOf(_message.mount), std::format(L"CARRIES NO {}", name)};
  }
  return Words{};
}

} // namespace Outpost
