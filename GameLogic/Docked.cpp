#include "pch.h"

#include "Docked.h"

#include "Arithmetic.h"
#include "ChartScreen.h"
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
// fuelDigits' last two, the units and the tenths, which go either side of the fuel text's point.
constexpr std::size_t FUEL_UNITS_DIGIT = 3;
constexpr std::size_t FUEL_TENTHS_DIGIT = 4;

constexpr std::uint8_t TEXT_LAYOUT = 2;
constexpr std::uint8_t ATTRIBUTE_MASK = 0x7F; // no blinking
constexpr std::uint8_t FRAME_ROW_CHARACTER = 0xCD;
constexpr std::uint8_t FRAME_SIDE_CHARACTER = 0xBA;
constexpr std::uint16_t FRAME_ROW_CELLS = 0x26;
constexpr std::uint16_t FRAME_TOP_ROW = 0x002;    // line 0, column 1
constexpr std::uint16_t FRAME_TITLE_ROW = 0x0A2;  // line 2
constexpr std::uint16_t FRAME_BOTTOM_ROW = 0x782; // line 24
constexpr std::uint16_t FRAME_SIDES = 0x050;      // line 1, column 0
constexpr std::uint16_t FRAME_SIDE_ROWS = 0x17;
constexpr std::uint16_t FRAME_RIGHT_SIDE = 0x4E; // column 39
constexpr std::uint16_t TEXT_ROW_BYTES = 0x50;

// Scan codes.
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F9 = 0x43;
constexpr std::uint8_t SCAN_F10 = 0x44;

// The docked screens' texts and places, in the data segment and on the 40-column text page.
constexpr std::uint16_t TITLE_CELL = 0x54;
constexpr std::uint16_t LINE_3 = 0xF4;
constexpr std::uint16_t LINE_4 = 0x144;
constexpr std::uint16_t LINE_5 = 0x194;
constexpr std::uint16_t LINE_6 = 0x1E4;
constexpr std::uint16_t LINE_7 = 0x234;
constexpr std::uint16_t LINE_8 = 0x284;
constexpr std::uint16_t LINE_9 = 0x2D4;
constexpr std::uint16_t LINE_10 = 0x324;
constexpr std::uint8_t STATUS_ATTRIBUTE = 0x0F;
constexpr std::uint8_t EQUIPMENT_HEADING_ATTRIBUTE = 0x0D;
constexpr std::uint16_t PRODUCT_NAME_END = 0x0C; // a product name's 12 characters, then a space
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
constexpr std::uint16_t TRADE_ROWS = 0x11;
constexpr std::uint16_t INVENTORY_ROWS = 0x12; // the 17 products and the refugees
constexpr std::uint16_t QUANTITY_DIGITS_BLANKED = 4;
constexpr std::uint8_t LEGAL_STATUS_OFFENDER = 1;
constexpr std::uint8_t LEGAL_STATUS_FUGITIVE = 0x28;
constexpr std::uint8_t DIGIT_ZERO = 0x30;
constexpr std::uint16_t EQUIPMENT_ROWS = 0x0D;
constexpr std::uint8_t FIRST_EQUIPMENT_MENU_ROW = 2;
constexpr std::uint16_t LASER_MOUNTS = 4;
constexpr std::uint8_t LASER_MOUNT_TYPE_MASK = 3;
constexpr std::uint16_t INVENTORY_CASH_BACK = 0x10; // the cash eight cells nearer its label than the status screen's
// The inventory's quantity, which FormatQuantity writes: a buffer past productNames' rows, which the symbol table sizes to take it.
constexpr std::size_t INVENTORY_QUANTITY_IN_NAMES = std::size_t{INVENTORY_ROWS} * PRODUCT_NAME_BYTES;
constexpr std::size_t QUANTITY_TEXT_BYTES = 6; // five digits and a NUL
constexpr std::uint16_t FUEL_LABEL_BYTES = 6;  // 'Fuel: ', where a NUL cuts the fuel text
constexpr std::uint16_t RANK_LETTERS = 9;      // 'ARCHANGEL' over 'COMMANDER'
constexpr std::uint8_t SPACE = 0x20;
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

constexpr std::uint16_t TITLE_TEXT_CELL = 0x65;
constexpr std::uint16_t WHITE_MASK = 0xFFFF;
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

// The cash's text and its NUL: FormatCredits fills all but the last of creditBalanceText's bytes.
[[nodiscard]] std::span<const std::uint8_t> CashRun(const GameState& _state)
{
  return _state.Data().commander.creditBalanceText;
}

// The selected system's name and its NUL, where TerminateSelectedSystemName leaves it after an eight-letter name.
[[nodiscard]] std::span<const std::uint8_t> SelectedNameRun(const GameState& _state)
{
  return _state.Data().commander.selectedSystem.name;
}

// The cash without its leading spaces: a step at a time along creditBalanceText while it is on a space. Returns the cash
// from the first character that is not a space.
std::string_view SkipCashSpaces(const GameState& _state)
{
  const std::uint8_t(&cash)[sizeof(CommanderState::creditBalanceText)] = _state.Data().commander.creditBalanceText;
  for (std::size_t first = 0;; ++first)
  {
    if (cash[first] != SPACE)
    {
      return TextWithin(CashRun(_state), first);
    }
  }
}

// _quantity as five digits into _text, up to four of their leading zeros blanked.
void FormatQuantity(std::uint16_t _quantity, std::span<std::uint8_t> _text)
{
  FormatDecimal5(_quantity, _text);
  BlankLeadingZeros(_text, static_cast<std::uint8_t>(QUANTITY_DIGITS_BLANKED));
}

// The status screen's title, which the original reads as a text from the rank on, up to a NUL: commanderRankText, then the
// space and the NUL after it, which only a commander file writes; a NUL past them would be in the cash's text, which ends in one.
[[nodiscard]] std::string CommanderTitleText(const GameState& _state)
{
  const CommanderState& commander = _state.Data().commander;
  std::string text;
  for (const std::span<const std::uint8_t> piece :
       {std::span<const std::uint8_t>(commander.commanderRankText), std::span<const std::uint8_t>(commander.rankTextEnd),
        std::span<const std::uint8_t>(commander.creditBalanceText)})
  {
    const std::string_view part = TextIn(piece);
    text += part;
    if (part.size() < piece.size())
    {
      break;
    }
  }
  return text;
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

// The fuel's 'n.n Light Years': its two digits, which FormatFuelLightYears writes, either side of its point.
[[nodiscard]] std::string FuelLightYearsText(const GameState& _state)
{
  const DockedState& docked = _state.Data().docked;
  return JoinTexts({CharText(docked.fuelUnitsDigit), FUEL_TEXT_POINT, CharText(docked.fuelTenthsDigit), FUEL_TEXT_SUFFIX});
}

// RunTitle, the title: the ship slots cleared (ClearAllObjects), the music on, the
// player's angles, the view and the lasers zeroed, the cockpit shown (screenLayout 2, which ShowCockpitScreen copies over with the
// direction flag clear), the message line cleared and ---- E L I T E ---- on it; then the planet and the turning ship of type
// titleShipType, in stationSlot at its titleShipDistances, until a key: F9 and F10 step the type through 2-29, any other ends it
// with the music off, the credits and a new game. It writes video memory at B800h throughout. TitleFrameLoop is one
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
  ClearMessageLine(_state);
  _state.Data().drawing.textPaperPattern = 0;
  (void)DrawScreenString(_state, ELITE_TITLE_TEXT, WHITE_MASK, TITLE_TEXT_CELL);
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

// The routine that shows _screen docked (D96). The status screen resets the keyboard first; Esc's disc menu is DispatchDockedKeys's.
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

// The docked host (D96): from _screen, the status screen or Esc's disc menu, each screen returns the key that closed it, which
// picks the next screen (DockedTargetOf), until F1 leaves; any other key waits for one. Returns whether the disc menu left for the
// disk, through LeaveGameLoopForDisk, which returns past RunTitleAndDocked (DiscMenuExit); otherwise F1 ended them.
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
      const DiscMenuExit exit = co_await ShowDiscControlScreen(_state, _host);
      if (exit.leaves)
      {
        co_return true;
      }
      key = ScreenKey{exit.scanCode};
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

void FormatFuelLightYears(GameState& _state)
{
  // The fuel times 10, then divided by 36 to a byte, which cannot overflow: at most 2550/36.
  const auto tenths = static_cast<std::uint16_t>(_state.Data().commander.fuel * FUEL_TENTHS_PER_UNIT);
  DockedState& docked = _state.Data().docked;
  FormatDecimal5(static_cast<std::uint8_t>(tenths / FUEL_UNITS_PER_TENTH), docked.fuelDigits);
  // The units and the tenths, the last two of the five digits, into the text either side of its point.
  docked.fuelUnitsDigit = std::uint8_t{docked.fuelDigits[FUEL_UNITS_DIGIT]};
  docked.fuelTenthsDigit = std::uint8_t{docked.fuelDigits[FUEL_TENTHS_DIGIT]};
}

void DrawDockedFrameAt(GameState& _state, Host& _host, std::uint8_t _screen, std::uint8_t _frame)
{
  if ((_state.Data().drawing.screenLayout & TEXT_LAYOUT) == 0)
  {
    _state.Data().drawing.screenLayout = TEXT_LAYOUT;
    SetTextMode(_state, _host);
  }
  // The screen's attribute, and its background as the border.
  const auto attribute = static_cast<std::uint8_t>(_screen & ATTRIBUTE_MASK);
  _state.Data().drawing.textAttribute = attribute;
  _host.Video().SetColorSelect(static_cast<std::uint8_t>(attribute >> 4));
  ClearTextScreen(_state);

  const std::uint8_t frame = _frame;
  for (const std::uint16_t row : {FRAME_TOP_ROW, FRAME_TITLE_ROW, FRAME_BOTTOM_ROW})
  {
    DrawFrameRow(_state, row, Join(frame, FRAME_ROW_CHARACTER));
  }
  DrawFrameSides(_state, FRAME_SIDES, FRAME_SIDE_ROWS, frame);
  for (const FrameCorner& corner : FRAME_CORNERS)
  {
    const auto cell = static_cast<std::uint16_t>(corner.row * TEXT_ROW_BYTES + corner.column * 2);
    _state.SetVideoWord(cell, Join(frame, corner.character));
  }
}

std::string_view DrawDockedFrame(GameState& _state, Host& _host, std::string_view _descriptor)
{
  DrawDockedFrameAt(_state, _host, static_cast<std::uint8_t>(_descriptor[0]), static_cast<std::uint8_t>(_descriptor[1]));
  return _descriptor.substr(2);
}

void DrawFrameSides(GameState& _state, std::uint16_t _cell, std::uint16_t _rows, std::uint8_t _attribute)
{
  const std::uint16_t side = Join(_attribute, FRAME_SIDE_CHARACTER);
  std::uint16_t cell = _cell;
  for (std::uint32_t row = 0; row < LoopCount(_rows); ++row)
  {
    _state.SetVideoWord(cell, side);
    _state.SetVideoWord(Offset(cell, FRAME_RIGHT_SIDE), side);
    cell = Offset(cell, TEXT_ROW_BYTES);
  }
}

void DrawFrameRow(GameState& _state, std::uint16_t _cell, std::uint16_t _value)
{
  std::uint16_t cell = _cell;
  for (std::uint16_t left = FRAME_ROW_CELLS; left != 0; --left)
  {
    _state.SetVideoWord(cell, _value);
    cell = Offset(cell, 2);
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
  const CommanderState& commander = _state.Data().commander;
  DrawDockedFrameAt(_state, _host, commander.commanderTitle[0], commander.commanderTitle[1]);
  const std::uint16_t title = PrintTextModeString(_state, CommanderTitleText(_state), TITLE_CELL);
  PrintTextModeString(_state, TextWithin(BytesOf(_state.Data().commander.defaultCommanderName), 0), title);
  _state.Data().drawing.textAttribute = STATUS_ATTRIBUTE;
  const bool inWitchSpace = _state.Data().commander.witchspaceCountdown != 0;
  std::uint16_t printed = PrintTextModeString(_state, SYSTEM_LABEL_TEXT, LINE_3);
  PrintTextModeString(_state, inWitchSpace ? WITCH_SPACE_TEXT : TextWithin(BytesOf(_state.Data().commander.currentSystem.name), 0),
                      printed);
  printed = PrintTextModeString(_state, HYPERSYSTEM_LABEL_TEXT, LINE_4);
  PrintTextModeString(_state, inWitchSpace ? WITCH_SPACE_TEXT : TextWithin(SelectedNameRun(_state), 0), printed);
  FormatFuelLightYears(_state);
  PrintTextModeString(_state, JoinTexts({FUEL_TEXT_PREFIX, FuelLightYearsText(_state)}), LINE_5);
  printed = PrintTextModeString(_state, CASH_LABEL_TEXT, LINE_6);
  PrintTextModeString(_state, SkipCashSpaces(_state), printed);
  printed = PrintTextModeString(_state, LEGAL_STATUS_LABEL_TEXT, LINE_7);
  PrintTextModeString(_state, LEGAL_STATUS_NAMES[static_cast<std::size_t>(LegalStandingOf(_state.Data().commander.legalStatus))], printed);
  printed = PrintTextModeString(_state, RATING_LABEL_TEXT, LINE_8);
  PrintTextModeString(_state, RATING_NAMES[RatingOf(_state.Data().commander.killCount)], printed);
  _state.Data().drawing.textAttribute = EQUIPMENT_HEADING_ATTRIBUTE;
  PrintTextModeString(_state, EQUIPMENT_HEADING_TEXT, LINE_9);
  _state.Data().drawing.textAttribute = STATUS_ATTRIBUTE;
  _state.Data().docked.missileCountDigit = static_cast<std::uint8_t>(_state.Data().commander.missileCount + DIGIT_ZERO);

  // One row for each item fitted, and after a laser the mounts it is fitted to.
  std::uint16_t row = LINE_10;
  // The equipment items from the missiles (EQUIPMENT_ITEMS), the fuel's after.
  const std::span<std::uint8_t CommanderState::*const> fitted = std::span(EQUIPMENT_ITEMS).subspan(1, EQUIPMENT_ROWS);
  std::size_t item = 0;
  _state.Data().menus.menuSelectedRow = FIRST_EQUIPMENT_MENU_ROW;
  for (std::uint16_t left = EQUIPMENT_ROWS;;)
  {
    if (_state.Data().commander.*fitted[item] != 0)
    {
      // The missiles' name has their count's digit inside it, which the original writes into the text above; the rest are
      // constant.
      const std::string missiles = JoinTexts({MISSILE_NAME_OPEN, CharText(_state.Data().docked.missileCountDigit), MISSILE_NAME_CLOSE});
      const std::string_view name = item == 0 ? std::string_view(missiles) : EQUIPMENT_NAMES[item];
      const std::uint16_t named = PrintTextModeString(_state, name, row);
      if (SelectLaserType(_state))
      {
        std::uint16_t cell = PrintTextModeString(_state, MOUNTS_OPEN_TEXT, named);
        // Each mount with this type of laser, from laserMountsFitted and laserMountTypes, a bit of each at a time.
        std::uint8_t mounts = _state.Data().commander.laserMountsFitted;
        std::uint8_t types = _state.Data().commander.laserMountTypes;
        std::size_t mountName = 0;
        for (std::uint16_t mount = LASER_MOUNTS;;)
        {
          const bool on = (mounts & 1) != 0;
          mounts = static_cast<std::uint8_t>(mounts >> 1);
          if (on && (types & LASER_MOUNT_TYPE_MASK) == _state.Data().equipment.selectedLaserType)
          {
            cell = PrintTextModeString(_state, LASER_MOUNT_NAMES[mountName], cell);
          }
          ++mountName;
          types = static_cast<std::uint8_t>(types >> 2);
          if (--mount == 0)
          {
            break;
          }
        }
        PrintTextModeString(_state, MOUNTS_CLOSE_TEXT, static_cast<std::uint16_t>(cell - 2));
      }
      row = Offset(row, TEXT_ROW_BYTES);
    }
    ++item;
    _state.Data().menus.menuSelectedRow = static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow + 1);
    if (--left == 0)
    {
      break;
    }
  }
  // Docked, D08 shows over the screen while it waits (ADR-063).
  const StatusShown shown(_host, _place == ScreenPlace::Docked ? std::optional<StatusView>(StatusViewOf(_state)) : std::nullopt);
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

[[nodiscard]] Task<ScreenKey> ShowInventoryScreen(GameState& _state, Host& _host, ScreenPlace _place)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _host, INVENTORY_FRAME), TITLE_CELL);
  // The fuel text without the padding the status screen has: 'Fuel: ', which the original cuts from the text by a NUL that
  // becomes a space again, then 'n.n Light Years'.
  FormatFuelLightYears(_state);
  const std::uint16_t labelEnd = PrintTextModeString(_state, FUEL_TEXT_PREFIX.substr(0, FUEL_LABEL_BYTES), LINE_3);
  PrintTextModeString(_state, FuelLightYearsText(_state), labelEnd);
  const std::uint16_t cash = PrintTextModeString(_state, CASH_LABEL_TEXT, LINE_4);
  const auto cashCell = static_cast<std::uint16_t>(cash - INVENTORY_CASH_BACK);
  PrintTextModeString(_state, SkipCashSpaces(_state), cashCell);

  // Every product held, and the refugees: name, quantity, unit, a row each; an empty row does not move the place on.
  const std::span<std::uint8_t> quantityText =
    std::span<std::uint8_t>(_state.Data().docked.productNames).subspan(INVENTORY_QUANTITY_IN_NAMES, QUANTITY_TEXT_BYTES);
  std::uint16_t row = LINE_6;
  std::uint16_t product = 0;
  for (std::uint16_t left = INVENTORY_ROWS;;)
  {
    const std::uint8_t amount = HeldOf(_state, product);
    if (amount != 0)
    {
      // The name cut at its twelfth character by a NUL, which becomes a space again before the unit after it.
      const std::span<std::uint8_t> name =
        BytesOf(_state.Data().docked.productNames).subspan(std::size_t{product} * PRODUCT_NAME_BYTES, PRODUCT_NAME_BYTES);
      name[PRODUCT_NAME_END] = NUL;
      const std::string_view named = TextIn(name);
      const std::uint16_t nameEnd = PrintTextModeString(_state, named, row);
      name[named.size()] = SPACE;
      FormatQuantity(amount, quantityText);
      const std::uint16_t quantity = PrintTextModeString(_state, TextIn(quantityText), Offset(nameEnd, 4));
      PrintTextModeString(_state, TextWithin(name, named.size()), quantity);
      row = Offset(row, TEXT_ROW_BYTES);
    }
    ++product;
    if (--left == 0)
    {
      break;
    }
  }
  co_return co_await WaitForScreenExitKey(_state, _host, SCAN_F10, _place);
}

[[nodiscard]] Task<void> ShowMissionBriefing(GameState& _state, Host& _host)
{
  _state.Data().commander.missionStage = STAGE_BRIEFED;
  PrintTextModeString(_state, DrawDockedFrame(_state, _host, EMERGENCY_FRAME), TITLE_CELL);
  const std::uint8_t mission = _state.Data().commander.missionNumber;
  if (mission == MISSION_SUPERNOVA)
  {
    PrintTextLines(_state, SUPERNOVA_BRIEFING_TEXT, LINE_4);
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
    PrintTextLines(_state, reply, LINE_10);
    co_await WaitForKeyPress(_state, _host);
    _state.Data().commander.supernovaFrames = SUPERNOVA_DELAY_FRAMES;
    _state.Data().commander.jumpedSinceBriefing = 0;
    co_return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    PrintTextLines(_state, MASK_BRIEFING_TEXT, LINE_4);
    co_await WaitForKeyPress(_state, _host);
    _state.Data().commander.maskMissionShipsLeft = MASK_SHIPS;
    _state.Data().commander.maskSystemJumps = MASK_SYSTEM_JUMPS;
    co_return;
  }
  PrintTextLines(_state, INVASION_BRIEFING_TEXT, LINE_4);
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
  PrintTextModeString(_state, DrawDockedFrame(_state, _host, TASK_COMPLETE_FRAME), TITLE_CELL);
  _state.Data().commander.missionStage = STAGE_DEBRIEFED;
  const std::uint8_t mission = _state.Data().commander.missionNumber;
  if (mission == MISSION_SUPERNOVA)
  {
    const bool fullHold = _state.Data().commander.cargoHold.refugeesTonnes == REFUGEE_TONNES;
    _state.Data().docked.refugeeRewardDigit = fullHold ? REWARD_DIGIT_FULL_HOLD : REWARD_DIGIT_OTHERWISE;
    _state.Data().commander.cargoHold.refugeesTonnes = 0;
    _state.Data().commander.cargoHold.cargoUsedTonnes = 0;
    AddCredits(_state, fullHold ? REWARD_FULL_HOLD_TENTHS : REWARD_OTHERWISE_TENTHS);
    // 'Thank you for saving us.' and three lines more, the reward's digit inside the second, which the original writes into it.
    const std::string debriefing =
      JoinTexts({SUPERNOVA_DEBRIEF_OPEN, CharText(_state.Data().docked.refugeeRewardDigit), SUPERNOVA_DEBRIEF_CLOSE});
    PrintTextLines(_state, debriefing, LINE_4, SUPERNOVA_DEBRIEFING_LINES);
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
    PrintTextLines(_state, text, LINE_4);
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
  PrintTextLines(_state, TextsOf(BytesOf(_state.Data().galaxy.invasionDebriefText)), LINE_4, INVASION_DEBRIEFING_LINES);
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
  // On into DockedKeyDispatch: back from a disk request, at the disc menu; otherwise at the status screen.
  if (_state.Data().saveLoad.resumeAtDiskMenu == 1)
  {
    co_return co_await DispatchDockedKeys(_state, _host, DockedScreen::Disc);
  }
  co_return co_await DispatchDockedKeys(_state, _host, DockedScreen::Status);
}

} // namespace Elite
