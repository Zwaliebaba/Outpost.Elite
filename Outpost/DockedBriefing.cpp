#include "pch.h"

#include "DockedBriefing.h"

#include "DockedStatus.h"

#include <format>
#include <string_view>
#include <vector>

namespace Outpost
{

namespace
{

// D09's palette, read off `docked/09-mission-briefing.png`.
constexpr std::uint32_t AMBER = 0xFFFFB43C;
constexpr std::uint32_t AMBER_LIGHT = 0xFFFFCF80;
constexpr std::uint32_t TEXT = 0xFFF3E6CF;
constexpr std::uint32_t LABEL = 0xFFC9BCA6;
constexpr std::uint32_t RULE = 0xFF64491D;
constexpr std::uint32_t ALERT = 0xFFFF5A2C;
constexpr std::uint32_t ALERT_LIGHT = 0xFFFFB09A;
constexpr std::uint32_t BOX_EDGE = 0xFF5B421B;

// The modal over D08, and what is in it, from its left at 409 to its right at 1511.
constexpr Engine::Rect MODAL{.left = 360.0f, .top = 130.0f, .width = 1200.0f, .height = 800.0f};
constexpr float LEFT = 409.0f;
constexpr float RIGHT = 1511.0f;
constexpr Engine::Rect TAB{.left = 409.0f, .top = 167.0f, .width = 177.0f, .height = 44.0f};
constexpr float HEADER_Y = 243.0f;
constexpr Engine::Point DOT{.x = 415.5f, .y = 243.5f};
constexpr float DOT_RADIUS = 6.5f;
constexpr float HEADER_TEXT_LEFT = 438.0f;
constexpr float HEADER_RULE_Y = 273.5f;
constexpr float SENDER_Y = 305.0f;
constexpr float TITLE_Y = 349.0f;
constexpr float TITLE_SIZE = 52.0f;
constexpr float BODY_TOP = 420.0f;
constexpr float BODY_SIZE = 26.0f;
constexpr float BODY_LINE = 39.0f;
constexpr float BODY_PARAGRAPH = 18.0f; // more between two paragraphs
constexpr float BODY_WIDTH = 1100.0f;
constexpr Engine::Rect REWARD{.left = 409.0f, .top = 730.0f, .width = 357.0f, .height = 82.0f};
constexpr float BOX_INSET = 19.0f;
constexpr float BOX_LABEL_DOWN = 22.5f;
constexpr float BOX_VALUE_DOWN = 52.5f;
constexpr Engine::Rect ACKNOWLEDGE{.left = 1215.0f, .top = 836.0f, .width = 296.0f, .height = 58.0f};
constexpr Engine::Rect YES{.left = 1155.0f, .top = 836.0f, .width = 356.0f, .height = 58.0f};
constexpr Engine::Rect NO{.left = 949.0f, .top = 836.0f, .width = 190.0f, .height = 58.0f};
constexpr float BUTTON_SIZE = 19.0f;
constexpr float BUTTON_SPACING = 4.0f;

// The keys the buttons send, as scan codes.
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_SPACE = 0x39;

[[nodiscard]] std::wstring Wide(std::string_view _text)
{
  return std::wstring(_text.begin(), _text.end());
}

[[nodiscard]] std::wstring Upper(std::wstring _text)
{
  for (wchar_t& letter : _text)
  {
    if (letter >= L'a' && letter <= L'z')
    {
      letter = static_cast<wchar_t>(letter - L'a' + L'A');
    }
  }
  return _text;
}

[[nodiscard]] bool Debriefing(const Elite::BriefingView& _view) noexcept
{
  return _view.kind == Elite::BriefingKind::Debriefing;
}

// _text in lines no wider than _width at _size, broken at its spaces.
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
    const float width = _measure(Engine::CanvasText{.text = longer,
                                                    .font = Engine::Font::Label,
                                                    .size = _size,
                                                    .spacing = 0.0f,
                                                    .anchor = Engine::Point{},
                                                    .align = Engine::Align::Left,
                                                    .color = TEXT});
    if (!line.empty() && width > _width)
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

void Button(const Engine::Rect& _rect, bool _lit, std::optional<Engine::Point> _pointer, std::wstring_view _text, Engine::Canvas& _canvas)
{
  const bool hovered = _pointer && DockedFrame::Contains(_rect, *_pointer);
  DockedFrame::Button(_rect, _lit || hovered ? DockedFrame::ButtonLook::Lit : DockedFrame::ButtonLook::Plain, _text, Engine::Font::Label,
                      BUTTON_SIZE, BUTTON_SPACING, _canvas);
}

} // namespace

void DockedBriefing::Draw(const Elite::BriefingView& _view, std::optional<Engine::Point> _pointer, const TextMeasure& _measure,
                          Engine::Canvas& _canvas)
{
  DockedStatus::Draw(_view.status, std::nullopt, _measure, _canvas);
  DockedFrame::Modal(MODAL, _canvas);

  // The transmission's own tab: only the one delivered now (ADR-068).
  DockedFrame::Button(TAB, DockedFrame::ButtonLook::Lit, std::format(L"M{} {}", _view.mission, Debriefing(_view) ? L"DEBRIEF" : L"BRIEF"),
                      Engine::Font::Label, 13.0f, 2.5f, _canvas);
  _canvas.Ellipse(DOT, DOT_RADIUS, DOT_RADIUS, ALERT);
  _canvas.Text(Debriefing(_view) ? L"TASK COMPLETE" : L"INCOMING TRANSMISSION", Engine::Font::Label, 17.0f, 4.0f,
               Engine::Point{.x = HEADER_TEXT_LEFT, .y = HEADER_Y}, Engine::Align::Left, ALERT_LIGHT);
  _canvas.Text(std::format(L"MISSION {} \u00B7 {}", _view.mission, Debriefing(_view) ? L"DEBRIEFING" : L"BRIEFING"), Engine::Font::Label,
               14.0f, 3.0f, Engine::Point{.x = RIGHT + 3.0f, .y = HEADER_Y}, Engine::Align::Right, TEXT);
  _canvas.Line(Engine::Point{.x = LEFT, .y = HEADER_RULE_Y}, Engine::Point{.x = RIGHT, .y = HEADER_RULE_Y}, 1.0f, RULE);

  _canvas.Text(L"FROM \u00B7 " + SenderOf(_view), Engine::Font::Label, 14.0f, 3.5f, Engine::Point{.x = LEFT, .y = SENDER_Y},
               Engine::Align::Left, LABEL);
  _canvas.Text(TitleOf(_view), Engine::Font::Label, TITLE_SIZE, 3.0f, Engine::Point{.x = LEFT, .y = TITLE_Y}, Engine::Align::Left,
               AMBER_LIGHT);
  float y = BODY_TOP;
  for (const std::string& paragraph : _view.paragraphs)
  {
    for (const std::wstring& line : Wrapped(Wide(paragraph), BODY_WIDTH, BODY_SIZE, _measure))
    {
      _canvas.Text(line, Engine::Font::Label, BODY_SIZE, 0.0f, Engine::Point{.x = LEFT, .y = y}, Engine::Align::Left, TEXT);
      y += BODY_LINE;
    }
    y += BODY_PARAGRAPH;
  }

  const std::wstring reward = RewardOf(_view);
  if (!reward.empty())
  {
    DockedFrame::Edge(REWARD, BOX_EDGE, _canvas);
    _canvas.Text(L"REWARD", Engine::Font::Label, 13.0f, 3.0f, Engine::Point{.x = REWARD.left + BOX_INSET, .y = REWARD.top + BOX_LABEL_DOWN},
                 Engine::Align::Left, LABEL);
    _canvas.Text(reward, Engine::Font::Label, 25.0f, 0.0f, Engine::Point{.x = REWARD.left + BOX_INSET, .y = REWARD.top + BOX_VALUE_DOWN},
                 Engine::Align::Left, AMBER_LIGHT);
  }

  if (_view.answer == Elite::BriefingAnswer::YesOrNo)
  {
    Button(NO, false, _pointer, L"N \u00B7 DECLINE", _canvas);
    Button(YES, true, _pointer, L"Y \u00B7 TAKE THE REFUGEES", _canvas);
    return;
  }
  Button(ACKNOWLEDGE, true, _pointer, L"SPACE \u00B7 ACKNOWLEDGE", _canvas);
}

std::optional<std::uint8_t> DockedBriefing::ScanCodeAt(const Elite::BriefingView& _view, Engine::Point _point) noexcept
{
  if (_view.answer == Elite::BriefingAnswer::YesOrNo)
  {
    if (DockedFrame::Contains(YES, _point))
    {
      return SCAN_Y;
    }
    if (DockedFrame::Contains(NO, _point))
    {
      return SCAN_N;
    }
    return std::nullopt;
  }
  if (DockedFrame::Contains(ACKNOWLEDGE, _point))
  {
    return SCAN_SPACE;
  }
  return std::nullopt;
}

std::wstring DockedBriefing::TitleOf(const Elite::BriefingView& _view)
{
  switch (_view.mission)
  {
  case 1:
    return L"Supernova";
  case 2:
    return L"The masking device";
  default:
    return L"The Thargoid invasion";
  }
}

std::wstring DockedBriefing::SenderOf(const Elite::BriefingView& _view)
{
  if (_view.mission == 1)
  {
    return L"THE PEOPLE OF " + Upper(Wide(_view.status.presentSystem));
  }
  return L"NAVY INTELLIGENCE";
}

std::wstring DockedBriefing::RewardOf(const Elite::BriefingView& _view)
{
  switch (_view.reward)
  {
  case Elite::BriefingReward::Credits:
    return DockedFrame::Tenths(_view.rewardTenths) + L" credits";
  case Elite::BriefingReward::MaskingDevice:
    return L"Masking device fitted";
  case Elite::BriefingReward::AntiEcmAndArchangel:
    return L"Anti-ECM and Archangel";
  case Elite::BriefingReward::None:
    break;
  }
  return {};
}

} // namespace Outpost
