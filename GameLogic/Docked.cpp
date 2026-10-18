#include "pch.h"

#include "Docked.h"

#include "Arithmetic.h"
#include "ChartScreen.h"
#include "DiscScreen.h"
#include "DockedTables.h"
#include "EquipScreen.h"
#include "Equipment.h"
#include "FlightSnapshot.h"
#include "Galaxy.h"
#include "Input.h"
#include "Market.h"
#include "MarketScreen.h"
#include "SaveLoad.h"
#include "Scene.h"
#include "SharedTables.h"
#include "Ships.h"
#include "Sound.h"
#include "StartUp.h"
#include "Text.h"
#include "Video.h"

#include <algorithm>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>

namespace Elite
{

namespace
{

constexpr std::uint8_t FUEL_TENTHS_PER_UNIT = 10;
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24; // 255 is 7.0 light years
constexpr std::uint8_t DODO_TECH_LEVEL = 9;         // a Dodo station from this tech level up, as D01 draws it

constexpr std::uint8_t TEXT_LAYOUT = 2;

// Scan codes.
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F9 = 0x43;
constexpr std::uint8_t SCAN_F10 = 0x44;

// The docked screens' rows, statuses and missions.
constexpr std::uint16_t TRADE_ROWS = 0x11;
constexpr std::uint8_t LEGAL_STATUS_OFFENDER = 1;
constexpr std::uint8_t LEGAL_STATUS_FUGITIVE = 0x28;
constexpr std::uint16_t EQUIPMENT_ROWS = 0x0D;
constexpr std::uint8_t FIRST_EQUIPMENT_MENU_ROW = 2;
constexpr std::uint16_t LASER_MOUNTS = 4;
constexpr std::uint16_t RANK_LETTERS = 9; // 'ARCHANGEL' over 'COMMANDER'
constexpr std::uint8_t LASER_MOUNT_TYPE_MASK = 3;
constexpr std::uint8_t NUL = 0;

constexpr std::uint8_t MISSION_SUPERNOVA = 1;
constexpr std::uint8_t MISSION_MASK_SHIP = 2;
constexpr std::uint8_t STAGE_BRIEFED = 1;
constexpr std::uint8_t STAGE_DEBRIEFED = 2;
constexpr std::uint8_t REFUGEE_TONNES = 0x14;
constexpr std::uint8_t REFUGEE_TONNES_LARGE_BAY = 0x23;
constexpr std::uint16_t SUPERNOVA_DELAY_FRAMES = 0x190;
constexpr std::uint8_t MASK_SHIPS = 5;
constexpr std::uint8_t MASK_SYSTEM_JUMPS = 2;
constexpr std::uint8_t REWARD_DIGIT_FULL_HOLD = 0x30; // '1000 Credits' when all 20 tonnes came
constexpr std::uint16_t REWARD_FULL_HOLD_TENTHS = 0x2710;
constexpr std::uint8_t REWARD_DIGIT_OTHERWISE = 0x34; // '1400 Credits'
constexpr std::uint16_t REWARD_OTHERWISE_TENTHS = 0x36B0;
constexpr std::uint16_t SUPERNOVA_DEBRIEFING_LINES = 4;
constexpr std::uint16_t INVASION_DEBRIEFING_LINES = 0x0E;

constexpr std::uint16_t PRESS_ANY_KEY_PLACE = 0x1C05;
constexpr std::uint16_t PRESS_ANY_KEY_MASK = 0x5555;
constexpr std::uint8_t TITLE_SHIP_SLOTS = 3;
constexpr std::uint8_t TITLE_LAYOUT = 2;
constexpr std::uint16_t TITLE_PLANET_RADIUS = 0x3C;
constexpr std::uint16_t TITLE_PLANET_X = 0xC8;
constexpr std::uint16_t TITLE_PLANET_Y = 0x32;
constexpr std::uint8_t TITLE_PLANET_COLOR = 2;
constexpr std::uint8_t TITLE_FIRST_SHIP_TYPE = 2;
constexpr std::uint8_t TITLE_LAST_SHIP_TYPE = 0x1D;
// The title ship, in stationSlot: what turns it each frame, and its flags.
constexpr std::uint16_t TITLE_PITCH_STEP = 0x19;
constexpr std::uint16_t TITLE_YAW_STEP = 0x14;
constexpr std::uint16_t TITLE_ROLL_STEP = 0x1E;
constexpr std::uint8_t TITLE_SHIP_FLAGS = 2;

// GetKey until a key comes (ADR-039). Returns the key.
[[nodiscard]] Task<KeyPress> WaitForKey(GameState& _state, Host& _host)
{
  for (;;)
  {
    const KeyPress key = GetKey(_state);
    if (key.scanCode != 0)
    {
      co_return key;
    }
    co_await AwaitNextKey(_host, key);
  }
}

// The selected system's name and its NUL, where TerminateSelectedSystemName leaves it after an eight-letter name.
[[nodiscard]] std::span<const std::uint8_t> SelectedNameRun(const GameState& _state)
{
  return _state.Data().commander.selectedSystem.name;
}

// Product _product's lot in cargoHold: the units held, and the units on sale.
[[nodiscard]] CommanderState::CargoHold::Lot& LotOf(GameState& _state, std::size_t _product)
{
  return ElementWithin(std::span(_state.Data().commander.cargoHold.lots), _product);
}

// The units of product _product held, the refugees' tonnes after the last commodity, as the inventory lists them.
[[nodiscard]] std::uint8_t HeldOf(const GameState& _state, std::size_t _product)
{
  const CommanderState::CargoHold& hold = _state.Data().commander.cargoHold;
  return _product == std::size(hold.lots) ? std::uint8_t{hold.refugeesTonnes}
                                          : std::uint8_t{ElementWithin(std::span(hold.lots), _product).held};
}

// RunTitle, the title: the ship slots cleared (ClearAllObjects), the music on, the
// player's angles, the view and the lasers zeroed, the cockpit shown (screenLayout 2, which ShowCockpitScreen copies over with the
// direction flag clear), then the planet and the turning ship of type
// titleShipType, in stationSlot at its titleShipDistances, until a key: F9 and F10 step the type through 2-29, any other ends it
// with the music off, the credits and a new game. TitleFrameLoop is one
// frame.
[[nodiscard]] Task<void> RunTitle(GameState& _state, Host& _host)
{
  ClearAllObjects(_state);
  StartMusic(_state, _host);
  _state.Data().commander.laserMountsFitted = 0;
  _state.Data().playerShip.playerPitchAngle = 0;
  _state.Data().playerShip.playerYawAngle = 0;
  _state.Data().playerShip.playerRollAngle = 0;
  _state.Data().playerShip.viewAngle = 0;
  _state.Data().drawing.screenLayout = TITLE_LAYOUT;
  (void)ShowCockpitScreen(_state, _host);
  _state.Data().drawing.textPaperPattern = 0;
  SpaceObject& ship = _state.Slot(SlotIndex::Station);
  _state.Data().world.shipSlotCount = TITLE_SHIP_SLOTS;
  // Zero into the yaw, the roll, the low words of x and y, the high bytes of x and y as a word, and z's high byte.
  ship.yaw = 0;
  ship.roll = 0;
  ship.x = 0;
  ship.y = 0;
  ship.xHigh = 0; // xHigh and yHigh, written as a word
  ship.yHigh = 0;
  ship.zHigh = 0;
  bool firstFrame = true;
  for (;;)
  {
    // The space view's layers watch the frame (ADR-033), the planet the renderer's to draw as the system's (ADR-035).
    _host.BeginSpaceViewFrame();
    _host.BeginSpaceViewObject();
    _state.Data().drawing.drawColor = TITLE_PLANET_COLOR;
    DrawTitlePlanet(_state, TITLE_PLANET_RADIUS, TITLE_PLANET_X, TITLE_PLANET_Y);
    _host.EndSpaceViewRendered();
    ship.flags = TITLE_SHIP_FLAGS;
    // titleShipType doubled, in 8 bits, indexes titleShipDistances, and one more makes it the type byte, active.
    // The ship stands at the type's own distance, turned a step on each axis: the roll, the yaw, then the pitch.
    const auto doubled = static_cast<std::uint8_t>(_state.Data().docked.titleShipType << 1);
    const std::uint16_t distance = TITLE_SHIP_DISTANCES[doubled / 2];
    ship.type = static_cast<std::uint8_t>(doubled + 1);
    ship.z = distance;
    ship.roll = Offset(ship.roll, TITLE_ROLL_STEP);
    ship.yaw = Offset(ship.yaw, TITLE_YAW_STEP);
    ship.pitch = Offset(ship.pitch, TITLE_PITCH_STEP);
    _state.Data().drawing.textPaperPattern = 0;
    DrawViewString(_state, PRESS_ANY_KEY_TEXT, PRESS_ANY_KEY_MASK, PRESS_ANY_KEY_PLACE);
    (void)TransformAndDrawObjects(_state, _host);
    co_await FinishSpaceViewFrame(_state, _host);
    // The frame as it was drawn, the title's, for a renderer that draws its ship and its planet (ADR-033, ADR-035), with the
    // dashboard the title shows: the cockpit's picture, nothing on it live (D64). DrawDisc's radius is twice the disc's rows.
    const SnapshotDisc planet{.shown = true,
                              .x = static_cast<std::int16_t>(TITLE_PLANET_X),
                              .row = static_cast<std::int16_t>(TITLE_PLANET_Y),
                              .rows = static_cast<std::uint16_t>(TITLE_PLANET_RADIUS >> 1)};
    _host.PublishFlightSnapshot(TakeTitleSnapshot(_state, ELITE_TITLE_TEXT, planet), !firstFrame);
    firstFrame = false;
    if (const KeyPress key = GetKey(_state); key.scanCode != 0)
    {
      if (key.scanCode == SCAN_F9)
      {
        _state.Data().docked.titleShipType = static_cast<std::uint8_t>(_state.Data().docked.titleShipType - 1);
        if (_state.Data().docked.titleShipType < TITLE_FIRST_SHIP_TYPE)
        {
          _state.Data().docked.titleShipType = TITLE_LAST_SHIP_TYPE;
        }
      }
      else if (key.scanCode == SCAN_F10)
      {
        _state.Data().docked.titleShipType = static_cast<std::uint8_t>(_state.Data().docked.titleShipType + 1);
        if (_state.Data().docked.titleShipType == TITLE_LAST_SHIP_TYPE + 1)
        {
          _state.Data().docked.titleShipType = TITLE_FIRST_SHIP_TYPE;
        }
      }
      else
      {
        break;
      }
    }
  }
  StopAllSound(_state, _host);
  co_await ShowCredits(_state, _host);
  StartNewGame(_state);
  _state.Data().flow.titleShown = 1;
}

// The routine that shows _screen docked (D96). The status screen resets the keyboard first; Esc's D10 is DispatchDockedKeys's.
[[nodiscard]] Task<ScreenKey> ShowDockedScreen(GameState& _state, Host& _host, DockedScreen _screen)
{
  switch (_screen)
  {
  case DockedScreen::Market:
    return ShowMarketScreen(_state, _host);
  case DockedScreen::Equip:
    return ShowEquipScreen(_state, _host);
  case DockedScreen::Galaxy:
    return ShowChartScreen(_state, _host, ChartKind::Galactic, ScreenPlace::Docked);
  case DockedScreen::Local:
    return ShowChartScreen(_state, _host, ChartKind::ShortRange, ScreenPlace::Docked);
  case DockedScreen::Status:
  case DockedScreen::Disc:
    break;
  }
  ResetKeyboard(_state);
  return ShowCommanderStatusScreen(_state, _host, ScreenPlace::Docked);
}

// The docked screen published to the host while the docked host runs, and taken down when it ends.
// D08's view published to the host while the docked status screen waits for its key, when there is one, and taken down after.
class StatusShown
{
public:
  StatusShown(Host& _host, std::optional<StatusView> _view)
    : m_host(_host),
      m_shown(_view.has_value())
  {
    if (_view)
    {
      m_host.ShowStatus(std::move(*_view));
    }
  }

  StatusShown(const StatusShown&) = delete;
  StatusShown& operator=(const StatusShown&) = delete;

  ~StatusShown()
  {
    if (m_shown)
    {
      m_host.HideStatus();
    }
  }

private:
  Host& m_host;
  bool m_shown;
};

// D09 published to the host while a briefing or a debriefing waits for its key, and taken down after.
class BriefingShown
{
public:
  explicit BriefingShown(Host& _host) noexcept
    : m_host(_host)
  {
  }

  BriefingShown(const BriefingShown&) = delete;
  BriefingShown& operator=(const BriefingShown&) = delete;

  ~BriefingShown()
  {
    m_host.HideBriefing();
  }

private:
  Host& m_host;
};

// D09 as _state stands: mission missionNumber's _kind, with _paragraphs, waiting for _answer, and _reward of _rewardTenths.
[[nodiscard]] BriefingView BriefingOf(const GameState& _state, BriefingKind _kind, std::vector<std::string> _paragraphs,
                                      BriefingAnswer _answer, BriefingReward _reward = BriefingReward::None,
                                      std::uint32_t _rewardTenths = 0)
{
  BriefingView view;
  view.status = StatusViewOf(_state);
  view.mission = _state.Data().commander.missionNumber;
  view.kind = _kind;
  view.paragraphs = std::move(_paragraphs);
  view.answer = _answer;
  view.reward = _reward;
  view.rewardTenths = _rewardTenths;
  return view;
}

// _lines texts of _texts, each after the NUL of the one before, as PrintTextLines reads them.
[[nodiscard]] std::vector<std::string_view> LinesOf(std::string_view _texts, std::uint16_t _lines)
{
  std::vector<std::string_view> lines;
  std::string_view rest = _texts;
  for (std::uint16_t line = 0; line < _lines; ++line)
  {
    const std::size_t nul = rest.find('\0');
    lines.push_back(rest.substr(0, nul));
    rest = nul == std::string_view::npos ? std::string_view{} : rest.substr(nul + 1);
  }
  return lines;
}

// _first's paragraphs, then _second's.
[[nodiscard]] std::vector<std::string> Joined(std::vector<std::string> _first, const std::vector<std::string>& _second)
{
  _first.insert(_first.end(), _second.begin(), _second.end());
  return _first;
}

class DockedScreenShown
{
public:
  explicit DockedScreenShown(Host& _host) noexcept
    : m_host(_host)
  {
  }

  DockedScreenShown(const DockedScreenShown&) = delete;
  DockedScreenShown& operator=(const DockedScreenShown&) = delete;

  ~DockedScreenShown()
  {
    m_host.HideDockedScreen();
  }

private:
  Host& m_host;
};

// The docked host (D96): from _screen, the status screen or Esc's D10, each screen returns the key that closed it, which picks the
// next screen (DockedTargetOf), until F1 leaves; any other key waits for one. Returns whether D10 left for the disk, through
// LeaveGameLoopForDisk, which returns past RunTitleAndDocked (DiscExit); otherwise F1 ended them.
[[nodiscard]] Task<bool> DispatchDockedKeys(GameState& _state, Host& _host, DockedScreen _screen)
{
  const DockedScreenShown shown(_host);
  DockedScreen screen = _screen;
  for (;;)
  {
    _host.ShowDockedScreen(screen);
    ScreenKey key{};
    if (screen == DockedScreen::Disc)
    {
      const DiscExit exit = co_await ShowDiscScreen(_state, _host);
      if (exit.leaves)
      {
        co_return true;
      }
      key = exit.key;
    }
    else
    {
      key = co_await ShowDockedScreen(_state, _host, screen);
    }
    for (;;)
    {
      const DockedTarget target = DockedTargetOf(key.scanCode);
      if (target.launches)
      {
        co_return false;
      }
      if (target.screen)
      {
        screen = *target.screen;
        break;
      }
      // Back to the GetKey.
      const KeyPress next = co_await WaitForKey(_state, _host);
      key = ScreenKey{next.scanCode};
    }
  }
}

} // namespace

void EnterDockedScreen(GameState& _state, Host& _host)
{
  if ((_state.Data().drawing.screenLayout & TEXT_LAYOUT) == 0)
  {
    _state.Data().drawing.screenLayout = TEXT_LAYOUT;
    SetTextMode(_state, _host);
  }
}

[[nodiscard]] Task<ScreenKey> WaitForScreenExitKey(GameState& _state, Host& _host, std::uint8_t _ownKey, ScreenPlace _place)
{
  // GetKey until Esc or another screen's key, every other key and an empty poll going back to the GetKey.
  for (;;)
  {
    const KeyPress key = GetKey(_state);
    const std::uint8_t scanCode = key.scanCode;
    if (scanCode != 0 && scanCode != _ownKey && ClosesScreen(scanCode, _place))
    {
      SelectSystemAtCursor(_state);
      co_return ScreenKey{scanCode};
    }
    co_await AwaitNextKey(_host, key);
  }
}

void AwardArchangelTitle(GameState& _state)
{
  for (std::uint16_t letter = 0; letter < RANK_LETTERS; ++letter)
  {
    _state.Data().commander.commanderRankText[letter] = std::uint8_t{ARCHANGEL_TITLE[letter]};
  }
}

[[nodiscard]] Task<ScreenKey> ShowCommanderStatusScreen(GameState& _state, Host& _host, ScreenPlace _place)
{
  if (_state.Data().flow.inFlight != 1 && _state.Data().commander.missionNumber != 0)
  {
    if (_state.Data().commander.missionStage == 0)
    {
      co_await ShowMissionBriefing(_state, _host);
    }
    else if (_state.Data().commander.missionStage == STAGE_BRIEFED && _state.Data().flight.playerDocked == 1)
    {
      co_await ShowMissionDebriefing(_state, _host);
    }
  }
  EnterDockedScreen(_state, _host);
  // D08 shows over the screen while it waits (ADR-063), docked and in flight (ADR-069).
  StatusView view = StatusViewOf(_state);
  view.flight = _place == ScreenPlace::Flight;
  const StatusShown shown(_host, std::move(view));
  co_return co_await WaitForScreenExitKey(_state, _host, _place == ScreenPlace::Docked ? SCAN_F6 : SCAN_F9, _place);
}

namespace
{

// _text without the spaces the original pads its names with: "JAMESON " is JAMESON.
[[nodiscard]] std::string WithoutTrailingSpaces(std::string_view _text)
{
  const std::size_t end = _text.find_last_not_of(' ');
  return std::string(_text.substr(0, end == std::string_view::npos ? 0 : end + 1));
}

} // namespace

LegalStanding LegalStandingOf(std::uint8_t _legalStatus) noexcept
{
  if (_legalStatus < LEGAL_STATUS_OFFENDER)
  {
    return LegalStanding::Clean;
  }
  return _legalStatus < LEGAL_STATUS_FUGITIVE ? LegalStanding::Offender : LegalStanding::Fugitive;
}

std::uint8_t RatingOf(std::uint8_t _kills) noexcept
{
  // The last threshold is above any kill count (killCount stops at FEh), so the rating is one of the nine.
  std::uint8_t rating = 0;
  while (_kills >= RATING_THRESHOLDS[rating])
  {
    ++rating;
  }
  return rating;
}

StatusView StatusViewOf(const GameState& _state)
{
  const CommanderState& commander = _state.Data().commander;
  StatusView view;
  view.systemName = std::string(TextWithin(commander.currentSystem.name, 0));
  view.dodo = commander.currentSystem.techLevel >= DODO_TECH_LEVEL;
  view.title = WithoutTrailingSpaces(TextIn(commander.commanderRankText));
  view.commanderName = WithoutTrailingSpaces(TextWithin(commander.defaultCommanderName, 0));
  view.witchSpace = commander.witchspaceCountdown != 0;
  view.presentSystem = view.systemName;
  view.hyperspaceTarget = std::string(TextWithin(SelectedNameRun(_state), 0));
  view.fuelTenths = static_cast<std::uint8_t>(commander.fuel * FUEL_TENTHS_PER_UNIT / FUEL_UNITS_PER_TENTH);
  view.creditsTenths = commander.creditsTenths;
  view.legal = LegalStandingOf(commander.legalStatus);
  view.rating = RatingOf(commander.killCount);
  // Each item fitted, in the list's order from the missiles; a laser with the mounts whose two bits of laserMountTypes are its type.
  const std::span<std::uint8_t CommanderState::*const> fitted = std::span(EQUIPMENT_ITEMS).subspan(1, EQUIPMENT_ROWS);
  for (std::size_t item = 0; item < fitted.size(); ++item)
  {
    const std::uint8_t count = commander.*fitted[item];
    if (count == 0)
    {
      continue;
    }
    FittedItem entry{.kind = static_cast<FittedKind>(item), .count = count, .mounts = 0};
    if (const std::optional<std::uint8_t> type = LaserTypeOfRow(static_cast<std::uint8_t>(FIRST_EQUIPMENT_MENU_ROW + item));
        type.has_value())
    {
      const std::uint8_t laser = type.value_or(0);
      for (std::uint8_t mount = 0; mount < LASER_MOUNTS; ++mount)
      {
        const bool on = ((commander.laserMountsFitted >> mount) & 1) != 0;
        if (on && ((commander.laserMountTypes >> (2 * mount)) & LASER_MOUNT_TYPE_MASK) == laser)
        {
          entry.mounts = static_cast<std::uint8_t>(entry.mounts | (1 << mount));
        }
      }
    }
    view.equipment.push_back(entry);
  }
  for (std::size_t commodity = 0; commodity < COMMODITY_KINDS; ++commodity)
  {
    view.held[commodity] = HeldOf(_state, commodity);
  }
  view.refugeesTonnes = commander.cargoHold.refugeesTonnes;
  view.usedTonnes = commander.cargoHold.cargoUsedTonnes;
  view.bayTonnes = CargoBayTonnes(commander.largeCargoBayFitted == 1);
  return view;
}

std::vector<std::string> BriefingParagraphs(std::span<const std::string_view> _lines)
{
  constexpr std::string_view PRESS_ANY_KEY = "Press any key to continue!";
  std::vector<std::string> paragraphs;
  std::string paragraph;
  const auto finish = [&]()
  {
    if (!paragraph.empty())
    {
      paragraphs.push_back(std::move(paragraph));
      paragraph.clear();
    }
  };
  for (const std::string_view line : _lines)
  {
    const std::size_t first = line.find_first_not_of(' ');
    if (first == std::string_view::npos || line == PRESS_ANY_KEY)
    {
      finish();
      continue;
    }
    const std::string_view text = line.substr(first, line.find_last_not_of(' ') - first + 1);
    if (!paragraph.empty() && paragraph.back() != '-')
    {
      paragraph.push_back(' ');
    }
    paragraph.append(text);
  }
  finish();
  return paragraphs;
}

[[nodiscard]] Task<void> ShowMissionBriefing(GameState& _state, Host& _host)
{
  _state.Data().commander.missionStage = STAGE_BRIEFED;
  EnterDockedScreen(_state, _host);
  const BriefingShown shown(_host);
  const std::uint8_t mission = _state.Data().commander.missionNumber;
  if (mission == MISSION_SUPERNOVA)
  {
    const std::vector<std::string> question = BriefingParagraphs(SUPERNOVA_BRIEFING_TEXT);
    _host.ShowBriefing(BriefingOf(_state, BriefingKind::Briefing, question, BriefingAnswer::YesOrNo));
    // Y takes the refugees, N refuses; every other key goes back to the GetKey, as an empty one does.
    std::uint8_t answer = 0;
    for (;;)
    {
      answer = (co_await WaitForKey(_state, _host)).scanCode;
      if (answer == SCAN_Y || answer == SCAN_N)
      {
        break;
      }
    }
    std::span<const std::string_view> reply = SUPERNOVA_REFUSED_LINES;
    if (answer == SCAN_Y)
    {
      // The hold emptied for the refugees: 0 at each of the 17 amounts.
      for (std::uint16_t product = 0, left = TRADE_ROWS;;)
      {
        LotOf(_state, product).held = 0;
        ++product;
        if (--left == 0)
        {
          break;
        }
      }
      _state.Data().commander.cargoHold.cargoUsedTonnes = REFUGEE_TONNES;
      if (_state.Data().commander.largeCargoBayFitted == 1)
      {
        _state.Data().commander.cargoHold.cargoUsedTonnes = REFUGEE_TONNES_LARGE_BAY;
      }
      _state.Data().commander.cargoHold.refugeesTonnes = std::uint8_t{_state.Data().commander.cargoHold.cargoUsedTonnes};
      reply = SUPERNOVA_ACCEPTED_LINES;
    }
    _host.ShowBriefing(
      BriefingOf(_state, BriefingKind::Briefing, Joined(question, BriefingParagraphs(reply)), BriefingAnswer::Acknowledge));
    co_await WaitForKeyPress(_state, _host);
    _state.Data().commander.supernovaFrames = SUPERNOVA_DELAY_FRAMES;
    _state.Data().commander.jumpedSinceBriefing = 0;
    co_return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    _host.ShowBriefing(BriefingOf(_state, BriefingKind::Briefing, BriefingParagraphs(MASK_BRIEFING_TEXT), BriefingAnswer::Acknowledge));
    co_await WaitForKeyPress(_state, _host);
    _state.Data().commander.maskMissionShipsLeft = MASK_SHIPS;
    _state.Data().commander.maskSystemJumps = MASK_SYSTEM_JUMPS;
    co_return;
  }
  _host.ShowBriefing(BriefingOf(_state, BriefingKind::Briefing, BriefingParagraphs(INVASION_BRIEFING_TEXT), BriefingAnswer::Acknowledge));
  co_await WaitForKeyPress(_state, _host);
  _state.Data().commander.thargoidInvasionActive = 1;
  _state.Data().commander.jumpedSinceBriefing = 0;
}

[[nodiscard]] Task<void> ShowMissionDebriefing(GameState& _state, Host& _host)
{
  // Not yet: the mask ship is still about, or the supernova's refugees have not been taken away.
  if (_state.Data().commander.missionNumber == MISSION_MASK_SHIP
        ? _state.Data().commander.maskShipDestroyed != 1
        : _state.Data().commander.missionNumber == MISSION_SUPERNOVA && _state.Data().commander.jumpedSinceBriefing != 1)
  {
    co_return;
  }
  EnterDockedScreen(_state, _host);
  const BriefingShown shown(_host);
  _state.Data().commander.missionStage = STAGE_DEBRIEFED;
  const std::uint8_t mission = _state.Data().commander.missionNumber;
  if (mission == MISSION_SUPERNOVA)
  {
    const bool fullHold = _state.Data().commander.cargoHold.refugeesTonnes == REFUGEE_TONNES;
    _state.Data().docked.refugeeRewardDigit = fullHold ? REWARD_DIGIT_FULL_HOLD : REWARD_DIGIT_OTHERWISE;
    _state.Data().commander.cargoHold.refugeesTonnes = 0;
    _state.Data().commander.cargoHold.cargoUsedTonnes = 0;
    const std::uint16_t rewardTenths = fullHold ? REWARD_FULL_HOLD_TENTHS : REWARD_OTHERWISE_TENTHS;
    AddCredits(_state, rewardTenths);
    // 'Thank you for saving us.' and three lines more, the reward's digit inside the second, which the original writes into it.
    const std::string debriefing =
      JoinTexts({SUPERNOVA_DEBRIEF_OPEN, CharText(_state.Data().docked.refugeeRewardDigit), SUPERNOVA_DEBRIEF_CLOSE});
    _host.ShowBriefing(BriefingOf(_state, BriefingKind::Debriefing, BriefingParagraphs(LinesOf(debriefing, SUPERNOVA_DEBRIEFING_LINES)),
                                  BriefingAnswer::Acknowledge, BriefingReward::Credits, rewardTenths));
    co_await WaitForKeyPress(_state, _host);
    _state.Data().commander.missionNumber = 0;
    _state.Data().commander.missionStage = 0;
    co_return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    std::span<const std::string_view> text = MASK_DESTROYED_LINES;
    if (_state.Data().commander.maskingDeviceRecovered == 1)
    {
      _state.Data().commander.maskingDeviceFitted = 1;
      text = MASK_RECOVERED_LINES;
    }
    else if (_state.Data().commander.fledMaskShip == 1)
    {
      text = MASK_FLED_LINES;
    }
    _host.ShowBriefing(
      BriefingOf(_state, BriefingKind::Debriefing, BriefingParagraphs(text), BriefingAnswer::Acknowledge,
                 _state.Data().commander.maskingDeviceRecovered == 1 ? BriefingReward::MaskingDevice : BriefingReward::None));
    co_await WaitForKeyPress(_state, _host);
    _state.Data().commander.maskMissionShipsLeft = 0;
    _state.Data().commander.maskShipDestroyed = 0;
    _state.Data().commander.missionNumber = 0;
    _state.Data().commander.maskingDeviceRecovered = 0;
    _state.Data().commander.maskSystemJumps = 0;
    _state.Data().commander.fledMaskShip = 0;
    _state.Data().commander.missionStage = 0;
    co_return;
  }
  _host.ShowBriefing(
    BriefingOf(_state, BriefingKind::Debriefing,
               BriefingParagraphs(LinesOf(TextsOf(BytesOf(_state.Data().galaxy.invasionDebriefText)), INVASION_DEBRIEFING_LINES)),
               BriefingAnswer::Acknowledge, BriefingReward::AntiEcmAndArchangel));
  co_await WaitForKeyPress(_state, _host);
  AwardArchangelTitle(_state);
  _state.Data().commander.antiEcmEmulatorFitted = 1;
  _state.Data().commander.missionNumber = 0;
  _state.Data().commander.missionStage = 0;
  _state.Data().commander.invadedStationDestroyed = 0;
  _state.Data().commander.thargoidInvasionActive = 0;
  _state.Data().commander.missionStage = 0;
}

[[nodiscard]] Task<bool> RunTitleAndDocked(GameState& _state, Host& _host)
{
  if (_state.Data().flow.titleShown != 1)
  {
    co_await RunTitle(_state, _host);
  }
  // On into DockedKeyDispatch: back from a disk request, at D10; otherwise at the status screen.
  if (_state.Data().saveLoad.resumeAtDiskMenu == 1)
  {
    co_return co_await DispatchDockedKeys(_state, _host, DockedScreen::Disc);
  }
  co_return co_await DispatchDockedKeys(_state, _host, DockedScreen::Status);
}

} // namespace Elite
