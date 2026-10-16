#include "pch.h"

#include "DockedStatus.h"

#include "DockedNames.h"

#include <array>
#include <format>
#include <string_view>

namespace Outpost
{

namespace
{

// D08's palette, read off `docked/08-commander-status.png`.
constexpr std::uint32_t AMBER = 0xFFFFB43C;
constexpr std::uint32_t AMBER_LIGHT = 0xFFFFCF80;
constexpr std::uint32_t TEXT = 0xFFF3E6CF;
constexpr std::uint32_t LABEL = 0xFFC9BCA6;
constexpr std::uint32_t GREEN_LIGHT = 0xFF9FE07A;
constexpr std::uint32_t ALERT_LIGHT = 0xFFFFB09A;
constexpr std::uint32_t RULE = 0xFF64491D;

// The three panels, each 850 units tall from 110: the commander's with its two chamfers, the equipment's with none, the cargo's
// with its top-right one.
constexpr Engine::Rect COMMANDER_PANEL{.left = 60.0f, .top = 110.0f, .width = 600.0f, .height = 850.0f};
constexpr Engine::Rect EQUIPMENT_PANEL{.left = 680.0f, .top = 110.0f, .width = 580.0f, .height = 850.0f};
constexpr Engine::Rect CARGO_PANEL{.left = 1280.0f, .top = 110.0f, .width = 580.0f, .height = 850.0f};
constexpr float INSET = 45.0f; // the text and the rules, in from each panel's sides

// The commander's panel: the title and the name, a rule, and six facts, each a label over its value.
constexpr float TITLE_Y = 158.0f;
constexpr float NAME_Y = 203.5f;
constexpr float NAME_SIZE = 62.0f;
constexpr float NAME_SPACING = 4.0f;
constexpr float COMMANDER_RULE_Y = 261.5f;
constexpr float FIRST_LABEL_Y = 296.0f;
constexpr float FACT_PITCH = 81.0f;
constexpr float VALUE_DOWN = 30.0f; // a value's middle, below its label's
constexpr float VALUE_SIZE = 30.0f;

// The equipment's and the cargo's panels: a heading, a rule, and a row each.
constexpr float HEADING_Y = 158.0f;
constexpr float HEADING_SIZE = 17.5f;
constexpr float HEADING_SPACING = 3.0f;
constexpr float LIST_RULE_Y = 189.5f;
constexpr float FIRST_ROW_Y = 221.5f;
constexpr float ROW_PITCH = 46.0f;
constexpr float ROW_SIZE = 25.0f;
constexpr float BULLET = 10.0f;
constexpr float BULLET_TO_TEXT = 26.0f; // from the bullet's left to the item's

constexpr float SMALL_LABEL = 15.0f;
constexpr float SMALL_SPACING = 4.0f;

// The original's mounts as D08 names them, in the order D03 draws them (D104): the fore mount is the front, the aft the rear.
constexpr std::array<std::pair<std::uint8_t, const wchar_t*>, 4> MOUNTS = {{
  {Elite::MOUNT_FORE, L"front"},
  {Elite::MOUNT_LEFT, L"left"},
  {Elite::MOUNT_RIGHT, L"right"},
  {Elite::MOUNT_AFT, L"rear"},
}};

constexpr std::array<const wchar_t*, 9> RATINGS = {
  L"Harmless", L"Mostly Harmless", L"Poor", L"Average", L"Above Average", L"Competent", L"Dangerous", L"Deadly", L"Elite",
};

[[nodiscard]] std::wstring Wide(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

// A label of the commander's panel over its value, the fact _index down.
void Fact(const wchar_t* _label, const std::wstring& _value, std::uint32_t _color, std::size_t _index, Engine::Canvas& _canvas)
{
  const float y = FIRST_LABEL_Y + FACT_PITCH * static_cast<float>(_index);
  const float left = COMMANDER_PANEL.left + INSET;
  _canvas.Text(_label, Engine::Font::Label, SMALL_LABEL, SMALL_SPACING, Engine::Point{.x = left, .y = y}, Engine::Align::Left, LABEL);
  _canvas.Text(_value, Engine::Font::Label, VALUE_SIZE, 0.0f, Engine::Point{.x = left, .y = y + VALUE_DOWN}, Engine::Align::Left, _color);
}

// A list panel's heading and its rule.
void Heading(const Engine::Rect& _panel, const wchar_t* _text, Engine::Canvas& _canvas)
{
  const float left = _panel.left + INSET;
  _canvas.Text(_text, Engine::Font::Label, HEADING_SIZE, HEADING_SPACING, Engine::Point{.x = left, .y = HEADING_Y}, Engine::Align::Left,
               AMBER);
  _canvas.Line(Engine::Point{.x = left, .y = LIST_RULE_Y}, Engine::Point{.x = _panel.left + _panel.width - INSET, .y = LIST_RULE_Y}, 1.0f,
               RULE);
}

[[nodiscard]] float RowY(std::size_t _row) noexcept
{
  return FIRST_ROW_Y + ROW_PITCH * static_cast<float>(_row);
}

void Commander(const Elite::StatusView& _view, Engine::Canvas& _canvas)
{
  DockedFrame::Panel(COMMANDER_PANEL, true, true, _canvas);
  const float left = COMMANDER_PANEL.left + INSET;
  _canvas.Text(Wide(_view.title), Engine::Font::Label, SMALL_LABEL, SMALL_SPACING, Engine::Point{.x = left, .y = TITLE_Y},
               Engine::Align::Left, LABEL);
  _canvas.Text(Wide(_view.commanderName), Engine::Font::Label, NAME_SIZE, NAME_SPACING, Engine::Point{.x = left, .y = NAME_Y},
               Engine::Align::Left, AMBER_LIGHT);
  _canvas.Line(Engine::Point{.x = left, .y = COMMANDER_RULE_Y},
               Engine::Point{.x = COMMANDER_PANEL.left + COMMANDER_PANEL.width - INSET, .y = COMMANDER_RULE_Y}, 1.0f, RULE);
  const std::wstring witch = L"WITCH SPACE";
  Fact(L"PRESENT SYSTEM", _view.witchSpace ? witch : Wide(_view.presentSystem), TEXT, 0, _canvas);
  Fact(L"HYPERSPACE TARGET", _view.witchSpace ? witch : Wide(_view.hyperspaceTarget), TEXT, 1, _canvas);
  Fact(L"FUEL", std::format(L"{}.{} light years", _view.fuelTenths / 10, _view.fuelTenths % 10), TEXT, 2, _canvas);
  Fact(L"CASH", DockedFrame::Tenths(_view.creditsTenths) + L" credits", AMBER_LIGHT, 3, _canvas);
  switch (_view.legal)
  {
  case Elite::LegalStanding::Clean:
    Fact(L"LEGAL STATUS", L"Clean", GREEN_LIGHT, 4, _canvas);
    break;
  case Elite::LegalStanding::Offender:
    Fact(L"LEGAL STATUS", L"Offender", AMBER_LIGHT, 4, _canvas);
    break;
  case Elite::LegalStanding::Fugitive:
    Fact(L"LEGAL STATUS", L"Fugitive", ALERT_LIGHT, 4, _canvas);
    break;
  }
  Fact(L"RATING", _view.rating < RATINGS.size() ? RATINGS[_view.rating] : RATINGS.back(), TEXT, 5, _canvas);
}

void Equipment(const Elite::StatusView& _view, Engine::Canvas& _canvas)
{
  DockedFrame::Panel(EQUIPMENT_PANEL, false, false, _canvas);
  Heading(EQUIPMENT_PANEL, L"EQUIPMENT FITTED", _canvas);
  const float left = EQUIPMENT_PANEL.left + INSET;
  for (std::size_t row = 0; row < _view.equipment.size(); ++row)
  {
    const float y = RowY(row);
    _canvas.Box(Engine::Rect{.left = left, .top = y - BULLET / 2.0f + 1.0f, .width = BULLET, .height = BULLET}, AMBER);
    _canvas.Text(DockedStatus::ItemText(_view.equipment[row]), Engine::Font::Label, ROW_SIZE, 0.0f,
                 Engine::Point{.x = left + BULLET_TO_TEXT, .y = y}, Engine::Align::Left, TEXT);
  }
}

void Cargo(const Elite::StatusView& _view, Engine::Canvas& _canvas)
{
  DockedFrame::Panel(CARGO_PANEL, true, false, _canvas);
  Heading(CARGO_PANEL, L"CARGO", _canvas);
  const float left = CARGO_PANEL.left + INSET;
  const float right = CARGO_PANEL.left + CARGO_PANEL.width - INSET;
  // A right-aligned spaced text's spacing after its last letter is outside it: its anchor is moved out by it.
  _canvas.Text(std::format(L"{} / {} t", _view.usedTonnes, _view.bayTonnes), Engine::Font::Number, HEADING_SIZE, HEADING_SPACING,
               Engine::Point{.x = right + HEADING_SPACING, .y = HEADING_Y}, Engine::Align::Right, AMBER_LIGHT);
  std::size_t row = 0;
  const auto line = [&](const wchar_t* _name, std::uint8_t _amount, const wchar_t* _unit)
  {
    const float y = RowY(row++);
    _canvas.Text(_name, Engine::Font::Label, ROW_SIZE, 0.0f, Engine::Point{.x = left, .y = y}, Engine::Align::Left, TEXT);
    _canvas.Text(std::format(L"{} {}", _amount, _unit), Engine::Font::Number, ROW_SIZE, 0.0f, Engine::Point{.x = right, .y = y},
                 Engine::Align::Right, AMBER_LIGHT);
  };
  for (std::size_t commodity = 0; commodity < Elite::COMMODITY_KINDS; ++commodity)
  {
    if (_view.held[commodity] != 0)
    {
      line(DockedNames::Commodity(commodity), _view.held[commodity], DockedNames::Unit(commodity));
    }
  }
  if (_view.refugeesTonnes != 0)
  {
    line(L"Refugees", _view.refugeesTonnes, L"t");
  }
}

} // namespace

std::wstring DockedStatus::ItemText(const Elite::FittedItem& _item)
{
  if (_item.kind == Elite::FittedKind::Missiles)
  {
    return std::format(L"Missiles ({})", _item.count);
  }
  std::wstring text = DockedNames::Equipment(_item.kind);
  if (_item.mounts == 0)
  {
    return text;
  }
  std::wstring mounts;
  for (const auto& [bit, name] : MOUNTS)
  {
    if ((_item.mounts & bit) != 0)
    {
      mounts += mounts.empty() ? name : std::wstring(L", ") + name;
    }
  }
  return text + L" (" + mounts + L")";
}

void DockedStatus::Draw(const Elite::StatusView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                        Engine::Canvas& _canvas)
{
  _canvas.Clear();
  const std::wstring system = Wide(_view.systemName);
  DockedFrame::Draw(DockedFrame::Desc{.location = std::format(L"DOCKED · {} STATION · {}", _view.dodo ? L"DODO" : L"CORIOLIS", system),
                                      .title = L"COMMANDER STATUS",
                                      .creditsTenths = _view.creditsTenths,
                                      .lit = DockedKey::Status},
                    _pointer, _measure, _canvas);
  Commander(_view, _canvas);
  Equipment(_view, _canvas);
  Cargo(_view, _canvas);
}

} // namespace Outpost
