#include "pch.h"

#include "Docked.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "DockedTables.h"
#include "Equipment.h"
#include "Galaxy.h"
#include "Input.h"
#include "Market.h"
#include "SaveLoad.h"
#include "Scene.h"
#include "Ships.h"
#include "Sound.h"
#include "StartUp.h"
#include "Text.h"
#include "Video.h"

#include <algorithm>
#include <initializer_list>

namespace Elite
{

namespace
{

constexpr std::uint16_t CREDITS_ON_MESSAGE_LINE = 0x78; // B800:0078

constexpr std::uint8_t FUEL_TENTHS_PER_UNIT = 10;
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24; // 255 is 7.0 light years
constexpr std::uint16_t FUEL_TEXT = 0x83E9;         // 'Fuel:  n.n Light Years'
constexpr std::uint16_t FUEL_DIGITS = 0x8407;       // FormatDecimal5's five, of which the last two are shown

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

// Where the original jumps back (Hardware::LoopTurn, ADR-015).
constexpr std::uint16_t DOCKED_KEY_DISPATCH = 0x0B40;
constexpr std::uint16_t DOCKED_KEY_TEST = 0x0B45; // after a screen, the key it returned
constexpr std::uint16_t DOCKED_STATUS_ENTRY = 0x0B96;
constexpr std::uint16_t DOCKED_DISK_MENU_ENTRY = 0x0BAC;
constexpr std::uint16_t SELL_CARGO_ROW = 0x5A75;
constexpr std::uint16_t BUY_CARGO_ROW = 0x5B40;
constexpr std::uint16_t BUY_CARGO_ROW_END = 0x5BBA;
constexpr std::uint16_t STATUS_CASH_SPACE = 0x5F32;
constexpr std::uint16_t STATUS_RATING = 0x5F70;
constexpr std::uint16_t STATUS_EQUIPMENT_ROW = 0x5FAD;
constexpr std::uint16_t STATUS_LASER_MOUNT = 0x5FD4;
constexpr std::uint16_t INVENTORY_CASH_SPACE = 0x6051;
constexpr std::uint16_t INVENTORY_ROW = 0x6066;
constexpr std::uint16_t INVENTORY_ROW_END = 0x60A1;
constexpr std::uint16_t WAIT_FOR_SCREEN_EXIT_KEY = 0x60B4;
constexpr std::uint16_t SUPERNOVA_ANSWER = 0x6E23;
constexpr std::uint16_t EVACUATE_CARGO = 0x6E4A;
constexpr std::uint16_t MASK_DEBRIEFING_TEXT = 0x6F52;
constexpr std::uint16_t TITLE_FRAME_LOOP = 0x7DF1;

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
constexpr std::uint16_t SELL_CARGO_HELP_TEXT = 0x8C6B; // 'Use control keys or device to select', then its second line
constexpr std::uint16_t BUY_CARGO_HELP_TEXT = 0x8C25;
constexpr std::uint16_t TRADE_HEADER_TEXT = 0x7BEC; // 'PRODUCT    UNIT    PRICE    QTY'
constexpr std::uint16_t NO_QUANTITY_TEXT = 0x821B;  // '     -'
constexpr std::uint16_t PRODUCT_NAME_END = 0x0C;    // a product name's 12 characters, then a space
constexpr std::uint16_t UNIT_AFTER_NAME = 0x0D;     // and then its unit
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
constexpr std::uint16_t TRADE_ROWS = 0x11;
constexpr std::uint16_t TRADE_PRICES_BYTES = 4; // a product's buy price, then its sell price, in screenPrices
constexpr std::uint16_t BUY_PRICE = 0;
constexpr std::uint16_t SELL_PRICE = 2;
constexpr std::uint16_t INVENTORY_ROWS = 0x12; // the 17 products and the refugees
constexpr std::uint16_t QUANTITY_DIGITS_BLANKED = 4;
constexpr std::uint16_t SYSTEM_LABEL_TEXT = 0x83CB;      // 'System:'
constexpr std::uint16_t HYPERSYSTEM_LABEL_TEXT = 0x83DA; // 'Hypersystem:'
constexpr std::uint16_t WITCH_SPACE_TEXT = 0x8E30;       // 'WITCH SPACE'
constexpr std::uint16_t CASH_LABEL_TEXT = 0x840C;        // 'Cash:'
constexpr std::uint16_t LEGAL_STATUS_LABEL_TEXT = 0x841B;
constexpr std::uint16_t RATING_LABEL_TEXT = 0x8448;
constexpr std::uint16_t EQUIPMENT_HEADING_TEXT = 0x84C7; // 'EQUIPMENT:'
constexpr std::uint16_t MOUNTS_OPEN_TEXT = 0x8214;       // ' ('
constexpr std::uint16_t MOUNTS_CLOSE_TEXT = 0x8217;      // ')'
constexpr std::uint8_t LEGAL_STATUS_OFFENDER = 1;
constexpr std::uint8_t LEGAL_STATUS_FUGITIVE = 0x28;
constexpr std::uint8_t DIGIT_ZERO = 0x30;
constexpr std::uint16_t EQUIPMENT_ROWS = 0x0D;
constexpr std::uint8_t FIRST_EQUIPMENT_MENU_ROW = 2;
constexpr std::uint16_t LASER_MOUNTS = 4;
constexpr std::uint8_t LASER_MOUNT_TYPE_MASK = 3;
constexpr std::uint16_t INVENTORY_CASH_BACK = 0x10; // the cash eight cells nearer its label than the status screen's
constexpr std::uint16_t INVENTORY_QUANTITY_TEXT = 0x8995;
constexpr std::uint16_t FUEL_LABEL_BYTES = 6;        // 'Fuel: ', where a NUL cuts the fuel text
constexpr std::uint16_t FUEL_DIGITS_AFTER_LABEL = 8; // from that NUL past the padding to 'n.n Light Years'
constexpr std::uint16_t RANK_LETTERS = 9;            // 'ARCHANGEL' over 'COMMANDER'
constexpr std::uint8_t SPACE = 0x20;
constexpr std::uint8_t NUL = 0;

constexpr std::uint8_t MISSION_SUPERNOVA = 1;
constexpr std::uint8_t MISSION_MASK_SHIP = 2;
constexpr std::uint8_t STAGE_BRIEFED = 1;
constexpr std::uint8_t STAGE_DEBRIEFED = 2;
constexpr std::uint16_t SUPERNOVA_BRIEFING_LINES = 5;
constexpr std::uint16_t SUPERNOVA_REFUSED_TEXT = 0x8EB1;  // 'We hope you die a horribly long'
constexpr std::uint16_t SUPERNOVA_ACCEPTED_TEXT = 0x8F79; // 'Thank you!'
constexpr std::uint16_t SUPERNOVA_ANSWER_LINES = 8;
constexpr std::uint8_t REFUGEE_TONNES = 0x14;
constexpr std::uint8_t REFUGEE_TONNES_LARGE_BAY = 0x23;
constexpr std::uint16_t SUPERNOVA_DELAY_FRAMES = 0x190;
constexpr std::uint16_t MASK_BRIEFING_LINES = 9;
constexpr std::uint8_t MASK_SHIPS = 5;
constexpr std::uint8_t MASK_SYSTEM_JUMPS = 2;
constexpr std::uint16_t INVASION_BRIEFING_LINES = 0x0C;
constexpr std::uint8_t REWARD_DIGIT_FULL_HOLD = 0x30; // '1000 Credits' when all 20 tonnes came
constexpr std::uint16_t REWARD_FULL_HOLD_TENTHS = 0x2710;
constexpr std::uint8_t REWARD_DIGIT_OTHERWISE = 0x34; // '1400 Credits'
constexpr std::uint16_t REWARD_OTHERWISE_TENTHS = 0x36B0;
constexpr std::uint16_t SUPERNOVA_DEBRIEFING_TEXT = 0x9056; // 'Thank you for saving us.'
constexpr std::uint16_t SUPERNOVA_DEBRIEFING_LINES = 4;
constexpr std::uint16_t MASK_RECOVERED_TEXT = 0x92B4; // 'This masking device looks very'
constexpr std::uint16_t MASK_RECOVERED_LINES = 8;
constexpr std::uint16_t MASK_FLED_TEXT = 0x925D; // 'Running away from the mask ship'
constexpr std::uint16_t MASK_FLED_LINES = 4;
constexpr std::uint16_t MASK_DESTROYED_TEXT = 0x91A8; // 'It was unfortunate that you could'
constexpr std::uint16_t MASK_DESTROYED_LINES = 7;
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

// CALL GetKey; JE _loop, de-assembled: GetKey until a key comes, each empty turn ending at the jump back (ADR-015). The turns
// carry nothing, as WaitForKeyPress's do. Returns the key.
KeyPress WaitForKey(GameState& _state, Hardware& _hardware, std::uint16_t _loop)
{
  for (;;)
  {
    const KeyPress key = GetKey(_state, _hardware);
    if (key.scanCode != 0)
    {
      return key;
    }
    _hardware.LoopTurn(_loop, {});
  }
}

// The cash without its leading spaces: INC SI from the byte before creditBalanceText while it is on a space, each turn of the
// scan jumping back to _loop with SI. Returns the first character that is not a space.
std::uint16_t SkipCashSpaces(const GameState& _state, Hardware& _hardware, std::uint16_t _loop)
{
  auto cash = static_cast<std::uint16_t>(DS.creditBalanceText.offset - 1);
  for (;;)
  {
    cash = Offset(cash, 1);
    if (_state.Byte(cash) != SPACE)
    {
      return cash;
    }
    _hardware.LoopTurn(_loop, {cash});
  }
}

// What FormatQuantity leaves: FormatDecimal5's units, and where BlankLeadingZeros stopped.
struct FormattedQuantity
{
  std::uint16_t units;
  BlankedZeros blanked;
};

// _quantity as five digits in the text at DS:_text, up to four of its leading zeros blanked.
FormattedQuantity FormatQuantity(GameState& _state, std::uint16_t _quantity, std::uint16_t _text)
{
  const std::uint16_t units = FormatDecimal5(_state, _quantity, _text);
  return FormattedQuantity{units, BlankLeadingZeros(_state, _text, static_cast<std::uint8_t>(QUANTITY_DIGITS_BLANKED))};
}

// The offset in the data segment of product _product's entry in cargoHold, the units held and the units on sale: what the
// original holds in cargoRowPointer, where native code holds the product.
[[nodiscard]] std::uint16_t CargoEntry(const GameState& _state, std::uint16_t _product) noexcept
{
  return Offset(_state.OffsetOf(_state.Data().commanderBlock.cargoHold), static_cast<std::uint16_t>(_product * 2));
}

// What both trade screens start with: the frame from the descriptor at DS:_frame and its title, the two lines of help from
// DS:_help, the header in its attribute, the rows' attribute, and the prices; cargoRowPointer on the first product, 0.
// _backward is the direction flag, which DrawDockedFrame goes by.
void DrawTradeScreenHeader(GameState& _state, Hardware& _hardware, std::uint16_t _frame, std::uint16_t _help, bool _backward)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, _frame, _backward), TITLE_CELL);
  const PrintedText help = PrintTextModeString(_state, _help, LINE_3);
  PrintTextModeString(_state, Offset(help.end, 1), LINE_4); // INC SI: the second line, after the first's NUL
  _state.Data().textAttribute = HEADER_ATTRIBUTE;
  PrintTextModeString(_state, TRADE_HEADER_TEXT, LINE_5);
  _state.Data().textAttribute = ROW_ATTRIBUTE;
  ComputeMarketPrices(_state);
  _state.Data().cargoRowPointer = 0;
}

// A trade screen's row: what the original holds in CX, SI, DI and BX, and pushes at the start of each row: the rows left, the
// product's name, the row's place on the text page, and the product's prices in screenPrices.
struct TradeRow
{
  std::uint16_t left;
  std::uint16_t name;
  std::uint16_t cell;
  std::uint16_t prices;
};

constexpr TradeRow FIRST_TRADE_ROW{TRADE_ROWS, DS.productNames.offset, LINE_6, DS.screenPrices.offset};

// A trade row's product name and price (PrintNameAndPrice): the price word _price bytes into the row's prices, four cells after
// the name. Returns where the price's print stops.
PrintedText PrintNameAndPrice(GameState& _state, const TradeRow& _row, std::uint16_t _price)
{
  const PrintedText named = PrintTextModeString(_state, _row.name, _row.cell);
  FormatTenths(_state, _state.Word(Offset(_row.prices, _price)));
  return PrintTextModeString(_state, DS.priceText.offset, Offset(named.nextCell, 4));
}

// After a row's quantity, printed up to _cell (PrintUnit): the unit after the row's product name.
void PrintUnit(GameState& _state, const TradeRow& _row, std::uint16_t _cell)
{
  PrintTextModeString(_state, Offset(_row.name, UNIT_AFTER_NAME), _cell);
}

// The end of a trade row (EndTradeRow): the next prices, then the next row's place and product, and the count.
[[nodiscard]] TradeRow EndTradeRow(const TradeRow& _row) noexcept
{
  return TradeRow{static_cast<std::uint16_t>(_row.left - 1), Offset(_row.name, PRODUCT_NAME_BYTES), Offset(_row.cell, TEXT_ROW_BYTES),
                  Offset(_row.prices, TRADE_PRICES_BYTES)};
}

// RunTitle (CS:7D8B), the title: the ship slots cleared (ClearAllObjects, by _backward, the direction flag), the music on, the
// player's angles, the view and the lasers zeroed, the cockpit shown (screenLayout 2, which ShowCockpitScreen copies over with the
// direction flag clear), the message line cleared and ---- E L I T E ---- on it; then the planet and the turning ship of type
// titleShipType, in stationSlot at its titleShipDistances, until a key: F9 and F10 step the type through 2-29, any other ends it
// with the music off, the credits and a new game. ES is B800h throughout, from its MOV ES,AX. TitleFrameLoop (CS:7DF1) is one
// frame; its turns carry nothing, as the frame loads every register it reads. Returns what it leaves in BP, PresentSpaceView's 20h
// from the credits; the direction flag is clear, from their FinishSpaceViewFrame.
std::uint16_t RunTitle(GameState& _state, Hardware& _hardware, bool _backward)
{
  ClearAllObjects(_state, _backward);
  StartMusic(_state, _hardware);
  _state.Data().commanderBlock.laserMountsFitted = 0;
  _state.Data().playerPitchAngle = 0;
  _state.Data().playerYawAngle = 0;
  _state.Data().playerRollAngle = 0;
  _state.Data().viewAngle = 0;
  _state.Data().anyKeyLatch = 0;
  _state.Data().screenLayout = TITLE_LAYOUT;
  (void)ShowCockpitScreen(_state, _hardware, _backward);
  bool backward = false;
  ClearMessageLine(_state, GameState::VIDEO_SEGMENT, backward);
  _state.Data().textPaperPattern = 0;
  (void)DrawScreenString(_state, DS.eliteTitleText.offset, WHITE_MASK, GameState::VIDEO_SEGMENT, TITLE_TEXT_CELL);
  SpaceObject& ship = _state.ObjectAt(DS.stationSlot.offset);
  _state.Data().shipSlotCount = TITLE_SHIP_SLOTS;
  // XOR AX,AX, then the yaw, the roll, the low words of x and y, the high bytes of x and y as a word, and z's high byte.
  ship.yaw = 0;
  ship.roll = 0;
  ship.x = 0;
  ship.y = 0;
  ship.xHigh = 0; // xHigh and yHigh, written as a word
  ship.yHigh = 0;
  ship.zHigh = 0;
  for (;;)
  {
    _state.Data().drawColor = TITLE_PLANET_COLOR;
    DrawTitlePlanet(_state, TITLE_PLANET_RADIUS, TITLE_PLANET_X, TITLE_PLANET_Y, backward);
    ship.flags = TITLE_SHIP_FLAGS;
    // MOV BL,titleShipType / SHL BL,1 / XOR BH,BH: the type doubled indexes titleShipDistances, and INC BL makes it the type byte,
    // active. The ship stands at the type's own distance, turned a step on each axis: the roll, the yaw, then the pitch.
    const auto doubled = static_cast<std::uint8_t>(_state.Data().titleShipType << 1);
    const std::uint16_t distance = _state.Word(Offset(DS.titleShipDistances.offset, doubled));
    ship.type = static_cast<std::uint8_t>(doubled + 1);
    ship.z = distance;
    ship.roll = Offset(ship.roll, TITLE_ROLL_STEP);
    ship.yaw = Offset(ship.yaw, TITLE_YAW_STEP);
    ship.pitch = Offset(ship.pitch, TITLE_PITCH_STEP);
    _state.Data().textPaperPattern = 0;
    (void)DrawViewString(_state, DS.pressAnyKeyText.offset, PRESS_ANY_KEY_MASK, PRESS_ANY_KEY_PLACE);
    (void)TransformAndDrawObjects(_state, _hardware, backward);
    FinishSpaceViewFrame(_state, _hardware);
    backward = false;
    if (const KeyPress key = GetKey(_state, _hardware); key.scanCode != 0)
    {
      if (key.scanCode == SCAN_F9)
      {
        _state.Data().titleShipType = static_cast<std::uint8_t>(_state.Data().titleShipType - 1);
        if (_state.Data().titleShipType < TITLE_FIRST_SHIP_TYPE)
        {
          _state.Data().titleShipType = TITLE_LAST_SHIP_TYPE;
        }
      }
      else if (key.scanCode == SCAN_F10)
      {
        _state.Data().titleShipType = static_cast<std::uint8_t>(_state.Data().titleShipType + 1);
        if (_state.Data().titleShipType == TITLE_LAST_SHIP_TYPE + 1)
        {
          _state.Data().titleShipType = TITLE_FIRST_SHIP_TYPE;
        }
      }
      else
      {
        break;
      }
    }
    _hardware.LoopTurn(TITLE_FRAME_LOOP, {});
  }
  StopAllSound(_state, _hardware);
  ShowCredits(_state, _hardware, backward);
  StartNewGame(_state, backward);
  _state.Data().titleShown = 1;
  // The credits' FinishSpaceViewFrame leaves BP, which StartNewGame keeps.
  return PRESENT_SPACE_VIEW_BP;
}

// The screen each F-key shows in DockedKeyDispatch (CS:0B45), in the order it tests them, called as the dispatch calls it: with
// the direction flag and BP, which the screens that select the system at the cursor take as the count when no system is on the
// chart. The status screen's arm (CS:0B96) resets the keyboard first. Every screen leaves ES on B800h, the text page or the
// graphics screen, so a chart shown after another finds it there.
struct DockedScreen
{
  std::uint8_t key;
  ScreenKey (*show)(GameState&, Hardware&, bool, std::uint16_t);
  bool clearsDirection; // a chart: PresentChartFrame clears the direction flag, which no other screen changes
};

constexpr std::array<DockedScreen, 9> DOCKED_SCREENS = {{
  {SCAN_F2, [](GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t /*_countIfNone*/)
   { return ShowSellCargoScreen(_state, _hardware, _backward); }, false},
  {SCAN_F3, [](GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t /*_countIfNone*/)
   { return ShowBuyCargoScreen(_state, _hardware, _backward); }, false},
  {SCAN_F4, [](GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t /*_countIfNone*/)
   { return ShowEquipShipScreen(_state, _hardware, _backward); }, false},
  {SCAN_F5, [](GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t /*_countIfNone*/)
   { return ShowGalacticChart(_state, _hardware, _backward, GameState::VIDEO_SEGMENT); }, true},
  {SCAN_F6, [](GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t /*_countIfNone*/)
   { return ShowShortRangeChart(_state, _hardware, _backward, GameState::VIDEO_SEGMENT); }, true},
  {SCAN_F7, &ShowSystemDataScreen, false},
  {SCAN_F8, &ShowMarketPricesScreen, false},
  {SCAN_F9,
   [](GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
   {
     ResetKeyboard(_state, _hardware);
     return ShowCommanderStatusScreen(_state, _hardware, _backward, _countIfNone);
   },
   false},
  {SCAN_F10, &ShowInventoryScreen, false},
}};

// DockedKeyDispatch (CS:0B40), from the arm that shows the screen for _screenKey: F9's status screen (CS:0B96), or Esc's disc menu
// (CS:0BAC). Each screen returns the key that closed it, which picks the next screen, until F1 leaves; Esc is the disc menu, and
// any other key waits for one (CS:0B40). _countIfNone is BP and _backward the direction flag as RunTitleAndDocked leaves them,
// carried from screen to screen as the original's registers carry them. Every jump back to the tests carries the key and BP,
// which the next screen reads; the wait's turns carry nothing, as WaitForKey's do.
DockedExit DispatchDockedKeys(GameState& _state, Hardware& _hardware, std::uint8_t _screenKey, std::uint16_t _countIfNone, bool _backward)
{
  std::uint8_t screenKey = _screenKey;
  std::uint16_t countLeft = _countIfNone;
  bool backward = _backward;
  for (;;)
  {
    ScreenKey key{};
    if (screenKey == SCAN_ESCAPE)
    {
      const DiscMenuExit exit = ShowDiscControlScreen(_state, _hardware, backward);
      if (exit.leaves)
      {
        return DockedExit{true, countLeft, backward};
      }
      key = ScreenKey{exit.scanCode, exit.al, std::nullopt};
    }
    else
    {
      const auto screen = std::ranges::find(DOCKED_SCREENS, screenKey, &DockedScreen::key);
      key = screen->show(_state, _hardware, backward, countLeft);
      backward = backward && !screen->clearsDirection;
    }
    countLeft = key.countLeft.value_or(countLeft);
    _hardware.LoopTurn(DOCKED_KEY_TEST, {Join(key.scanCode, key.al), countLeft});
    for (;;)
    {
      if (key.scanCode == SCAN_F1)
      {
        return DockedExit{false, countLeft, backward};
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
      _hardware.LoopTurn(DOCKED_KEY_DISPATCH, {});
      const KeyPress next = WaitForKey(_state, _hardware, DOCKED_KEY_DISPATCH);
      key = ScreenKey{next.scanCode, AlAfterKey(key.al, next), std::nullopt};
    }
  }
}

} // namespace

PrintedText PrintCreditsOnMessageLine(GameState& _state)
{
  SwapTextAttributeNibbles(_state);
  const PrintedText printed = PrintTextModeString(_state, DS.creditBalanceText.offset, CREDITS_ON_MESSAGE_LINE);
  SwapTextAttributeNibbles(_state);
  return printed;
}

void FormatFuelLightYears(GameState& _state)
{
  // MUL AH by 10, then DIV BL by 36, which cannot overflow: at most 2550/36.
  const auto tenths = static_cast<std::uint16_t>(_state.Data().commanderBlock.fuel * FUEL_TENTHS_PER_UNIT);
  FormatDecimal5(_state, static_cast<std::uint8_t>(tenths / FUEL_UNITS_PER_TENTH), FUEL_DIGITS);
  // The units and the tenths, the last two of the five digits, into the text either side of its point.
  _state.Data().data83F7 = std::uint8_t{_state.Data().data840A};
  _state.Data().data83F9 = std::uint8_t{_state.Data().data840B};
}

std::uint16_t DrawDockedFrame(GameState& _state, Hardware& _hardware, std::uint16_t _descriptor, bool _backward)
{
  if ((_state.Data().screenLayout & TEXT_LAYOUT) == 0)
  {
    _state.Data().screenLayout = TEXT_LAYOUT;
    SetTextMode(_hardware);
  }
  // The screen's attribute, and its background as the border.
  const auto attribute = static_cast<std::uint8_t>(_state.Byte(_descriptor) & ATTRIBUTE_MASK);
  _state.Data().textAttribute = attribute;
  _hardware.SetColorSelect(static_cast<std::uint8_t>(attribute >> 4));
  ClearTextScreen(_state, _backward);

  const std::uint8_t frame = _state.Byte(Offset(_descriptor, 1));
  for (const std::uint16_t row : {FRAME_TOP_ROW, FRAME_TITLE_ROW, FRAME_BOTTOM_ROW})
  {
    DrawFrameRow(_state, GameState::VIDEO_SEGMENT, row, Join(frame, FRAME_ROW_CHARACTER), _backward);
  }
  DrawFrameSides(_state, GameState::VIDEO_SEGMENT, FRAME_SIDES, FRAME_SIDE_ROWS, frame);
  for (const FrameCorner& corner : FRAME_CORNERS)
  {
    const auto cell = static_cast<std::uint16_t>(corner.row * TEXT_ROW_BYTES + corner.column * 2);
    _state.SetVideoWord(cell, Join(frame, corner.character));
  }
  return Offset(_descriptor, 2);
}

std::uint16_t DrawFrameSides(GameState& _state, std::uint16_t _segment, std::uint16_t _cell, std::uint16_t _rows, std::uint8_t _attribute)
{
  const std::uint16_t side = Join(_attribute, FRAME_SIDE_CHARACTER);
  std::uint16_t cell = _cell;
  for (std::uint32_t row = 0; row < LoopCount(_rows); ++row)
  {
    _state.SetFarWord(_segment, cell, side);
    _state.SetFarWord(_segment, Offset(cell, FRAME_RIGHT_SIDE), side);
    cell = Offset(cell, TEXT_ROW_BYTES);
  }
  return cell;
}

std::uint16_t DrawFrameRow(GameState& _state, std::uint16_t _segment, std::uint16_t _cell, std::uint16_t _value, bool _backwards)
{
  const auto step = static_cast<std::uint16_t>(_backwards ? 0xFFFE : 2);
  std::uint16_t cell = _cell;
  for (std::uint16_t left = FRAME_ROW_CELLS; left != 0; --left)
  {
    _state.SetFarWord(_segment, cell, _value);
    cell = Offset(cell, step);
  }
  return cell;
}

std::uint8_t AlAfterKey(std::uint8_t _al, const KeyPress& _key) noexcept
{
  // ROL AX,1 / SHR AH,1 when it takes a code: AL shifted left, Shift in bit 0.
  return _key.taken ? static_cast<std::uint8_t>((_al << 1) | (_key.shift ? 1 : 0)) : _al;
}

ScreenKey WaitForScreenExitKey(GameState& _state, Hardware& _hardware, std::uint8_t _ownKey, std::uint8_t _al, std::uint16_t _countIfNone)
{
  // GetKey until Esc or another screen's F-key, every other turn ending at the jump back to 60B4. The turns carry nothing, as
  // WaitForKeyPress's do: GetKey writes AH before it reads it, and reads AL only when it takes a code, which changes keyBuffer.
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
    _hardware.LoopTurn(WAIT_FOR_SCREEN_EXIT_KEY, {});
  }
}

void AwardArchangelTitle(GameState& _state)
{
  for (std::uint16_t letter = 0; letter < RANK_LETTERS; ++letter)
  {
    _state.Data().commanderBlock.commanderRankText[letter] = std::uint8_t{_state.Data().archangelTitle[letter]};
  }
}

ScreenKey ShowSellCargoScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  DrawTradeScreenHeader(_state, _hardware, DS.sellCargoFrame.offset, SELL_CARGO_HELP_TEXT, _backward);
  TradeRow row = FIRST_TRADE_ROW;
  for (;;)
  {
    const PrintedText price = PrintNameAndPrice(_state, row, SELL_PRICE);
    // The units held, from the cargoHold entry of the product cargoRowPointer numbers, which moves on to the next.
    const std::uint16_t product = _state.Data().cargoRowPointer;
    const std::uint8_t held = _state.Byte(CargoEntry(_state, product));
    _state.Data().cargoRowPointer = static_cast<std::uint16_t>(product + 1);
    if (held != 0)
    {
      FormatQuantity(_state, held, DS.quantityText.offset);
      const PrintedText quantity = PrintTextModeString(_state, DS.quantityText.offset, Offset(price.nextCell, 2));
      PrintUnit(_state, row, quantity.nextCell);
    }
    else
    {
      PrintTextModeString(_state, NO_QUANTITY_TEXT, price.nextCell);
    }
    row = EndTradeRow(row);
    if (row.left == 0)
    {
      break;
    }
    _hardware.LoopTurn(SELL_CARGO_ROW, {row.left, row.name, row.cell, row.prices});
  }
  _state.Data().tradeScreenIsBuy = 0;
  return RunCargoTradeMenu(_state, _hardware);
}

ScreenKey ShowBuyCargoScreen(GameState& _state, Hardware& _hardware, bool _backward)
{
  StartMarketRandom(_state);
  DrawTradeScreenHeader(_state, _hardware, DS.buyCargoFrame.offset, BUY_CARGO_HELP_TEXT, _backward);
  TradeRow row = FIRST_TRADE_ROW;
  for (;;)
  {
    const PrintedText price = PrintNameAndPrice(_state, row, BUY_PRICE);
    std::optional<std::uint8_t> onSale;
    if (_state.Data().commanderBlock.marketQuantitiesSet == 0)
    {
      // The quantity on sale, the first time after an arrival.
      onSale = RollQuantityOnSale(_state);
    }
    else
    {
      const std::uint8_t drawn = _state.Byte(Offset(CargoEntry(_state, _state.Data().cargoRowPointer), 1));
      if (drawn != 0)
      {
        onSale = drawn;
      }
    }
    // The quantity into the market's byte of the product's cargoHold entry, and cargoRowPointer on to the next product.
    if (onSale)
    {
      FormatQuantity(_state, *onSale, DS.quantityText.offset);
      const PrintedText quantity = PrintTextModeString(_state, DS.quantityText.offset, Offset(price.nextCell, 2));
      const std::uint16_t product = _state.Data().cargoRowPointer;
      _state.SetByte(Offset(CargoEntry(_state, product), 1), *onSale);
      _state.Data().cargoRowPointer = static_cast<std::uint16_t>(product + 1);
      PrintUnit(_state, row, quantity.nextCell);
    }
    else
    {
      PrintTextModeString(_state, NO_QUANTITY_TEXT, price.nextCell);
      const std::uint16_t product = _state.Data().cargoRowPointer;
      _state.SetByte(Offset(CargoEntry(_state, product), 1), 0);
      _state.Data().cargoRowPointer = static_cast<std::uint16_t>(product + 1);
      // The jump back into the row's end, with CX the count and BX the next prices.
      _hardware.LoopTurn(BUY_CARGO_ROW_END, {row.left, Offset(row.prices, TRADE_PRICES_BYTES)});
    }
    row = EndTradeRow(row);
    if (row.left == 0)
    {
      break;
    }
    _hardware.LoopTurn(BUY_CARGO_ROW, {row.left, row.name, row.cell, row.prices});
  }
  _state.Data().tradeScreenIsBuy = 1;
  _state.Data().commanderBlock.marketQuantitiesSet = 1;
  return RunCargoTradeMenu(_state, _hardware);
}

ScreenKey ShowCommanderStatusScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
{
  if (_state.Data().inFlight != 1 && _state.Data().commanderBlock.missionNumber != 0)
  {
    if (_state.Data().commanderBlock.missionStage == 0)
    {
      ShowMissionBriefing(_state, _hardware, _backward);
    }
    else if (_state.Data().commanderBlock.missionStage == STAGE_BRIEFED && _state.Data().playerDocked == 1)
    {
      ShowMissionDebriefing(_state, _hardware, _backward);
    }
  }
  const PrintedText title =
    PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.commanderTitle.offset, _backward), TITLE_CELL);
  PrintTextModeString(_state, DS.defaultCommanderName.offset, title.nextCell);
  _state.Data().textAttribute = STATUS_ATTRIBUTE;
  const bool inWitchSpace = _state.Data().commanderBlock.witchspaceCountdown != 0;
  PrintedText printed = PrintTextModeString(_state, SYSTEM_LABEL_TEXT, LINE_3);
  PrintTextModeString(_state, inWitchSpace ? WITCH_SPACE_TEXT : DS.currentSystemName.offset, printed.nextCell);
  printed = PrintTextModeString(_state, HYPERSYSTEM_LABEL_TEXT, LINE_4);
  PrintTextModeString(_state, inWitchSpace ? WITCH_SPACE_TEXT : DS.selectedSystemName.offset, printed.nextCell);
  FormatFuelLightYears(_state);
  PrintTextModeString(_state, FUEL_TEXT, LINE_5);
  printed = PrintTextModeString(_state, CASH_LABEL_TEXT, LINE_6);
  PrintTextModeString(_state, SkipCashSpaces(_state, _hardware, STATUS_CASH_SPACE), printed.nextCell);
  printed = PrintTextModeString(_state, LEGAL_STATUS_LABEL_TEXT, LINE_7);
  const std::uint8_t legal = _state.Data().commanderBlock.legalStatus;
  const std::uint16_t standing = legal < LEGAL_STATUS_OFFENDER ? 0 : legal < LEGAL_STATUS_FUGITIVE ? 1 : 2;
  PrintTextModeString(_state, LEGAL_STATUS_NAMES[standing], printed.nextCell);
  printed = PrintTextModeString(_state, RATING_LABEL_TEXT, LINE_8);
  // The rating: the first threshold above the kill count, each turn of the search carrying the threshold's place and the rating.
  const std::uint8_t kills = _state.Data().commanderBlock.killCount;
  std::uint16_t threshold = static_cast<std::uint16_t>(DS.ratingThresholds.offset - 1);
  std::uint16_t rating = 0xFFFF;
  for (;;)
  {
    threshold = Offset(threshold, 1);
    rating = Offset(rating, 1);
    if (kills < _state.Byte(threshold))
    {
      break;
    }
    _hardware.LoopTurn(STATUS_RATING, {threshold, rating});
  }
  // The last threshold is above any kill count (killCount stops at FEh), so the rating is one of the nine.
  PrintTextModeString(_state, RATING_NAMES[rating], printed.nextCell);
  _state.Data().textAttribute = EQUIPMENT_HEADING_ATTRIBUTE;
  PrintTextModeString(_state, EQUIPMENT_HEADING_TEXT, LINE_9);
  _state.Data().textAttribute = STATUS_ATTRIBUTE;
  // AL keeps the missiles' digit until a row prints, whose NUL then leaves 0 there.
  auto al = static_cast<std::uint8_t>(_state.Data().commanderBlock.missileCount + DIGIT_ZERO);
  _state.Data().data84F5 = al;

  // One row for each item fitted, and after a laser the mounts it is fitted to. Each turn carries the count, the row's place,
  // the item's count and its name's pointer: where the original holds them, the pointer the place of the item's entry in
  // equipmentNames.
  std::uint16_t row = LINE_10;
  std::uint16_t fitted = DS.missileCount.offset;
  std::size_t item = 0;
  _state.Data().menuSelectedRow = FIRST_EQUIPMENT_MENU_ROW;
  for (std::uint16_t left = EQUIPMENT_ROWS;;)
  {
    if (_state.Byte(fitted) != 0)
    {
      // The missiles' name is the buffer their count was written into above; the rest are constant.
      const std::string_view name = item == 0 ? _state.TextAt(_state.Data().gap84EC) : EQUIPMENT_NAMES[item];
      const std::uint16_t named = PrintTextModeString(_state, name, row);
      al = 0;
      if (SelectLaserType(_state))
      {
        std::uint16_t cell = PrintTextModeString(_state, MOUNTS_OPEN_TEXT, named).nextCell;
        // Each mount with this type of laser, from laserMountsFitted in DL and laserMountTypes in DH, which each turn carries
        // with the mount name's pointer, the place of its entry in laserMountNames, the count and where the next name prints.
        std::uint8_t mounts = _state.Data().commanderBlock.laserMountsFitted;
        std::uint8_t types = _state.Data().commanderBlock.laserMountTypes;
        std::size_t mountName = 0;
        for (std::uint16_t mount = LASER_MOUNTS;;)
        {
          const bool on = (mounts & 1) != 0;
          mounts = static_cast<std::uint8_t>(mounts >> 1);
          if (on && (types & LASER_MOUNT_TYPE_MASK) == _state.Data().selectedLaserType)
          {
            cell = PrintTextModeString(_state, LASER_MOUNT_NAMES[mountName], cell);
          }
          ++mountName;
          types = static_cast<std::uint8_t>(types >> 2);
          if (--mount == 0)
          {
            break;
          }
          _hardware.LoopTurn(STATUS_LASER_MOUNT,
                             {Join(types, mounts), _state.OffsetOf(_state.Data().laserMountNames[mountName]), mount, cell});
        }
        PrintTextModeString(_state, MOUNTS_CLOSE_TEXT, static_cast<std::uint16_t>(cell - 2));
      }
      row = Offset(row, TEXT_ROW_BYTES);
    }
    fitted = Offset(fitted, 1);
    ++item;
    _state.Data().menuSelectedRow = static_cast<std::uint8_t>(_state.Data().menuSelectedRow + 1);
    if (--left == 0)
    {
      break;
    }
    _hardware.LoopTurn(STATUS_EQUIPMENT_ROW, {left, row, fitted, _state.OffsetOf(_state.Data().equipmentNames[item])});
  }
  return WaitForScreenExitKey(_state, _hardware, SCAN_F9, al, _countIfNone);
}

ScreenKey ShowInventoryScreen(GameState& _state, Hardware& _hardware, bool _backward, std::uint16_t _countIfNone)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.inventoryFrame.offset, _backward), TITLE_CELL);
  // The fuel text without the padding the status screen has: 'Fuel: ', cut by a NUL that becomes a space again, then
  // 'n.n Light Years'.
  FormatFuelLightYears(_state);
  _state.SetByte(Offset(FUEL_TEXT, FUEL_LABEL_BYTES), NUL);
  const PrintedText label = PrintTextModeString(_state, FUEL_TEXT, LINE_3);
  _state.SetByte(label.end, SPACE);
  PrintTextModeString(_state, Offset(label.end, FUEL_DIGITS_AFTER_LABEL), label.nextCell);
  const PrintedText cash = PrintTextModeString(_state, CASH_LABEL_TEXT, LINE_4);
  const auto cashCell = static_cast<std::uint16_t>(cash.nextCell - INVENTORY_CASH_BACK);
  PrintTextModeString(_state, SkipCashSpaces(_state, _hardware, INVENTORY_CASH_SPACE), cashCell);

  // Every product held, and the refugees: name, quantity, unit, a row each. Each turn carries the count, the name, the amount's
  // place and the row's place, which an empty row does not move on.
  std::uint16_t row = LINE_6;
  std::uint16_t name = DS.productNames.offset;
  std::uint16_t held = DS.cargoHold.offset;
  for (std::uint16_t left = INVENTORY_ROWS;;)
  {
    const std::uint8_t amount = _state.Byte(held);
    if (amount == 0)
    {
      _hardware.LoopTurn(INVENTORY_ROW_END, {left, name, held, row});
    }
    else
    {
      // The name cut at its twelfth character by a NUL, which becomes a space again before the unit after it.
      _state.SetByte(Offset(name, PRODUCT_NAME_END), NUL);
      const PrintedText product = PrintTextModeString(_state, name, row);
      _state.SetByte(product.end, SPACE);
      FormatQuantity(_state, amount, INVENTORY_QUANTITY_TEXT);
      const PrintedText quantity = PrintTextModeString(_state, INVENTORY_QUANTITY_TEXT, Offset(product.nextCell, 4));
      PrintTextModeString(_state, product.end, quantity.nextCell);
      row = Offset(row, TEXT_ROW_BYTES);
    }
    held = Offset(held, 2);
    name = Offset(name, PRODUCT_NAME_BYTES);
    if (--left == 0)
    {
      break;
    }
    _hardware.LoopTurn(INVENTORY_ROW, {left, name, held, row});
  }
  // The last row leaves AL 0: an empty one its amount, a printed one the NUL its last print stops at.
  return WaitForScreenExitKey(_state, _hardware, SCAN_F10, 0, _countIfNone);
}

void ShowMissionBriefing(GameState& _state, Hardware& _hardware, bool _backward)
{
  _state.Data().commanderBlock.missionStage = STAGE_BRIEFED;
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.emergencyFrame.offset, _backward), TITLE_CELL);
  const std::uint8_t mission = _state.Data().commanderBlock.missionNumber;
  if (mission == MISSION_SUPERNOVA)
  {
    PrintTextLines(_state, DS.supernovaBriefingText.offset, LINE_4, SUPERNOVA_BRIEFING_LINES);
    // Y takes the refugees, N refuses; every other key goes back to the GetKey at 6E23, as an empty one does.
    std::uint8_t answer = 0;
    for (;;)
    {
      answer = WaitForKey(_state, _hardware, SUPERNOVA_ANSWER).scanCode;
      if (answer == SCAN_Y || answer == SCAN_N)
      {
        break;
      }
      _hardware.LoopTurn(SUPERNOVA_ANSWER, {});
    }
    std::uint16_t reply = SUPERNOVA_REFUSED_TEXT;
    if (answer == SCAN_Y)
    {
      // The hold emptied for the refugees: MOV [DI],CH, which is 0, at each of the 17 amounts, the LOOP's turns carrying DI
      // and CX.
      std::uint16_t held = DS.cargoHold.offset;
      for (std::uint16_t left = TRADE_ROWS;;)
      {
        _state.SetByte(held, 0);
        held = Offset(held, 2);
        if (--left == 0)
        {
          break;
        }
        _hardware.LoopTurn(EVACUATE_CARGO, {held, left});
      }
      _state.Data().commanderBlock.cargoHold.cargoUsedTonnes = REFUGEE_TONNES;
      if (_state.Data().commanderBlock.largeCargoBayFitted == 1)
      {
        _state.Data().commanderBlock.cargoHold.cargoUsedTonnes = REFUGEE_TONNES_LARGE_BAY;
      }
      _state.Data().commanderBlock.cargoHold.refugeesTonnes = std::uint8_t{_state.Data().commanderBlock.cargoHold.cargoUsedTonnes};
      reply = SUPERNOVA_ACCEPTED_TEXT;
    }
    PrintTextLines(_state, reply, LINE_10, SUPERNOVA_ANSWER_LINES);
    (void)WaitForKeyPress(_state, _hardware);
    _state.Data().commanderBlock.supernovaFrames = SUPERNOVA_DELAY_FRAMES;
    _state.Data().commanderBlock.jumpedSinceBriefing = 0;
    return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    PrintTextLines(_state, DS.maskBriefingText.offset, LINE_4, MASK_BRIEFING_LINES);
    (void)WaitForKeyPress(_state, _hardware);
    _state.Data().commanderBlock.maskMissionShipsLeft = MASK_SHIPS;
    _state.Data().commanderBlock.maskSystemJumps = MASK_SYSTEM_JUMPS;
    return;
  }
  PrintTextLines(_state, DS.invasionBriefingText.offset, LINE_4, INVASION_BRIEFING_LINES);
  (void)WaitForKeyPress(_state, _hardware);
  _state.Data().commanderBlock.thargoidInvasionActive = 1;
  _state.Data().commanderBlock.jumpedSinceBriefing = 0;
}

void ShowMissionDebriefing(GameState& _state, Hardware& _hardware, bool _backward)
{
  // Not yet: the mask ship is still about, or the supernova's refugees have not been taken away.
  if (_state.Data().commanderBlock.missionNumber == MISSION_MASK_SHIP
        ? _state.Data().commanderBlock.maskShipDestroyed != 1
        : _state.Data().commanderBlock.missionNumber == MISSION_SUPERNOVA && _state.Data().commanderBlock.jumpedSinceBriefing != 1)
  {
    return;
  }
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, DS.taskCompleteFrame.offset, _backward), TITLE_CELL);
  _state.Data().commanderBlock.missionStage = STAGE_DEBRIEFED;
  const std::uint8_t mission = _state.Data().commanderBlock.missionNumber;
  if (mission == MISSION_SUPERNOVA)
  {
    const bool fullHold = _state.Data().commanderBlock.cargoHold.refugeesTonnes == REFUGEE_TONNES;
    _state.Data().refugeeRewardDigit = fullHold ? REWARD_DIGIT_FULL_HOLD : REWARD_DIGIT_OTHERWISE;
    _state.Data().commanderBlock.cargoHold.refugeesTonnes = 0;
    _state.Data().commanderBlock.cargoHold.cargoUsedTonnes = 0;
    AddCredits(_state, fullHold ? REWARD_FULL_HOLD_TENTHS : REWARD_OTHERWISE_TENTHS);
    PrintTextLines(_state, SUPERNOVA_DEBRIEFING_TEXT, LINE_4, SUPERNOVA_DEBRIEFING_LINES);
    (void)WaitForKeyPress(_state, _hardware);
    _state.Data().commanderBlock.missionNumber = 0;
    _state.Data().commanderBlock.missionStage = 0;
    return;
  }
  if (mission == MISSION_MASK_SHIP)
  {
    std::uint16_t text = MASK_DESTROYED_TEXT;
    std::uint16_t lines = MASK_DESTROYED_LINES;
    if (_state.Data().commanderBlock.maskingDeviceRecovered == 1)
    {
      _state.Data().commanderBlock.maskingDeviceFitted = 1;
      text = MASK_RECOVERED_TEXT;
      lines = MASK_RECOVERED_LINES;
      _hardware.LoopTurn(MASK_DEBRIEFING_TEXT, {}); // the JMP back to the print the other two run on into
    }
    else if (_state.Data().commanderBlock.fledMaskShip == 1)
    {
      text = MASK_FLED_TEXT;
      lines = MASK_FLED_LINES;
    }
    PrintTextLines(_state, text, LINE_4, lines);
    (void)WaitForKeyPress(_state, _hardware);
    _state.Data().commanderBlock.maskMissionShipsLeft = 0;
    _state.Data().commanderBlock.maskShipDestroyed = 0;
    _state.Data().commanderBlock.missionNumber = 0;
    _state.Data().commanderBlock.maskingDeviceRecovered = 0;
    _state.Data().commanderBlock.maskSystemJumps = 0;
    _state.Data().commanderBlock.fledMaskShip = 0;
    _state.Data().commanderBlock.missionStage = 0;
    return;
  }
  PrintTextLines(_state, DS.invasionDebriefText.offset, LINE_4, INVASION_DEBRIEFING_LINES);
  (void)WaitForKeyPress(_state, _hardware);
  AwardArchangelTitle(_state);
  _state.Data().commanderBlock.antiEcmEmulatorFitted = 1;
  _state.Data().commanderBlock.missionNumber = 0;
  _state.Data().commanderBlock.missionStage = 0;
  _state.Data().commanderBlock.invadedStationDestroyed = 0;
  _state.Data().commanderBlock.thargoidInvasionActive = 0;
  _state.Data().commanderBlock.missionStage = 0;
}

DockedExit RunTitleAndDocked(GameState& _state, Hardware& _hardware, std::uint16_t _countIfNone, bool _backward)
{
  std::uint16_t countIfNone = _countIfNone;
  bool backward = _backward;
  if (_state.Data().titleShown != 1)
  {
    countIfNone = RunTitle(_state, _hardware, _backward);
    backward = false;
  }
  // JMP into DockedKeyDispatch: back from a disk request, at the disc menu (CS:0BAC); otherwise at the status screen (CS:0B96). The
  // jumps back carry BP, which the status screen reads, as the dispatch's own turns do.
  if (_state.Data().resumeAtDiskMenu == 1)
  {
    _hardware.LoopTurn(DOCKED_DISK_MENU_ENTRY, {countIfNone});
    return DispatchDockedKeys(_state, _hardware, SCAN_ESCAPE, countIfNone, backward);
  }
  _hardware.LoopTurn(DOCKED_STATUS_ENTRY, {countIfNone});
  return DispatchDockedKeys(_state, _hardware, SCAN_F9, countIfNone, backward);
}

} // namespace Elite
