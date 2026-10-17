#include "pch.h"

#include "ChartScreen.h"

#include "Flight.h"
#include "Galaxy.h"
#include "GameState.h"
#include "Host.h"
#include "Input.h"
#include "Text.h"
#include "Timer.h"

#include <algorithm>
#include <string>
#include <utility>

namespace Elite
{

namespace
{

// The keys the charts read, as scan codes.
constexpr std::uint8_t SCAN_ESC = 0x01;
constexpr std::uint8_t SCAN_D = 0x20;
constexpr std::uint8_t SCAN_F = 0x21;
constexpr std::uint8_t SCAN_SPACE = 0x39;
constexpr std::uint8_t SCAN_F5 = 0x3F;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_KEYPAD_5 = 0x4C;

// A stored tech level from which the station is a Dodo.
constexpr std::uint8_t DODO_TECH_LEVEL = 9;

// The chart units to a tenth of a light year: on the galactic chart a distance is four times the root of dx^2 + drow^2, and the
// short-range chart draws a galaxy unit 3.5 of its own.
constexpr float GALACTIC_UNITS_PER_TENTH = 0.25f;
constexpr float SHORT_RANGE_SCALE = 3.5f;

// The bytes Find's name is typed into: its letters and a NUL.
constexpr std::size_t FIND_INPUT_BYTES = FIND_LETTERS_MOST + 1;

// The chart's key in its place: F4 or F5 docked (D96), F5 or F6 in flight, as the original's.
[[nodiscard]] std::uint8_t OwnKey(ChartKind _kind, ScreenPlace _place) noexcept
{
  if (_place == ScreenPlace::Docked)
  {
    return DockedKeyOf(_kind == ChartKind::Galactic ? DockedScreen::Galaxy : DockedScreen::Local);
  }
  return _kind == ChartKind::Galactic ? SCAN_F5 : SCAN_F6;
}

// Find's name typed into findInput, as the original's typed line left it: its characters, then NULs.
void TypeFindInput(GameState& _state, const std::string& _typed)
{
  std::uint8_t(&input)[FIND_INPUT_BYTES] = _state.Data().galaxy.findInput;
  for (std::size_t letter = 0; letter < FIND_INPUT_BYTES; ++letter)
  {
    input[letter] = letter < _typed.size() && letter < FIND_LETTERS_MOST ? static_cast<std::uint8_t>(_typed[letter]) : std::uint8_t{0};
  }
}

// Hides the chart when its routine ends, however it ends.
class ChartShown
{
public:
  explicit ChartShown(Host& _host) noexcept
    : m_host(_host)
  {
  }

  ChartShown(const ChartShown&) = delete;
  ChartShown& operator=(const ChartShown&) = delete;

  ~ChartShown()
  {
    m_host.HideChart();
  }

private:
  Host& m_host;
};

} // namespace

ChartInput ChartKey(const KeyPress& _key, bool _finding)
{
  if (_finding)
  {
    if (_key.scanCode == SCAN_ESC)
    {
      return ChartInput{.action = ChartAction::CloseFind};
    }
    const std::optional<std::uint8_t> typed = TypedCharacter(_key);
    if (!typed)
    {
      return ChartInput{};
    }
    if (*typed == TYPED_ENTER)
    {
      return ChartInput{.action = ChartAction::Submit};
    }
    if (*typed == TYPED_BACKSPACE)
    {
      return ChartInput{.action = ChartAction::Erase};
    }
    return ChartInput{.action = ChartAction::Type, .character = *typed};
  }
  switch (_key.scanCode)
  {
  case SCAN_KEYPAD_5:
  case SCAN_SPACE:
    return ChartInput{.action = ChartAction::Recenter};
  case SCAN_D:
    return ChartInput{.action = ChartAction::Nearest};
  case SCAN_F:
    return ChartInput{.action = ChartAction::OpenFind};
  default:
    return ChartInput{};
  }
}

ChartScreen StepFind(const ChartScreen& _screen, const ChartInput& _input)
{
  ChartScreen screen = _screen;
  switch (_input.action)
  {
  case ChartAction::OpenFind:
    screen = ChartScreen{.find = FindState::Typing, .typed = {}};
    break;
  case ChartAction::Type:
    if (screen.typed.size() < FIND_LETTERS_MOST)
    {
      screen.typed.push_back(static_cast<char>(_input.character));
    }
    break;
  case ChartAction::Erase:
    if (!screen.typed.empty())
    {
      screen.typed.pop_back();
    }
    break;
  case ChartAction::CloseFind:
    screen = ChartScreen{};
    break;
  case ChartAction::None:
  case ChartAction::Recenter:
  case ChartAction::Nearest:
  case ChartAction::Submit:
    break;
  }
  return screen;
}

ChartView ChartViewOf(const GameState& _state, GameState& _scratch, const ChartScreen& _screen, ChartKind _kind, ScreenPlace _place)
{
  const CommanderState& commander = _state.Data().commander;
  ChartView view;
  view.kind = _kind;
  view.flight = _place == ScreenPlace::Flight;
  view.systemName = std::string(TextWithin(commander.currentSystem.name, 0));
  view.dodo = commander.currentSystem.techLevel >= DODO_TECH_LEVEL;
  view.witchSpace = commander.witchspaceCountdown != 0;
  view.creditsTenths = commander.creditsTenths;
  view.galaxyNumber = static_cast<std::uint8_t>(commander.galaxyNumber + 1);
  const bool galactic = _kind == ChartKind::Galactic;
  view.currentX = galactic ? commander.currentSystemX : SHORT_RANGE_CENTER.x;
  view.currentRow = galactic ? commander.currentSystemChartY : SHORT_RANGE_CENTER.row;
  view.reachTenths = FuelReachTenths(commander.fuel);
  view.ringRadius = static_cast<float>(view.reachTenths) * GALACTIC_UNITS_PER_TENTH * (galactic ? 1.0f : SHORT_RANGE_SCALE);
  view.cursorX = commander.chartCursorX;
  view.cursorRow = commander.chartCursorY;

  _scratch.Data() = _state.Data();
  view.stars = ChartStars(_scratch);

  // The target: the system latched for the jump while the countdown runs, otherwise the one the cursor selects as the chart
  // closes.
  _scratch.Data() = _state.Data();
  view.countdown = _state.Data().jump.hyperspaceCountdown;
  view.locked = view.countdown != 0;
  if (view.locked)
  {
    const std::uint8_t latched = _state.Data().hyperspace.hyperspaceTarget.index;
    _scratch.Data().commander.selectedSystem.index = latched;
    LoadSystemSeeds(_scratch, latched);
    ComputeDistanceToSystem(_scratch);
    DescribeSystem(_scratch);
  }
  else
  {
    SelectSystemAtCursor(_scratch);
  }
  ChartTarget& target = view.target;
  target.facts = SelectedSystemFacts(_scratch);
  const ChartClick place = SystemChartPlace(_scratch, target.facts.index);
  target.x = place.x;
  target.row = place.row;
  target.inRange = WithinJumpRange(target.facts.distanceTenths, commander.fuel);
  target.shortTenths = !target.inRange && target.facts.distanceTenths > view.reachTenths
                         ? static_cast<std::uint16_t>(target.facts.distanceTenths - view.reachTenths)
                         : std::uint16_t{0};

  view.find = _screen.find;
  view.typed = _screen.typed;
  return view;
}

Task<ScreenKey> ShowChartScreen(GameState& _state, Host& _host, ChartKind _kind, ScreenPlace _place)
{
  EnterChart(_state, _host, _kind);
  const ChartShown shown(_host);
  GameState scratch;
  ChartScreen screen;
  const std::uint8_t ownKey = OwnKey(_kind, _place);
  const auto publish = [&]() { _host.ShowChart(ChartViewOf(_state, scratch, screen, _kind, _place)); };
  publish();
  for (;;)
  {
    co_await WaitForTimerTick(_state, _host);
    bool changed = false;
    while (const std::optional<ChartClick> click = _host.TakeChartClick())
    {
      PlaceChartCursor(_state, *click);
      if (screen.find == FindState::NotOnChart)
      {
        screen = ChartScreen{};
      }
      changed = true;
    }
    const KeyPress key = GetKey(_state);
    if (key.scanCode != 0)
    {
      const ChartInput input = ChartKey(key, screen.find == FindState::Typing);
      switch (input.action)
      {
      case ChartAction::None:
        if (screen.find != FindState::Typing && key.scanCode != ownKey && ClosesScreen(key.scanCode, _place))
        {
          LeaveChart(_state);
          co_return ScreenKey{key.scanCode};
        }
        break;
      case ChartAction::Recenter:
        RecenterChartCursor(_state);
        screen = ChartScreen{};
        changed = true;
        break;
      case ChartAction::Nearest:
        NoteNearestSystem(_state);
        screen = ChartScreen{};
        changed = true;
        break;
      case ChartAction::Submit:
      {
        TypeFindInput(_state, screen.typed);
        const FindResult outcome = FindTypedSystem(_state);
        screen = outcome == FindResult::NotOnChart ? ChartScreen{.find = FindState::NotOnChart, .typed = screen.typed} : ChartScreen{};
        changed = true;
        break;
      }
      case ChartAction::OpenFind:
      case ChartAction::Type:
      case ChartAction::Erase:
      case ChartAction::CloseFind:
        screen = StepFind(screen, input);
        changed = true;
        break;
      }
    }
    if (changed)
    {
      publish();
    }
  }
}

} // namespace Elite
