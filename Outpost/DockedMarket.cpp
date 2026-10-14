#include "pch.h"

#include "DockedMarket.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <format>
#include <utility>

namespace Outpost
{

namespace
{

// The market's palette, read off `docked/01-market.png`.
constexpr std::uint32_t AMBER = 0xFFFFB43C;
constexpr std::uint32_t AMBER_LIGHT = 0xFFFFCF80;
constexpr std::uint32_t TEXT = 0xFFF3E6CF;
constexpr std::uint32_t LABEL = 0xFFC9BCA6;
constexpr std::uint32_t UNIT_SELECTED = 0xFFD1A866;
constexpr std::uint32_t ROW_STRIPE = 0xFF161411;
constexpr std::uint32_t ROW_SELECTED = 0xFF453416;
constexpr std::uint32_t ROW_HOVER = 0xFF221C14;
constexpr std::uint32_t RULE = 0xFF64491D;
constexpr std::uint32_t CONTROL_EDGE = 0xFF886124;
constexpr std::uint32_t CONTROL_LIT_FILL = 0xFF382A14;
constexpr std::uint32_t DISABLED_EDGE = 0xFF3A2E1C;
constexpr std::uint32_t DISABLED_TEXT = 0xFF6E6150;
constexpr std::uint32_t TRACK = 0xFF1E170E;
constexpr std::uint32_t GLOW = 0x55FFB43C;

// The table: its panel, its header and its rows.
constexpr Engine::Rect TABLE{.left = 60.0f, .top = 110.0f, .width = 1281.0f, .height = 852.0f};
constexpr float HEADER_Y = 141.5f;
constexpr float HEADER_RULE_Y = 166.5f;
constexpr float ROWS_TOP = 175.0f;
constexpr float ROW_PITCH = 42.0f;
constexpr float ROWS_LEFT = 81.0f;
constexpr float ROWS_WIDTH = 1240.0f;
constexpr float ROW_TEXT = 19.0f; // a row's text, down from its top
constexpr float NAME_X = 97.0f;
constexpr float UNIT_X = 585.0f;
constexpr float BUY_RIGHT = 823.0f;
constexpr float SELL_RIGHT = 983.0f;
constexpr float STOCK_RIGHT = 1143.0f;
constexpr float HOLD_RIGHT = 1303.0f;
constexpr float ROW_SIZE = 22.0f;
constexpr float HEADER_SIZE = 16.0f;
constexpr float HEADER_SPACING = 3.0f;

// The Selected panel, the cargo hold's, and what is in them.
constexpr Engine::Rect SELECTED{.left = 1380.0f, .top = 110.0f, .width = 479.0f, .height = 418.0f};
constexpr Engine::Rect HOLD{.left = 1380.0f, .top = 549.0f, .width = 479.0f, .height = 94.0f};
constexpr float INSIDE_LEFT = 1413.0f;
constexpr float INSIDE_RIGHT = 1827.0f;
constexpr float SMALL_LABEL = 15.0f;
constexpr float SMALL_SPACING = 4.0f;
constexpr Engine::Rect LESS{.left = 1557.0f, .top = 369.0f, .width = 48.0f, .height = 48.0f};
constexpr Engine::Rect QUANTITY{.left = 1615.0f, .top = 369.0f, .width = 86.0f, .height = 48.0f};
constexpr Engine::Rect MORE{.left = 1711.0f, .top = 369.0f, .width = 48.0f, .height = 48.0f};
constexpr Engine::Rect MOST{.left = 1769.0f, .top = 369.0f, .width = 58.0f, .height = 48.0f};
constexpr Engine::Rect BUY{.left = 1413.0f, .top = 436.0f, .width = 200.0f, .height = 64.0f};
constexpr Engine::Rect SELL{.left = 1627.0f, .top = 436.0f, .width = 200.0f, .height = 64.0f};
constexpr Engine::Rect BAR{.left = 1413.0f, .top = 603.0f, .width = 414.0f, .height = 16.0f};
constexpr float SEGMENT_PITCH = 12.0f;
constexpr float SEGMENT_WIDTH = 9.0f;
constexpr Engine::Rect STRIP{.left = 1380.0f, .top = 667.0f, .width = 479.0f, .height = 64.0f};
constexpr Engine::Point FOOTER{.x = 1380.0f, .y = 947.0f};

// The commodities, in the market's order, and their units.
enum class Unit : std::uint8_t
{
  Tonnes,
  Kilograms,
  Grams,
};

struct Commodity
{
  const wchar_t* name;
  Unit unit;
};

constexpr std::array<Commodity, Elite::COMMODITY_KINDS> COMMODITIES = {{
  {L"Food", Unit::Tonnes},
  {L"Textiles", Unit::Tonnes},
  {L"Radioactives", Unit::Tonnes},
  {L"Slaves", Unit::Tonnes},
  {L"Liquor/Wines", Unit::Tonnes},
  {L"Luxuries", Unit::Tonnes},
  {L"Narcotics", Unit::Tonnes},
  {L"Computers", Unit::Tonnes},
  {L"Machinery", Unit::Tonnes},
  {L"Alloys", Unit::Tonnes},
  {L"Firearms", Unit::Tonnes},
  {L"Furs", Unit::Tonnes},
  {L"Minerals", Unit::Tonnes},
  {L"Gold", Unit::Kilograms},
  {L"Platinum", Unit::Kilograms},
  {L"Gem-Stones", Unit::Grams},
  {L"Alien Items", Unit::Tonnes},
}};

[[nodiscard]] const wchar_t* UnitText(Unit _unit) noexcept
{
  switch (_unit)
  {
  case Unit::Kilograms:
    return L"kg";
  case Unit::Grams:
    return L"g";
  case Unit::Tonnes:
    break;
  }
  return L"t";
}

[[nodiscard]] std::wstring Upper(std::wstring _text)
{
  std::ranges::transform(_text, _text.begin(), [](wchar_t _letter) { return static_cast<wchar_t>(std::towupper(_letter)); });
  return _text;
}

// _amount of commodity _commodity with its unit, "11 t", or a dash for none.
[[nodiscard]] std::wstring Amount(std::uint8_t _amount, std::uint8_t _commodity)
{
  if (_amount == 0)
  {
    return L"\u2014";
  }
  return std::format(L"{} {}", _amount, UnitText(COMMODITIES[_commodity].unit));
}

// The same in a strip's capitals: "11 T".
[[nodiscard]] std::wstring Capitals(std::uint8_t _amount, std::uint8_t _commodity)
{
  return std::format(L"{} {}", _amount, Upper(UnitText(COMMODITIES[_commodity].unit)));
}

[[nodiscard]] Engine::Rect RowRect(std::size_t _row) noexcept
{
  return Engine::Rect{.left = ROWS_LEFT, .top = ROWS_TOP + ROW_PITCH * static_cast<float>(_row), .width = ROWS_WIDTH, .height = ROW_PITCH};
}

void Edges(const Engine::Rect& _rect, std::uint32_t _color, Engine::Canvas& _canvas)
{
  _canvas.BoxEdge(
    Engine::Rect{.left = _rect.left + 0.5f, .top = _rect.top + 0.5f, .width = _rect.width - 1.0f, .height = _rect.height - 1.0f}, 1.0f,
    _color);
}

[[nodiscard]] Engine::Point Middle(const Engine::Rect& _rect) noexcept
{
  return Engine::Point{.x = _rect.left + _rect.width / 2.0f, .y = _rect.top + _rect.height / 2.0f};
}

// A control: its box, lit (the amber edge and fill), hovered, plain or off, and its text in the middle.
enum class Look : std::uint8_t
{
  Lit,
  Plain,
  Off,
};

void Control(const Engine::Rect& _rect, Look _look, const wchar_t* _text, Engine::Font _font, float _size, float _spacing,
             Engine::Canvas& _canvas)
{
  if (_look == Look::Lit)
  {
    _canvas.Box(_rect, CONTROL_LIT_FILL);
  }
  Edges(_rect, _look == Look::Lit ? AMBER : (_look == Look::Off ? DISABLED_EDGE : CONTROL_EDGE), _canvas);
  Engine::Point middle = Middle(_rect);
  middle.x += _spacing / 2.0f; // the spacing after the last letter, which centring would count
  _canvas.Text(_text, _font, _size, _spacing, middle, Engine::Align::Center, _look == Look::Off ? DISABLED_TEXT : AMBER_LIGHT);
}

void Header(const wchar_t* _text, float _x, Engine::Align _align, Engine::Canvas& _canvas)
{
  _canvas.Text(_text, Engine::Font::Label, HEADER_SIZE, HEADER_SPACING, Engine::Point{.x = _x, .y = HEADER_Y}, _align, AMBER);
}

void Table(const Elite::MarketView& _view, std::optional<Engine::Point> _pointer, Engine::Canvas& _canvas)
{
  DockedFrame::Panel(TABLE, true, true, _canvas);
  Header(L"PRODUCT", NAME_X, Engine::Align::Left, _canvas);
  Header(L"UNIT", UNIT_X, Engine::Align::Left, _canvas);
  // A right-aligned header's spacing after its last letter is outside the column: its anchor is moved out by it.
  Header(L"BUY", BUY_RIGHT + HEADER_SPACING, Engine::Align::Right, _canvas);
  Header(L"SELL", SELL_RIGHT + HEADER_SPACING, Engine::Align::Right, _canvas);
  Header(L"STOCK", STOCK_RIGHT + HEADER_SPACING, Engine::Align::Right, _canvas);
  Header(L"IN HOLD", HOLD_RIGHT + HEADER_SPACING, Engine::Align::Right, _canvas);
  _canvas.Line(Engine::Point{.x = TABLE.left, .y = HEADER_RULE_Y}, Engine::Point{.x = TABLE.left + TABLE.width, .y = HEADER_RULE_Y}, 1.0f,
               RULE);

  for (std::uint8_t row = 0; row < Elite::COMMODITY_KINDS; ++row)
  {
    const Engine::Rect rect = RowRect(row);
    const bool selected = row == _view.selected;
    const bool hover = !selected && _pointer && DockedFrame::Contains(rect, *_pointer);
    if (selected || hover || row % 2 == 1)
    {
      _canvas.Box(rect, selected ? ROW_SELECTED : (hover ? ROW_HOVER : ROW_STRIPE));
    }
    const float y = rect.top + ROW_TEXT;
    const std::uint32_t text = selected ? AMBER_LIGHT : TEXT;
    _canvas.Text(COMMODITIES[row].name, Engine::Font::Label, ROW_SIZE, 0.0f, Engine::Point{.x = NAME_X, .y = y}, Engine::Align::Left, text);
    _canvas.Text(UnitText(COMMODITIES[row].unit), Engine::Font::Label, ROW_SIZE, 0.0f, Engine::Point{.x = UNIT_X, .y = y},
                 Engine::Align::Left, selected ? UNIT_SELECTED : LABEL);
    _canvas.Text(DockedFrame::Tenths(_view.buyTenths[row]), Engine::Font::Number, ROW_SIZE, 0.0f, Engine::Point{.x = BUY_RIGHT, .y = y},
                 Engine::Align::Right, text);
    _canvas.Text(DockedFrame::Tenths(_view.sellTenths[row]), Engine::Font::Number, ROW_SIZE, 0.0f, Engine::Point{.x = SELL_RIGHT, .y = y},
                 Engine::Align::Right, text);
    _canvas.Text(Amount(_view.stock[row], row), Engine::Font::Number, ROW_SIZE, 0.0f, Engine::Point{.x = STOCK_RIGHT, .y = y},
                 Engine::Align::Right, text);
    _canvas.Text(Amount(_view.held[row], row), Engine::Font::Number, ROW_SIZE, 0.0f, Engine::Point{.x = HOLD_RIGHT, .y = y},
                 Engine::Align::Right, text);
  }
}

// One of the Selected panel's rows: its label, and its value at the right.
void Detail(const wchar_t* _label, const std::wstring& _value, float _y, Engine::Canvas& _canvas)
{
  _canvas.Text(_label, Engine::Font::Label, 20.0f, 0.0f, Engine::Point{.x = INSIDE_LEFT, .y = _y}, Engine::Align::Left, LABEL);
  _canvas.Text(_value, Engine::Font::Number, 20.0f, 0.0f, Engine::Point{.x = INSIDE_RIGHT, .y = _y}, Engine::Align::Right, TEXT);
}

void SelectedPanel(const Elite::MarketView& _view, std::optional<Engine::Point> _pointer, Engine::Canvas& _canvas)
{
  const std::uint8_t row = _view.selected;
  const wchar_t* const unit = UnitText(COMMODITIES[row].unit);
  DockedFrame::Panel(SELECTED, true, false, _canvas);
  _canvas.Text(L"SELECTED", Engine::Font::Label, SMALL_LABEL, SMALL_SPACING, Engine::Point{.x = INSIDE_LEFT, .y = 145.5f},
               Engine::Align::Left, LABEL);
  _canvas.Text(COMMODITIES[row].name, Engine::Font::Label, 40.0f, 0.0f, Engine::Point{.x = INSIDE_LEFT, .y = 189.5f}, Engine::Align::Left,
               AMBER_LIGHT);
  Detail(L"Buy price", std::format(L"{} / {}", DockedFrame::Tenths(_view.buyTenths[row]), unit), 238.5f, _canvas);
  Detail(L"Sell price", std::format(L"{} / {}", DockedFrame::Tenths(_view.sellTenths[row]), unit), 270.5f, _canvas);
  Detail(L"Station stock", Amount(_view.stock[row], row), 302.5f, _canvas);
  Detail(L"In hold", Amount(_view.held[row], row), 334.5f, _canvas);

  _canvas.Text(L"QUANTITY", Engine::Font::Label, SMALL_LABEL, SMALL_SPACING, Engine::Point{.x = INSIDE_LEFT, .y = 391.0f},
               Engine::Align::Left, LABEL);
  const auto look = [&_pointer](const Engine::Rect& _rect, bool _on)
  {
    if (!_on)
    {
      return Look::Off;
    }
    return _pointer && DockedFrame::Contains(_rect, *_pointer) ? Look::Lit : Look::Plain;
  };
  Control(LESS, look(LESS, true), L"\u2212", Engine::Font::Number, 26.0f, 0.0f, _canvas);
  Edges(QUANTITY, AMBER, _canvas);
  _canvas.Text(std::format(L"{}", _view.quantity), Engine::Font::Number, 26.0f, 0.0f, Middle(QUANTITY), Engine::Align::Center, AMBER_LIGHT);
  Control(MORE, look(MORE, _view.quantity < Elite::MOST_QUANTITY), L"+", Engine::Font::Number, 26.0f, 0.0f, _canvas);
  Control(MOST, look(MOST, true), L"MAX", Engine::Font::Label, SMALL_LABEL, 2.0f, _canvas);

  // [B] BUY is lit as the screen's first action, as drawn, unless the pointer is on [S] SELL; each is off when its trade at
  // the quantity would be refused.
  const bool onSell = _pointer && DockedFrame::Contains(SELL, *_pointer);
  const Look buy = !_view.buyAllowed ? Look::Off : (onSell ? Look::Plain : Look::Lit);
  const Look sell = !_view.sellAllowed ? Look::Off : (onSell ? Look::Lit : Look::Plain);
  Control(BUY, buy, L"[B] BUY", Engine::Font::Label, 22.0f, 4.0f, _canvas);
  Control(SELL, sell, L"[S] SELL", Engine::Font::Label, 22.0f, 4.0f, _canvas);
}

void HoldPanel(const Elite::MarketView& _view, Engine::Canvas& _canvas)
{
  DockedFrame::Panel(HOLD, false, false, _canvas);
  _canvas.Text(L"CARGO HOLD", Engine::Font::Label, SMALL_LABEL, SMALL_SPACING, Engine::Point{.x = INSIDE_LEFT, .y = 580.0f},
               Engine::Align::Left, LABEL);
  _canvas.Text(std::format(L"{} / {} t", _view.usedTonnes, _view.bayTonnes), Engine::Font::Number, 16.0f, 3.0f,
               Engine::Point{.x = INSIDE_RIGHT + 3.0f, .y = 580.5f}, Engine::Align::Right, AMBER_LIGHT);
  _canvas.Box(BAR, TRACK);
  const float share = _view.bayTonnes == 0 ? 0.0f : std::min(1.0f, static_cast<float>(_view.usedTonnes) / _view.bayTonnes);
  const float lit = BAR.width * share;
  if (lit <= 0.0f)
  {
    return;
  }
  _canvas.Box(Engine::Rect{.left = BAR.left - 3.0f, .top = BAR.top - 3.0f, .width = lit + 6.0f, .height = BAR.height + 6.0f}, GLOW, 4.0f);
  for (int segment = 0; SEGMENT_PITCH * static_cast<float>(segment) < lit; ++segment)
  {
    const float left = BAR.left + SEGMENT_PITCH * static_cast<float>(segment);
    const float width = std::min(SEGMENT_WIDTH, BAR.left + lit - left);
    _canvas.Box(Engine::Rect{.left = left, .top = BAR.top, .width = width, .height = BAR.height}, AMBER);
  }
}

} // namespace

void DockedMarket::Draw(const Elite::MarketView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                        Engine::Canvas& _canvas)
{
  _canvas.Clear();
  const std::wstring system = Upper(std::wstring(_view.systemName.begin(), _view.systemName.end()));
  DockedFrame::Draw(
    DockedFrame::Desc{.location = std::format(L"DOCKED \u00B7 {} STATION \u00B7 {}", _view.dodo ? L"DODO" : L"CORIOLIS", system),
                      .title = system + L" MARKET",
                      .creditsTenths = _view.creditsTenths,
                      .lit = DockedKey::Market},
    _pointer, _measure, _canvas);
  Table(_view, _pointer, _canvas);
  SelectedPanel(_view, _pointer, _canvas);
  HoldPanel(_view, _canvas);
  if (_view.message.notice != Elite::MarketNotice::None)
  {
    const Words words = WordsOf(_view.message);
    const bool success = _view.message.notice == Elite::MarketNotice::Bought || _view.message.notice == Elite::MarketNotice::Sold;
    DockedFrame::Strip(STRIP, success ? DockedFrame::Tone::Success : DockedFrame::Tone::Refusal, words.headline, words.detail, _canvas);
  }
  _canvas.Text(L"\u2191 \u2193 or click to select \u00B7 + \u2212 quantity \u00B7 B buy \u00B7 S sell", Engine::Font::Label, 17.0f, 0.0f,
               FOOTER, Engine::Align::Left, LABEL);
}

std::optional<Elite::MarketClick> DockedMarket::ControlAt(Engine::Point _point) noexcept
{
  for (std::uint8_t row = 0; row < Elite::COMMODITY_KINDS; ++row)
  {
    if (DockedFrame::Contains(RowRect(row), _point))
    {
      return Elite::MarketClick{.control = Elite::MarketControl::Row, .row = row};
    }
  }
  constexpr std::array<std::pair<Engine::Rect, Elite::MarketControl>, 5> CONTROLS = {{
    {LESS, Elite::MarketControl::Less},
    {MORE, Elite::MarketControl::More},
    {MOST, Elite::MarketControl::Most},
    {BUY, Elite::MarketControl::Buy},
    {SELL, Elite::MarketControl::Sell},
  }};
  for (const auto& [rect, control] : CONTROLS)
  {
    if (DockedFrame::Contains(rect, _point))
    {
      return Elite::MarketClick{.control = control, .row = 0};
    }
  }
  return std::nullopt;
}

DockedMarket::Words DockedMarket::WordsOf(const Elite::MarketMessage& _message)
{
  const std::uint8_t commodity = _message.commodity;
  const std::wstring name = Upper(COMMODITIES[commodity].name);
  switch (_message.notice)
  {
  case Elite::MarketNotice::None:
    break;
  case Elite::MarketNotice::Bought:
    return Words{std::format(L"BOUGHT {} {}", Capitals(_message.amount, commodity), name),
                 std::format(L"{} CR", DockedFrame::Tenths(_message.tenths))};
  case Elite::MarketNotice::Sold:
    return Words{std::format(L"SOLD {} {}", Capitals(_message.amount, commodity), name),
                 std::format(L"{} CR", DockedFrame::Tenths(_message.tenths))};
  case Elite::MarketNotice::NothingInStock:
    return Words{std::format(L"NO {} IN STOCK", name), {}};
  case Elite::MarketNotice::NotForSale:
    return Words{L"ALIEN ITEMS", L"NOT FOR SALE AT THIS STATION"};
  case Elite::MarketNotice::HoldFull:
    if (COMMODITIES[commodity].unit == Unit::Tonnes)
    {
      return Words{L"CARGO HOLD FULL",
                   std::format(L"{} FREE OF {}", Capitals(_message.freeUnits, commodity), Capitals(_message.capacityUnits, commodity))};
    }
    return Words{std::format(L"NO ROOM FOR MORE {}", name),
                 std::format(L"{} FREE OF {}", Capitals(_message.freeUnits, commodity), Capitals(_message.capacityUnits, commodity))};
  case Elite::MarketNotice::NothingHeld:
    return Words{std::format(L"NO {} IN THE HOLD", name), {}};
  case Elite::MarketNotice::NotEnoughInStock:
    return Words{std::format(L"ONLY {} IN STOCK", Capitals(_message.stock, commodity)),
                 std::format(L"YOU ASKED FOR {}", Capitals(_message.amount, commodity))};
  case Elite::MarketNotice::NotEnoughHeld:
    return Words{std::format(L"ONLY {} {} IN THE HOLD", Capitals(_message.held, commodity), name), {}};
  case Elite::MarketNotice::NotEnoughCredits:
    return Words{L"NOT ENOUGH CREDITS", std::format(L"{} CR NEEDED, {} CR AVAILABLE", DockedFrame::Tenths(_message.tenths),
                                                    DockedFrame::Tenths(_message.creditsTenths))};
  }
  return Words{};
}

} // namespace Outpost
