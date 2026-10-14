#include "pch.h"

#include "Docked.h"

#include "Arithmetic.h"
#include "DockedTables.h"
#include "Equipment.h"
#include "FlightSnapshot.h"
#include "Galaxy.h"
#include "Input.h"
#include "Market.h"
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

constexpr std::uint16_t CREDITS_ON_MESSAGE_LINE = 0x78; // B800:0078

constexpr std::uint8_t FUEL_TENTHS_PER_UNIT = 10;
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24; // 255 is 7.0 light years
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
constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_Y = 0x15;
constexpr std::uint8_t SCAN_N = 0x31;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F2 = 0x3C;
constexpr std::uint8_t SCAN_F3 = 0x3D;
constexpr std::uint8_t SCAN_F4 = 0x3E;
constexpr std::uint8_t SCAN_F5 = 0x3F;
constexpr std::uint8_t SCAN_F6 = 0x40;
constexpr std::uint8_t SCAN_F7 = 0x41;
constexpr std::uint8_t SCAN_F8 = 0x42;
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
constexpr std::uint8_t HEADER_ATTRIBUTE = 0x4E;
constexpr std::uint8_t ROW_ATTRIBUTE = 0x4F;
constexpr std::uint8_t STATUS_ATTRIBUTE = 0x0F;
constexpr std::uint8_t EQUIPMENT_HEADING_ATTRIBUTE = 0x0D;
constexpr std::uint16_t PRODUCT_NAME_END = 0x0C; // a product name's 12 characters, then a space
constexpr std::uint16_t UNIT_AFTER_NAME = 0x0D;  // and then its unit
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
constexpr std::uint16_t TRADE_ROWS = 0x11;
// A trade row's price in screenPrices: a product's buy price, then its sell price.
constexpr std::uint16_t MenusState::ScreenPrice::*BUY_PRICE = &MenusState::ScreenPrice::price;
constexpr std::uint16_t MenusState::ScreenPrice::*SELL_PRICE = &MenusState::ScreenPrice::resale;
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

// CALL GetKey; JE back to it, de-assembled: GetKey until a key comes (ADR-039). Returns the key.
KeyPress WaitForKey(GameState& _state, Hardware& _hardware)
{
  for (;;)
  {
    const KeyPress key = GetKey(_state, _hardware);
    if (key.scanCode != 0)
    {
      return key;
    }
    AwaitNextKey(_hardware, key);
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

// The cash without its leading spaces: INC SI from the byte before creditBalanceText while it is on a space. Returns the cash
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

// Product _product's name and unit, up to its NUL.
[[nodiscard]] std::string_view ProductName(const GameState& _state, std::uint16_t _product)
{
  return TextWithin(BytesOf(_state.Data().docked.productNames), std::size_t{_product} * PRODUCT_NAME_BYTES);
}

// What both trade screens start with: the frame from the descriptor _frame and its title, the two lines of help _help, the
// header in its attribute, the rows' attribute, and the prices; cargoRowPointer on the first product, 0. _backward is the
// direction flag, which DrawDockedFrame goes by.
template <typename Frame>
void DrawTradeScreenHeader(GameState& _state, Hardware& _hardware, const Frame& _frame, std::span<const std::string_view> _help)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, _frame), TITLE_CELL);
  PrintTextModeString(_state, _help[0], LINE_3);
  PrintTextModeString(_state, _help[1], LINE_4); // INC SI: the second line, after the first's NUL
  _state.Data().drawing.textAttribute = HEADER_ATTRIBUTE;
  PrintTextModeString(_state, TRADE_HEADER_TEXT, LINE_5);
  _state.Data().drawing.textAttribute = ROW_ATTRIBUTE;
  ComputeMarketPrices(_state);
  _state.Data().docked.cargoRowPointer = 0;
}

// A trade screen's row: the rows left, the product, and the row's place on the text page. The original holds the rows left in
// CX, the product's name in SI, the place in DI and its prices in screenPrices in BX, and pushes them at the start of each
// row.
struct TradeRow
{
  std::uint16_t left;
  std::uint16_t product;
  std::uint16_t cell;
};

constexpr TradeRow FIRST_TRADE_ROW{TRADE_ROWS, 0, LINE_6};

// A trade row's product name and price (PrintNameAndPrice): the price _price of the row's prices, four cells after the name.
// Returns where the price's print stops.
std::uint16_t PrintNameAndPrice(GameState& _state, const TradeRow& _row, std::uint16_t MenusState::ScreenPrice::*_price)
{
  const std::uint16_t named = PrintTextModeString(_state, ProductName(_state, _row.product), _row.cell);
  FormatTenths(_state, ElementWithin(std::span(_state.Data().menus.screenPrices), _row.product).*_price);
  return PrintTextModeString(_state, TextWithin(_state.Data().menus.priceText, 0), Offset(named, 4));
}

// After a row's quantity, printed up to _cell (PrintUnit): the unit after the row's product name.
void PrintUnit(GameState& _state, const TradeRow& _row, std::uint16_t _cell)
{
  PrintTextModeString(
    _state, TextWithin(BytesOf(_state.Data().docked.productNames), std::size_t{_row.product} * PRODUCT_NAME_BYTES + UNIT_AFTER_NAME),
    _cell);
}

// The end of a trade row (EndTradeRow): the next prices, then the next row's place and product, and the count.
[[nodiscard]] TradeRow EndTradeRow(const TradeRow& _row) noexcept
{
  return TradeRow{static_cast<std::uint16_t>(_row.left - 1), static_cast<std::uint16_t>(_row.product + 1),
                  Offset(_row.cell, TEXT_ROW_BYTES)};
}

// RunTitle (CS:7D8B), the title: the ship slots cleared (ClearAllObjects), the music on, the
// player's angles, the view and the lasers zeroed, the cockpit shown (screenLayout 2, which ShowCockpitScreen copies over with the
// direction flag clear), the message line cleared and ---- E L I T E ---- on it; then the planet and the turning ship of type
// titleShipType, in stationSlot at its titleShipDistances, until a key: F9 and F10 step the type through 2-29, any other ends it
// with the music off, the credits and a new game. ES is B800h throughout, from its MOV ES,AX. TitleFrameLoop (CS:7DF1) is one
// frame. Returns what it leaves in BP, PresentSpaceView's 20h from the credits; the direction flag is clear, from their
// FinishSpaceViewFrame.
std::uint16_t RunTitle(GameState& _state, Hardware& _hardware)
{
  ClearAllObjects(_state);
  StartMusic(_state, _hardware);
  _state.Data().commander.laserMountsFitted = 0;
  _state.Data().playerShip.playerPitchAngle = 0;
  _state.Data().playerShip.playerYawAngle = 0;
  _state.Data().playerShip.playerRollAngle = 0;
  _state.Data().playerShip.viewAngle = 0;
  _state.Data().drawing.screenLayout = TITLE_LAYOUT;
  (void)ShowCockpitScreen(_state, _hardware);
  ClearMessageLine(_state);
  _state.Data().drawing.textPaperPattern = 0;
  (void)DrawScreenString(_state, ELITE_TITLE_TEXT, WHITE_MASK, TITLE_TEXT_CELL);
  SpaceObject& ship = _state.Slot(SlotIndex::Station);
  _state.Data().world.shipSlotCount = TITLE_SHIP_SLOTS;
  // XOR AX,AX, then the yaw, the roll, the low words of x and y, the high bytes of x and y as a word, and z's high byte.
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
    _hardware.BeginSpaceViewFrame();
    _hardware.BeginSpaceViewObject();
    _state.Data().drawing.drawColor = TITLE_PLANET_COLOR;
    DrawTitlePlanet(_state, TITLE_PLANET_RADIUS, TITLE_PLANET_X, TITLE_PLANET_Y);
    _hardware.EndSpaceViewRendered();
    ship.flags = TITLE_SHIP_FLAGS;
    // MOV BL,titleShipType / SHL BL,1 / XOR BH,BH: the type doubled indexes titleShipDistances, and INC BL makes it the type byte,
    // active. The ship stands at the type's own distance, turned a step on each axis: the roll, the yaw, then the pitch.
    const auto doubled = static_cast<std::uint8_t>(_state.Data().docked.titleShipType << 1);
    const std::uint16_t distance = TITLE_SHIP_DISTANCES[doubled / 2];
    ship.type = static_cast<std::uint8_t>(doubled + 1);
    ship.z = distance;
    ship.roll = Offset(ship.roll, TITLE_ROLL_STEP);
    ship.yaw = Offset(ship.yaw, TITLE_YAW_STEP);
    ship.pitch = Offset(ship.pitch, TITLE_PITCH_STEP);
    _state.Data().drawing.textPaperPattern = 0;
    DrawViewString(_state, PRESS_ANY_KEY_TEXT, PRESS_ANY_KEY_MASK, PRESS_ANY_KEY_PLACE);
    (void)TransformAndDrawObjects(_state, _hardware);
    FinishSpaceViewFrame(_state, _hardware);
    // The frame as it was drawn, the title's, for a renderer that draws its ship and its planet (ADR-033, ADR-035), with the
    // dashboard the title shows: the cockpit's picture, nothing on it live (D64). DrawDisc's radius is twice the disc's rows.
    const SnapshotDisc planet{.shown = true,
                              .x = static_cast<std::int16_t>(TITLE_PLANET_X),
                              .row = static_cast<std::int16_t>(TITLE_PLANET_Y),
                              .rows = static_cast<std::uint16_t>(TITLE_PLANET_RADIUS >> 1)};
    _hardware.PublishFlightSnapshot(TakeTitleSnapshot(_state, ELITE_TITLE_TEXT, planet), !firstFrame);
    firstFrame = false;
    if (const KeyPress key = GetKey(_state, _hardware); key.scanCode != 0)
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
  StopAllSound(_state, _hardware);
  ShowCredits(_state, _hardware);
  StartNewGame(_state);
  _state.Data().flow.titleShown = 1;
  // The credits' FinishSpaceViewFrame leaves BP, which StartNewGame keeps.
  return PRESENT_SPACE_VIEW_BP;
}

// The screen each F-key shows in DockedKeyDispatch (CS:0B45), in the order it tests them, called as the dispatch calls it: with
// BP, which the screens that select the system at the cursor take as the count when no system is on the
// chart. The status screen's arm (CS:0B96) resets the keyboard first. Every screen leaves ES on B800h, the text page or the
// graphics screen, so a chart shown after another finds it there.
struct DockedScreen
{
  std::uint8_t key;
  ScreenKey (*show)(GameState&, Hardware&, std::uint16_t);
};

constexpr std::array<DockedScreen, 9> DOCKED_SCREENS = {{
  {SCAN_F2, [](GameState& _state, Hardware& _hardware, std::uint16_t /*_countIfNone*/) { return ShowSellCargoScreen(_state, _hardware); }},
  {SCAN_F3, [](GameState& _state, Hardware& _hardware, std::uint16_t /*_countIfNone*/) { return ShowBuyCargoScreen(_state, _hardware); }},
  {SCAN_F4, [](GameState& _state, Hardware& _hardware, std::uint16_t /*_countIfNone*/) { return ShowEquipShipScreen(_state, _hardware); }},
  {SCAN_F5, [](GameState& _state, Hardware& _hardware, std::uint16_t /*_countIfNone*/) { return ShowGalacticChart(_state, _hardware); }},
  {SCAN_F6, [](GameState& _state, Hardware& _hardware, std::uint16_t /*_countIfNone*/) { return ShowShortRangeChart(_state, _hardware); }},
  {SCAN_F7, &ShowSystemDataScreen},
  {SCAN_F8, &ShowMarketPricesScreen},
  {SCAN_F9,
   [](GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone)
   {
     ResetKeyboard(_state, _hardware);
     return ShowCommanderStatusScreen(_state, _hardware, _countIfNone);
   }},
  {SCAN_F10, &ShowInventoryScreen},
}};

// DockedKeyDispatch (CS:0B40), from the arm that shows the screen for _screenKey: F9's status screen (CS:0B96), or Esc's disc menu
// (CS:0BAC). Each screen returns the key that closed it, which picks the next screen, until F1 leaves; Esc is the disc menu, and
// any other key waits for one (CS:0B40). _countIfNone is BP as RunTitleAndDocked leaves it,
// carried from screen to screen as the original's registers carry them, with the key and BP, which the next screen reads; the
// wait's turns carry nothing, as WaitForKey's do.
DockedExit DispatchDockedKeys(GameState& _state, Hardware& _hardware, std::uint8_t _screenKey, std::uint16_t _countIfNone)
{
  std::uint8_t screenKey = _screenKey;
  std::uint16_t countLeft = _countIfNone;
  for (;;)
  {
    ScreenKey key{};
    if (screenKey == SCAN_ESCAPE)
    {
      const DiscMenuExit exit = ShowDiscControlScreen(_state, _hardware);
      if (exit.leaves)
      {
        return DockedExit{true, countLeft};
      }
      key = ScreenKey{exit.scanCode, exit.al, std::nullopt};
    }
    else
    {
      const auto screen = std::ranges::find(DOCKED_SCREENS, screenKey, &DockedScreen::key);
      key = screen->show(_state, _hardware, countLeft);
    }
    countLeft = key.countLeft.value_or(countLeft);
    for (;;)
    {
      if (key.scanCode == SCAN_F1)
      {
        return DockedExit{false, countLeft};
      }
      if (std::ranges::find(DOCKED_SCREENS, key.scanCode, &DockedScreen::key) != DOCKED_SCREENS.end())
      {
        screenKey = key.scanCode;
        break;
      }
      // DEC AH: Esc reaches 0, and shows the disc menu.
      if (key.scanCode == SCAN_ESCAPE)
      {
        screenKey = SCAN_ESCAPE;
        break;
      }
      // Back to the GetKey at 0B40h.
      const KeyPress next = WaitForKey(_state, _hardware);
      key = ScreenKey{next.scanCode, AlAfterKey(key.al, next), std::nullopt};
    }
  }
}

} // namespace

void PrintCreditsOnMessageLine(GameState& _state)
{
  SwapTextAttributeNibbles(_state);
  PrintTextModeString(_state, TextWithin(CashRun(_state), 0), CREDITS_ON_MESSAGE_LINE);
  SwapTextAttributeNibbles(_state);
}

void FormatFuelLightYears(GameState& _state)
{
  // MUL AH by 10, then DIV BL by 36, which cannot overflow: at most 2550/36.
  const auto tenths = static_cast<std::uint16_t>(_state.Data().commander.fuel * FUEL_TENTHS_PER_UNIT);
  DockedState& docked = _state.Data().docked;
  FormatDecimal5(static_cast<std::uint8_t>(tenths / FUEL_UNITS_PER_TENTH), docked.fuelDigits);
  // The units and the tenths, the last two of the five digits, into the text either side of its point.
  docked.fuelUnitsDigit = std::uint8_t{docked.fuelDigits[FUEL_UNITS_DIGIT]};
  docked.fuelTenthsDigit = std::uint8_t{docked.fuelDigits[FUEL_TENTHS_DIGIT]};
}

void DrawDockedFrameAt(GameState& _state, Hardware& _hardware, std::uint8_t _screen, std::uint8_t _frame)
{
  if ((_state.Data().drawing.screenLayout & TEXT_LAYOUT) == 0)
  {
    _state.Data().drawing.screenLayout = TEXT_LAYOUT;
    SetTextMode(_hardware);
  }
  // The screen's attribute, and its background as the border.
  const auto attribute = static_cast<std::uint8_t>(_screen & ATTRIBUTE_MASK);
  _state.Data().drawing.textAttribute = attribute;
  _hardware.SetColorSelect(static_cast<std::uint8_t>(attribute >> 4));
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

std::string_view DrawDockedFrame(GameState& _state, Hardware& _hardware, std::string_view _descriptor)
{
  DrawDockedFrameAt(_state, _hardware, static_cast<std::uint8_t>(_descriptor[0]), static_cast<std::uint8_t>(_descriptor[1]));
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

std::uint8_t AlAfterKey(std::uint8_t _al, const KeyPress& _key) noexcept
{
  // ROL AX,1 / SHR AH,1 when it takes a code: AL shifted left, Shift in bit 0.
  return _key.taken ? static_cast<std::uint8_t>((_al << 1) | (_key.shift ? 1 : 0)) : _al;
}

ScreenKey WaitForScreenExitKey(GameState& _state, Hardware& _hardware, std::uint8_t _ownKey, std::uint8_t _al, std::uint16_t _countIfNone)
{
  // GetKey until Esc or another screen's F-key, every other key and an empty poll going back to 60B4h.
  std::uint8_t al = _al;
  for (;;)
  {
    const KeyPress key = GetKey(_state, _hardware);
    al = AlAfterKey(al, key);
    const std::uint8_t scanCode = key.scanCode;
    if (scanCode != 0 && scanCode != _ownKey && (scanCode == SCAN_ESCAPE || (scanCode >= SCAN_F1 && scanCode <= SCAN_F10)))
    {
      const std::uint16_t countLeft = SelectSystemAtCursor(_state, _countIfNone);
      return ScreenKey{scanCode, al, countLeft};
    }
    AwaitNextKey(_hardware, key);
  }
}

void AwardArchangelTitle(GameState& _state)
{
  for (std::uint16_t letter = 0; letter < RANK_LETTERS; ++letter)
  {
    _state.Data().commander.commanderRankText[letter] = std::uint8_t{ARCHANGEL_TITLE[letter]};
  }
}

ScreenKey ShowSellCargoScreen(GameState& _state, Hardware& _hardware)
{
  DrawTradeScreenHeader(_state, _hardware, SELL_CARGO_FRAME, SELL_CARGO_HELP_LINES);
  TradeRow row = FIRST_TRADE_ROW;
  for (;;)
  {
    const std::uint16_t price = PrintNameAndPrice(_state, row, SELL_PRICE);
    // The units held, from the cargoHold entry of the product cargoRowPointer numbers, which moves on to the next.
    const std::uint16_t product = _state.Data().docked.cargoRowPointer;
    const std::uint8_t held = LotOf(_state, product).held;
    _state.Data().docked.cargoRowPointer = static_cast<std::uint16_t>(product + 1);
    if (held != 0)
    {
      FormatQuantity(held, _state.Data().menus.quantityText);
      const std::uint16_t quantity =
        PrintTextModeString(_state, TextWithin(BytesOf(_state.Data().menus.quantityText), 0), Offset(price, 2));
      PrintUnit(_state, row, quantity);
    }
    else
    {
      PrintTextModeString(_state, NO_QUANTITY_TEXT, price);
    }
    row = EndTradeRow(row);
    if (row.left == 0)
    {
      break;
    }
  }
  _state.Data().docked.tradeScreenIsBuy = 0;
  return RunCargoTradeMenu(_state, _hardware);
}

ScreenKey ShowBuyCargoScreen(GameState& _state, Hardware& _hardware)
{
  StartMarketRandom(_state);
  DrawTradeScreenHeader(_state, _hardware, BUY_CARGO_FRAME, BUY_CARGO_HELP_LINES);
  TradeRow row = FIRST_TRADE_ROW;
  for (;;)
  {
    const std::uint16_t price = PrintNameAndPrice(_state, row, BUY_PRICE);
    std::optional<std::uint8_t> onSale;
    if (_state.Data().commander.marketQuantitiesSet == 0)
    {
      // The quantity on sale, the first time after an arrival.
      onSale = RollQuantityOnSale(_state);
    }
    else
    {
      const std::uint8_t drawn = LotOf(_state, _state.Data().docked.cargoRowPointer).onSale;
      if (drawn != 0)
      {
        onSale = drawn;
      }
    }
    // The quantity into the market's byte of the product's cargoHold entry, and cargoRowPointer on to the next product.
    if (onSale)
    {
      FormatQuantity(*onSale, _state.Data().menus.quantityText);
      const std::uint16_t quantity =
        PrintTextModeString(_state, TextWithin(BytesOf(_state.Data().menus.quantityText), 0), Offset(price, 2));
      const std::uint16_t product = _state.Data().docked.cargoRowPointer;
      LotOf(_state, product).onSale = *onSale;
      _state.Data().docked.cargoRowPointer = static_cast<std::uint16_t>(product + 1);
      PrintUnit(_state, row, quantity);
    }
    else
    {
      PrintTextModeString(_state, NO_QUANTITY_TEXT, price);
      const std::uint16_t product = _state.Data().docked.cargoRowPointer;
      LotOf(_state, product).onSale = 0;
      _state.Data().docked.cargoRowPointer = static_cast<std::uint16_t>(product + 1);
      // The jump back into the row's end, with CX the count and BX the next prices.
    }
    row = EndTradeRow(row);
    if (row.left == 0)
    {
      break;
    }
  }
  _state.Data().docked.tradeScreenIsBuy = 1;
  _state.Data().commander.marketQuantitiesSet = 1;
  return RunCargoTradeMenu(_state, _hardware);
}

ScreenKey ShowCommanderStatusScreen(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone)
{
  if (_state.Data().flow.inFlight != 1 && _state.Data().commander.missionNumber != 0)
  {
    if (_state.Data().commander.missionStage == 0)
    {
      ShowMissionBriefing(_state, _hardware);
    }
    else if (_state.Data().commander.missionStage == STAGE_BRIEFED && _state.Data().flight.playerDocked == 1)
    {
      ShowMissionDebriefing(_state, _hardware);
    }
  }
  const CommanderState& commander = _state.Data().commander;
  DrawDockedFrameAt(_state, _hardware, commander.commanderTitle[0], commander.commanderTitle[1]);
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
  const std::uint8_t legal = _state.Data().commander.legalStatus;
  const std::uint16_t standing = legal < LEGAL_STATUS_OFFENDER ? 0 : legal < LEGAL_STATUS_FUGITIVE ? 1 : 2;
  PrintTextModeString(_state, LEGAL_STATUS_NAMES[standing], printed);
  printed = PrintTextModeString(_state, RATING_LABEL_TEXT, LINE_8);
  // The rating: the first threshold above the kill count. The last threshold is above any kill count (killCount stops at FEh),
  // so the rating is one of the nine.
  const std::uint8_t kills = _state.Data().commander.killCount;
  std::uint16_t rating = 0;
  while (kills >= RATING_THRESHOLDS[rating])
  {
    ++rating;
  }
  PrintTextModeString(_state, RATING_NAMES[rating], printed);
  _state.Data().drawing.textAttribute = EQUIPMENT_HEADING_ATTRIBUTE;
  PrintTextModeString(_state, EQUIPMENT_HEADING_TEXT, LINE_9);
  _state.Data().drawing.textAttribute = STATUS_ATTRIBUTE;
  // AL keeps the missiles' digit until a row prints, whose NUL then leaves 0 there.
  auto al = static_cast<std::uint8_t>(_state.Data().commander.missileCount + DIGIT_ZERO);
  _state.Data().docked.missileCountDigit = al;

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
      al = 0;
      if (SelectLaserType(_state))
      {
        std::uint16_t cell = PrintTextModeString(_state, MOUNTS_OPEN_TEXT, named);
        // Each mount with this type of laser, from laserMountsFitted in DL and laserMountTypes in DH.
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
  return WaitForScreenExitKey(_state, _hardware, SCAN_F9, al, _countIfNone);
}

ScreenKey ShowInventoryScreen(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, INVENTORY_FRAME), TITLE_CELL);
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
  // The last row leaves AL 0: an empty one its amount, a printed one the NUL its last print stops at.
  return WaitForScreenExitKey(_state, _hardware, SCAN_F10, 0, _countIfNone);
}

void ShowMissionBriefing(GameState& _state, Hardware& _hardware)
{
  _state.Data().commander.missionStage = STAGE_BRIEFED;
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, EMERGENCY_FRAME), TITLE_CELL);
  const std::uint8_t mission = _state.Data().commander.missionNumber;
  if (mission == MISSION_SUPERNOVA)
  {
    PrintTextLines(_state, SUPERNOVA_BRIEFING_TEXT, LINE_4);
    // Y takes the refugees, N refuses; every other key goes back to the GetKey at 6E23, as an empty one does.
    std::uint8_t answer = 0;
    for (;;)
    {
      answer = WaitForKey(_state, _hardware).scanCode;
      if (answer == SCAN_Y || answer == SCAN_N)
      {
        break;
      }
    }
    std::span<const std::string_view> reply = SUPERNOVA_REFUSED_LINES;
    if (answer == SCAN_Y)
    {
      // The hold emptied for the refugees: MOV [DI],CH, which is 0, at each of the 17 amounts.
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
    WaitForKeyPress(_state, _hardware);
    _state.Data().commander.supernovaFrames = SUPERNOVA_DELAY_FRAMES;
    _state.Data().commander.jumpedSinceBriefing = 0;
    return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    PrintTextLines(_state, MASK_BRIEFING_TEXT, LINE_4);
    WaitForKeyPress(_state, _hardware);
    _state.Data().commander.maskMissionShipsLeft = MASK_SHIPS;
    _state.Data().commander.maskSystemJumps = MASK_SYSTEM_JUMPS;
    return;
  }
  PrintTextLines(_state, INVASION_BRIEFING_TEXT, LINE_4);
  WaitForKeyPress(_state, _hardware);
  _state.Data().commander.thargoidInvasionActive = 1;
  _state.Data().commander.jumpedSinceBriefing = 0;
}

void ShowMissionDebriefing(GameState& _state, Hardware& _hardware)
{
  // Not yet: the mask ship is still about, or the supernova's refugees have not been taken away.
  if (_state.Data().commander.missionNumber == MISSION_MASK_SHIP
        ? _state.Data().commander.maskShipDestroyed != 1
        : _state.Data().commander.missionNumber == MISSION_SUPERNOVA && _state.Data().commander.jumpedSinceBriefing != 1)
  {
    return;
  }
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, TASK_COMPLETE_FRAME), TITLE_CELL);
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
    WaitForKeyPress(_state, _hardware);
    _state.Data().commander.missionNumber = 0;
    _state.Data().commander.missionStage = 0;
    return;
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
    WaitForKeyPress(_state, _hardware);
    _state.Data().commander.maskMissionShipsLeft = 0;
    _state.Data().commander.maskShipDestroyed = 0;
    _state.Data().commander.missionNumber = 0;
    _state.Data().commander.maskingDeviceRecovered = 0;
    _state.Data().commander.maskSystemJumps = 0;
    _state.Data().commander.fledMaskShip = 0;
    _state.Data().commander.missionStage = 0;
    return;
  }
  PrintTextLines(_state, TextsOf(BytesOf(_state.Data().galaxy.invasionDebriefText)), LINE_4, INVASION_DEBRIEFING_LINES);
  WaitForKeyPress(_state, _hardware);
  AwardArchangelTitle(_state);
  _state.Data().commander.antiEcmEmulatorFitted = 1;
  _state.Data().commander.missionNumber = 0;
  _state.Data().commander.missionStage = 0;
  _state.Data().commander.invadedStationDestroyed = 0;
  _state.Data().commander.thargoidInvasionActive = 0;
  _state.Data().commander.missionStage = 0;
}

DockedExit RunTitleAndDocked(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone)
{
  std::uint16_t countIfNone = _countIfNone;
  if (_state.Data().flow.titleShown != 1)
  {
    countIfNone = RunTitle(_state, _hardware);
  }
  // JMP into DockedKeyDispatch: back from a disk request, at the disc menu (CS:0BAC); otherwise at the status screen (CS:0B96),
  // with BP, which the status screen reads.
  if (_state.Data().saveLoad.resumeAtDiskMenu == 1)
  {
    return DispatchDockedKeys(_state, _hardware, SCAN_ESCAPE, countIfNone);
  }
  return DispatchDockedKeys(_state, _hardware, SCAN_F9, countIfNone);
}

} // namespace Elite
