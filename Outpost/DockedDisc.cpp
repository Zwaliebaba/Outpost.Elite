#include "pch.h"

#include "DockedDisc.h"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>

namespace Outpost
{

namespace
{

// D10's palette, read off `docked/10-disc-menu.png`.
constexpr std::uint32_t AMBER = 0xFFFFB43C;
constexpr std::uint32_t AMBER_LIGHT = 0xFFFFCF80;
constexpr std::uint32_t TEXT = 0xFFF3E6CF;
constexpr std::uint32_t LABEL = 0xFFC9BCA6;
constexpr std::uint32_t RULE = 0xFF64491D;
constexpr std::uint32_t ITEM_EDGE = 0xFF433216;
constexpr std::uint32_t ITEM_HOVER_FILL = 0xFF2A2114;
constexpr std::uint32_t NUMBER = 0xFFA17F4D;
constexpr std::uint32_t TABLE_EDGE = 0xFF5B421B;

// The modal, and its menu's column on the left.
constexpr Engine::Rect MODAL{.left = 360.0f, .top = 150.0f, .width = 1200.0f, .height = 740.0f};
constexpr float DIVIDER_X = 800.5f;
constexpr float MENU_LEFT = 393.0f;
constexpr float MENU_RIGHT = 768.0f;
constexpr float MENU_TITLE_Y = 194.0f;
constexpr float MENU_RULE_Y = 223.5f;
constexpr float ITEM_TOP = 242.0f;
constexpr float ITEM_PITCH = 72.0f;
constexpr float ITEM_HEIGHT = 62.0f;
constexpr float NUMBER_LEFT = 412.0f;
constexpr float NUMBER_BOX = 34.0f;
constexpr float NUMBER_DOWN = 14.0f; // the number's box, below its item's top
constexpr float ITEM_TEXT_LEFT = 464.0f;
constexpr float ITEM_SIZE = 22.0f;
constexpr float FOOTER_Y = 842.5f;

// The panel on the right: its title, its list, the line under it, the strip and the two buttons.
constexpr float PANEL_LEFT = 841.0f;
constexpr float PANEL_RIGHT = 1519.0f;
constexpr float PANEL_TITLE_Y = 205.0f;
constexpr float PANEL_TITLE_SIZE = 32.0f;
constexpr float TABLE_TOP = 243.0f;
constexpr float HEADER_HEIGHT = 40.0f;
constexpr float HEADER_SIZE = 13.0f;
constexpr float HEADER_SPACING = 3.0f;
constexpr float ROW_HEIGHT = 48.0f;
constexpr float ROW_SIZE = 21.0f;
constexpr std::size_t ROWS_SHOWN_MOST = 8;
constexpr float NAME_X = 860.0f;
constexpr float SAVED_X = 1241.0f;
constexpr float CREDITS_RIGHT = 1500.0f;
constexpr float LINE_BELOW_TABLE = 36.5f; // the count's middle, below the table
constexpr float LINE_SIZE = 20.0f;
constexpr Engine::Rect STRIP{.left = 841.0f, .top = 707.0f, .width = 678.0f, .height = 64.0f};
constexpr Engine::Rect BACK{.left = 1077.0f, .top = 797.0f, .width = 151.0f, .height = 57.0f};
constexpr Engine::Rect PRIMARY{.left = 1242.0f, .top = 797.0f, .width = 277.0f, .height = 57.0f};
constexpr float BUTTON_SIZE = 17.0f;
constexpr float BUTTON_SPACING = 3.0f;

// Save commander's name: its label, its field, and the caret after what is typed.
constexpr float NAME_LABEL_Y = 262.0f;
constexpr Engine::Rect NAME_FIELD{.left = 841.0f, .top = 283.0f, .width = 678.0f, .height = 72.0f};
constexpr float NAME_SIZE = 34.0f;
constexpr float NAME_SPACING = 3.0f;
constexpr float CARET_GAP = 4.0f;
constexpr float CARET_HEIGHT = 36.0f;
constexpr float NAME_HINT_Y = 386.5f;

constexpr std::array<const wchar_t*, 6> ITEMS = {
  L"Load commander", L"Save commander", L"Delete file", L"Catalogue disc", L"Quit game", L"Input device",
};
constexpr std::array<const wchar_t*, Elite::DISC_DEVICE_COUNT> DEVICES = {L"Keyboard", L"Mouse"};

[[nodiscard]] std::wstring Wide(std::string_view _text)
{
  return std::wstring(_text.begin(), _text.end());
}

[[nodiscard]] std::size_t IndexOf(Elite::DiscItem _item) noexcept
{
  return static_cast<std::size_t>(_item);
}

// Whether _view's panel lists the catalogue's files.
[[nodiscard]] bool ListsFiles(const Elite::DiscView& _view) noexcept
{
  return _view.item == Elite::DiscItem::Load || _view.item == Elite::DiscItem::Catalogue || _view.item == Elite::DiscItem::Delete;
}

// Whether _view asks a Y/N question.
[[nodiscard]] bool AsksYesOrNo(const Elite::DiscView& _view) noexcept
{
  return _view.prompt != Elite::DiscPrompt::None && _view.prompt != Elite::DiscPrompt::Name;
}

// The rows of _view's list: its files, its devices, or none.
[[nodiscard]] std::size_t RowsOf(const Elite::DiscView& _view) noexcept
{
  if (ListsFiles(_view))
  {
    return _view.files.size();
  }
  return _view.item == Elite::DiscItem::Device ? Elite::DISC_DEVICE_COUNT : 0;
}

// The row of _view's list selected.
[[nodiscard]] std::size_t SelectedRow(const Elite::DiscView& _view) noexcept
{
  return _view.item == Elite::DiscItem::Device ? _view.device : _view.selected;
}

[[nodiscard]] std::size_t RowsShown(const Elite::DiscView& _view) noexcept
{
  return std::min(RowsOf(_view), ROWS_SHOWN_MOST);
}

[[nodiscard]] Engine::Rect ItemRect(std::size_t _index) noexcept
{
  return Engine::Rect{
    .left = MENU_LEFT, .top = ITEM_TOP + ITEM_PITCH * static_cast<float>(_index), .width = MENU_RIGHT - MENU_LEFT, .height = ITEM_HEIGHT};
}

// The list's row _shown from the top, inside the table's edge.
[[nodiscard]] Engine::Rect RowRect(std::size_t _shown) noexcept
{
  return Engine::Rect{.left = PANEL_LEFT + 1.0f,
                      .top = TABLE_TOP + HEADER_HEIGHT + ROW_HEIGHT * static_cast<float>(_shown),
                      .width = PANEL_RIGHT - PANEL_LEFT - 2.0f,
                      .height = ROW_HEIGHT};
}

// The table's height with _rows rows, one at least for the line that says it is empty.
[[nodiscard]] float TableHeight(std::size_t _rows) noexcept
{
  return HEADER_HEIGHT + ROW_HEIGHT * static_cast<float>(std::max<std::size_t>(_rows, 1)) + 1.0f;
}

[[nodiscard]] bool Hovered(const Engine::Rect& _rect, std::optional<Engine::Point> _pointer) noexcept
{
  return _pointer && DockedFrame::Contains(_rect, *_pointer);
}

void Menu(const Elite::DiscView& _view, std::optional<Engine::Point> _pointer, Engine::Canvas& _canvas)
{
  _canvas.Text(L"DISC ACCESS \u00B7 ESC", Engine::Font::Label, 17.0f, 4.0f, Engine::Point{.x = MENU_LEFT, .y = MENU_TITLE_Y},
               Engine::Align::Left, AMBER);
  _canvas.Line(Engine::Point{.x = MENU_LEFT, .y = MENU_RULE_Y}, Engine::Point{.x = MENU_RIGHT, .y = MENU_RULE_Y}, 1.0f, RULE);
  for (std::size_t index = 0; index < ITEMS.size(); ++index)
  {
    const Engine::Rect rect = ItemRect(index);
    const bool selected = index == IndexOf(_view.item);
    if (selected)
    {
      DockedFrame::Button(rect, DockedFrame::ButtonLook::Lit, L"", Engine::Font::Label, ITEM_SIZE, 0.0f, _canvas);
    }
    else
    {
      if (Hovered(rect, _pointer))
      {
        _canvas.Box(rect, ITEM_HOVER_FILL);
      }
      DockedFrame::Edge(rect, ITEM_EDGE, _canvas);
    }
    const Engine::Rect number{.left = NUMBER_LEFT, .top = rect.top + NUMBER_DOWN, .width = NUMBER_BOX, .height = NUMBER_BOX};
    DockedFrame::Edge(number, selected ? AMBER_LIGHT : NUMBER, _canvas);
    _canvas.Text(std::format(L"{}", index + 1), Engine::Font::Number, 17.0f, 0.0f,
                 Engine::Point{.x = number.left + NUMBER_BOX / 2.0f, .y = number.top + NUMBER_BOX / 2.0f}, Engine::Align::Center,
                 selected ? AMBER_LIGHT : NUMBER);
    _canvas.Text(ITEMS[index], Engine::Font::Label, ITEM_SIZE, 1.5f, Engine::Point{.x = ITEM_TEXT_LEFT, .y = rect.top + ITEM_HEIGHT / 2.0f},
                 Engine::Align::Left, selected ? AMBER_LIGHT : TEXT);
  }
  _canvas.Text(L"1\u20136 or click \u00B7 Esc returns to the game", Engine::Font::Label, 15.0f, 0.0f,
               Engine::Point{.x = MENU_LEFT, .y = FOOTER_Y}, Engine::Align::Left, LABEL);
}

// A list's table: its edge, its header with _columns, and its rows drawn by _row, the selected one lit unless a question waits.
template <typename RowFn>
float Table(const Elite::DiscView& _view, std::optional<Engine::Point> _pointer, const std::array<const wchar_t*, 3>& _columns, RowFn _row,
            const wchar_t* _empty, Engine::Canvas& _canvas)
{
  const std::size_t shown = RowsShown(_view);
  const float height = TableHeight(shown);
  DockedFrame::Edge(Engine::Rect{.left = PANEL_LEFT, .top = TABLE_TOP, .width = PANEL_RIGHT - PANEL_LEFT, .height = height}, TABLE_EDGE,
                    _canvas);
  const float header = TABLE_TOP + HEADER_HEIGHT / 2.0f;
  _canvas.Text(_columns[0], Engine::Font::Label, HEADER_SIZE, HEADER_SPACING, Engine::Point{.x = NAME_X, .y = header}, Engine::Align::Left,
               AMBER);
  _canvas.Text(_columns[1], Engine::Font::Label, HEADER_SIZE, HEADER_SPACING, Engine::Point{.x = SAVED_X, .y = header}, Engine::Align::Left,
               AMBER);
  _canvas.Text(_columns[2], Engine::Font::Label, HEADER_SIZE, HEADER_SPACING,
               Engine::Point{.x = CREDITS_RIGHT + HEADER_SPACING, .y = header}, Engine::Align::Right, AMBER);
  _canvas.Line(Engine::Point{.x = PANEL_LEFT, .y = TABLE_TOP + HEADER_HEIGHT - 0.5f},
               Engine::Point{.x = PANEL_RIGHT, .y = TABLE_TOP + HEADER_HEIGHT - 0.5f}, 1.0f, TABLE_EDGE);
  if (shown == 0)
  {
    _canvas.Text(_empty, Engine::Font::Label, ROW_SIZE, 0.0f, Engine::Point{.x = NAME_X, .y = RowRect(0).top + ROW_HEIGHT / 2.0f},
                 Engine::Align::Left, LABEL);
    return TABLE_TOP + height;
  }
  const std::size_t first = DockedDisc::FirstRowShown(_view);
  for (std::size_t at = 0; at < shown; ++at)
  {
    const std::size_t row = first + at;
    const Engine::Rect rect = RowRect(at);
    const bool selected = row == SelectedRow(_view) && !AsksYesOrNo(_view);
    const DockedFrame::RowLook look = DockedFrame::RowLookOf(row, selected, Hovered(rect, _pointer) && !AsksYesOrNo(_view), false);
    DockedFrame::Row(rect, look, _canvas);
    _row(row, rect.top + ROW_HEIGHT / 2.0f, DockedFrame::RowText(look));
  }
  return TABLE_TOP + height;
}

void Catalogue(const Elite::DiscView& _view, std::optional<Engine::Point> _pointer, Engine::Canvas& _canvas)
{
  const auto row = [&](std::size_t _row, float _y, std::uint32_t _color)
  {
    const Elite::DiscFile& file = _view.files[_row];
    _canvas.Text(Wide(file.name), Engine::Font::Label, ROW_SIZE, 0.0f, Engine::Point{.x = NAME_X, .y = _y}, Engine::Align::Left, _color);
    _canvas.Text(DockedDisc::Saved(file), Engine::Font::Number, 18.0f, 0.0f, Engine::Point{.x = SAVED_X, .y = _y}, Engine::Align::Left,
                 _color);
    _canvas.Text(file.creditsTenths ? DockedDisc::Credits(*file.creditsTenths) : std::wstring(L"\u2014"), Engine::Font::Number, ROW_SIZE,
                 0.0f, Engine::Point{.x = CREDITS_RIGHT, .y = _y}, Engine::Align::Right, _color);
  };
  const float bottom = Table(_view, _pointer, {L"COMMANDER FILES", L"SAVED", L"CREDITS"}, row, L"No commander files", _canvas);
  const std::size_t count = _view.files.size();
  const std::wstring line = count == 0   ? std::wstring(L"The disc holds no commander files")
                            : count == 1 ? std::wstring(L"1 commander file")
                                         : std::format(L"{} commander files", count);
  _canvas.Text(line, Engine::Font::Label, LINE_SIZE, 0.0f, Engine::Point{.x = PANEL_LEFT, .y = bottom + LINE_BELOW_TABLE},
               Engine::Align::Left, LABEL);
}

void Devices(const Elite::DiscView& _view, std::optional<Engine::Point> _pointer, Engine::Canvas& _canvas)
{
  const auto row = [&](std::size_t _row, float _y, std::uint32_t _color)
  {
    _canvas.Text(DEVICES[_row], Engine::Font::Label, ROW_SIZE, 0.0f, Engine::Point{.x = NAME_X, .y = _y}, Engine::Align::Left, _color);
    if (_row == _view.deviceInUse)
    {
      _canvas.Text(L"IN USE", Engine::Font::Label, HEADER_SIZE, HEADER_SPACING, Engine::Point{.x = CREDITS_RIGHT + HEADER_SPACING, .y = _y},
                   Engine::Align::Right, AMBER);
    }
  };
  const float bottom = Table(_view, _pointer, {L"INPUT DEVICE", L"", L""}, row, L"", _canvas);
  _canvas.Text(L"What steers the ship in flight", Engine::Font::Label, LINE_SIZE, 0.0f,
               Engine::Point{.x = PANEL_LEFT, .y = bottom + LINE_BELOW_TABLE}, Engine::Align::Left, LABEL);
}

void Name(const Elite::DiscView& _view, const TextMeasure& _measure, Engine::Canvas& _canvas)
{
  _canvas.Text(L"COMMANDER NAME", Engine::Font::Label, HEADER_SIZE, HEADER_SPACING, Engine::Point{.x = PANEL_LEFT, .y = NAME_LABEL_Y},
               Engine::Align::Left, AMBER);
  const bool typing = _view.prompt == Elite::DiscPrompt::Name;
  DockedFrame::Edge(NAME_FIELD, typing ? AMBER : TABLE_EDGE, _canvas);
  const std::wstring typed = Wide(_view.typed);
  const Engine::Point at{.x = NAME_X, .y = NAME_FIELD.top + NAME_FIELD.height / 2.0f};
  _canvas.Text(typed, Engine::Font::Label, NAME_SIZE, NAME_SPACING, at, Engine::Align::Left, AMBER_LIGHT);
  const float width = _measure(Engine::CanvasText{.text = typed,
                                                  .font = Engine::Font::Label,
                                                  .size = NAME_SIZE,
                                                  .spacing = NAME_SPACING,
                                                  .anchor = at,
                                                  .align = Engine::Align::Left,
                                                  .color = AMBER_LIGHT});
  const float caret = at.x + width + CARET_GAP;
  _canvas.Text(L".CDR", Engine::Font::Label, NAME_SIZE, NAME_SPACING, Engine::Point{.x = caret + CARET_GAP, .y = at.y}, Engine::Align::Left,
               NUMBER);
  if (typing)
  {
    _canvas.Line(Engine::Point{.x = caret, .y = at.y - CARET_HEIGHT / 2.0f}, Engine::Point{.x = caret, .y = at.y + CARET_HEIGHT / 2.0f},
                 2.0f, AMBER_LIGHT);
  }
  _canvas.Text(L"A letter, then letters and digits, eight at most", Engine::Font::Label, LINE_SIZE, 0.0f,
               Engine::Point{.x = PANEL_LEFT, .y = NAME_HINT_Y}, Engine::Align::Left, LABEL);
}

// The lit button's words for _view.
[[nodiscard]] const wchar_t* PrimaryText(const Elite::DiscView& _view) noexcept
{
  switch (_view.prompt)
  {
  case Elite::DiscPrompt::Overwrite:
    return L"Y \u00B7 OVERWRITE";
  case Elite::DiscPrompt::Delete:
    return L"Y \u00B7 DELETE";
  case Elite::DiscPrompt::Quit:
    return L"Y \u00B7 QUIT";
  case Elite::DiscPrompt::Retry:
    return L"Y \u00B7 RETRY";
  case Elite::DiscPrompt::Name:
    return L"ENTER \u00B7 SAVE";
  case Elite::DiscPrompt::None:
    break;
  }
  switch (_view.item)
  {
  case Elite::DiscItem::Delete:
    return L"ENTER \u00B7 DELETE SELECTED";
  case Elite::DiscItem::Save:
    return L"ENTER \u00B7 SAVE";
  case Elite::DiscItem::Quit:
    return L"ENTER \u00B7 QUIT GAME";
  case Elite::DiscItem::Device:
    return L"ENTER \u00B7 USE SELECTED";
  case Elite::DiscItem::Load:
  case Elite::DiscItem::Catalogue:
    break;
  }
  return L"ENTER \u00B7 LOAD SELECTED";
}

// Whether the lit button would do nothing: Load commander or Delete file on an empty catalogue.
[[nodiscard]] bool PrimaryOff(const Elite::DiscView& _view) noexcept
{
  return _view.prompt == Elite::DiscPrompt::None && ListsFiles(_view) && _view.files.empty();
}

// Whether _view's strip says a success.
[[nodiscard]] bool IsSuccess(Elite::DiscNote _note) noexcept
{
  switch (_note)
  {
  case Elite::DiscNote::Loaded:
  case Elite::DiscNote::Saved:
  case Elite::DiscNote::Deleted:
  case Elite::DiscNote::Catalogued:
  case Elite::DiscNote::Keyboard:
  case Elite::DiscNote::Mouse:
    return true;
  case Elite::DiscNote::None:
  case Elite::DiscNote::Failed:
  case Elite::DiscNote::NoFiles:
  case Elite::DiscNote::BadName:
  case Elite::DiscNote::NoMouse:
    break;
  }
  return false;
}

} // namespace

void DockedDisc::Draw(const Elite::DiscView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                      Engine::Canvas& _canvas)
{
  _canvas.Clear();
  std::wstring system = Wide(_view.systemName);
  std::ranges::transform(system, system.begin(), [](wchar_t _letter)
                         { return _letter >= L'a' && _letter <= L'z' ? static_cast<wchar_t>(_letter - L'a' + L'A') : _letter; });
  DockedFrame::Draw(
    DockedFrame::Desc{.location = std::format(L"DOCKED \u00B7 {} STATION \u00B7 {}", _view.dodo ? L"DODO" : L"CORIOLIS", system),
                      .title = L"DISC ACCESS",
                      .creditsTenths = _view.creditsTenths,
                      .lit = DockedKey::Disc},
    _pointer, _measure, _canvas);
  DockedFrame::Modal(MODAL, _canvas);
  _canvas.Line(Engine::Point{.x = DIVIDER_X, .y = MODAL.top}, Engine::Point{.x = DIVIDER_X, .y = MODAL.top + MODAL.height}, 1.0f, RULE);
  Menu(_view, _pointer, _canvas);

  _canvas.Text(ITEMS[IndexOf(_view.item)], Engine::Font::Label, PANEL_TITLE_SIZE, 2.0f, Engine::Point{.x = PANEL_LEFT, .y = PANEL_TITLE_Y},
               Engine::Align::Left, TEXT);
  switch (_view.item)
  {
  case Elite::DiscItem::Load:
  case Elite::DiscItem::Catalogue:
  case Elite::DiscItem::Delete:
    Catalogue(_view, _pointer, _canvas);
    break;
  case Elite::DiscItem::Save:
    Name(_view, _measure, _canvas);
    break;
  case Elite::DiscItem::Device:
    Devices(_view, _pointer, _canvas);
    break;
  case Elite::DiscItem::Quit:
    _canvas.Text(L"The game ends. What is not saved is lost.", Engine::Font::Label, LINE_SIZE, 0.0f,
                 Engine::Point{.x = PANEL_LEFT, .y = TABLE_TOP + HEADER_HEIGHT / 2.0f}, Engine::Align::Left, LABEL);
    break;
  }

  if (AsksYesOrNo(_view) || _view.note != Elite::DiscNote::None)
  {
    const Words words = WordsOf(_view);
    const bool success = !AsksYesOrNo(_view) && IsSuccess(_view.note);
    DockedFrame::Strip(STRIP, success ? DockedFrame::Tone::Success : DockedFrame::Tone::Refusal, words.headline, words.detail, _canvas);
  }
  const bool question = AsksYesOrNo(_view);
  DockedFrame::Button(BACK, Hovered(BACK, _pointer) ? DockedFrame::ButtonLook::Lit : DockedFrame::ButtonLook::Plain,
                      question ? L"N \u00B7 NO" : L"ESC \u00B7 BACK", Engine::Font::Label, BUTTON_SIZE, BUTTON_SPACING, _canvas);
  DockedFrame::Button(PRIMARY, PrimaryOff(_view) ? DockedFrame::ButtonLook::Off : DockedFrame::ButtonLook::Lit, PrimaryText(_view),
                      Engine::Font::Label, BUTTON_SIZE, BUTTON_SPACING, _canvas);
}

std::optional<Elite::DiscClick> DockedDisc::ControlAt(const Elite::DiscView& _view, Engine::Point _point) noexcept
{
  for (std::size_t index = 0; index < ITEMS.size(); ++index)
  {
    if (DockedFrame::Contains(ItemRect(index), _point))
    {
      return Elite::DiscClick{.target = Elite::DiscTarget::Item, .index = index};
    }
  }
  const std::size_t first = FirstRowShown(_view);
  for (std::size_t at = 0; at < RowsShown(_view); ++at)
  {
    if (DockedFrame::Contains(RowRect(at), _point))
    {
      return Elite::DiscClick{.target = Elite::DiscTarget::Row, .index = first + at};
    }
  }
  if (DockedFrame::Contains(BACK, _point))
  {
    return Elite::DiscClick{.target = Elite::DiscTarget::Back, .index = 0};
  }
  if (DockedFrame::Contains(PRIMARY, _point) && !PrimaryOff(_view))
  {
    return Elite::DiscClick{.target = Elite::DiscTarget::Primary, .index = 0};
  }
  return std::nullopt;
}

DockedDisc::Words DockedDisc::WordsOf(const Elite::DiscView& _view)
{
  const std::wstring subject = Wide(_view.subject);
  switch (_view.prompt)
  {
  case Elite::DiscPrompt::Overwrite:
    return Words{.headline = subject + L" IS ON THE DISC \u2014 OVERWRITE IT?", .detail = L"Y overwrite \u00B7 N change the name"};
  case Elite::DiscPrompt::Delete:
    return Words{.headline = L"DELETE " + subject + L"?", .detail = L"Y delete \u00B7 N keep it"};
  case Elite::DiscPrompt::Quit:
    return Words{.headline = L"QUIT THE GAME?", .detail = L"Y quit \u00B7 N stay docked"};
  case Elite::DiscPrompt::Retry:
    return Words{.headline = L"DISC ERROR \u2014 " + subject, .detail = L"Y try again \u00B7 N give up"};
  case Elite::DiscPrompt::None:
  case Elite::DiscPrompt::Name:
    break;
  }
  switch (_view.note)
  {
  case Elite::DiscNote::Loaded:
    return Words{.headline = L"LOADED " + subject, .detail = {}};
  case Elite::DiscNote::Saved:
    return Words{.headline = L"SAVED " + subject, .detail = {}};
  case Elite::DiscNote::Deleted:
    return Words{.headline = L"DELETED " + subject, .detail = {}};
  case Elite::DiscNote::Catalogued:
    return Words{.headline = L"DISC CATALOGUED", .detail = {}};
  case Elite::DiscNote::Failed:
    return Words{.headline = L"DISC ERROR \u2014 " + subject, .detail = L"Nothing was loaded, saved or deleted"};
  case Elite::DiscNote::NoFiles:
    return Words{.headline = L"NO COMMANDER FILES ON THE DISC", .detail = {}};
  case Elite::DiscNote::BadName:
    return Words{.headline = L"A NAME STARTS WITH A LETTER", .detail = L"Then letters and digits, eight at most"};
  case Elite::DiscNote::NoMouse:
    return Words{.headline = L"NO MOUSE ATTACHED", .detail = L"The keyboard still steers"};
  case Elite::DiscNote::Keyboard:
    return Words{.headline = L"THE KEYBOARD STEERS", .detail = {}};
  case Elite::DiscNote::Mouse:
    return Words{.headline = L"THE MOUSE STEERS", .detail = {}};
  case Elite::DiscNote::None:
    break;
  }
  return Words{};
}

std::wstring DockedDisc::Credits(std::uint32_t _tenths)
{
  std::wstring whole = std::format(L"{}", _tenths / 10);
  for (std::size_t at = whole.size(); at > 3; at -= 3)
  {
    whole.insert(at - 3, 1, L' ');
  }
  return std::format(L"{}.{}", whole, _tenths % 10);
}

std::wstring DockedDisc::Saved(const Elite::DiscFile& _file)
{
  return std::format(L"{:02}-{:02}-{:02}", unsigned{_file.day}, unsigned{_file.month}, unsigned{_file.year % 100u});
}

std::size_t DockedDisc::FirstRowShown(const Elite::DiscView& _view) noexcept
{
  const std::size_t selected = SelectedRow(_view);
  return selected < ROWS_SHOWN_MOST ? 0 : selected - ROWS_SHOWN_MOST + 1;
}

} // namespace Outpost
