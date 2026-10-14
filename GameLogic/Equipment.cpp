#include "pch.h"

#include "Equipment.h"

#include "Arithmetic.h"
#include "Docked.h"
#include "EquipmentTables.h"
#include "Flight.h"
#include "Market.h"
#include "Maths.h"
#include "Ships.h"
#include "Text.h"
#include "Timer.h"

#include <span>
#include <string_view>

namespace Elite
{

namespace
{

// Scan codes the menus read.
constexpr std::uint8_t SCAN_ESCAPE = 0x01;
constexpr std::uint8_t SCAN_S = 0x1F;
constexpr std::uint8_t SCAN_B = 0x30;
constexpr std::uint8_t SCAN_SPACE = 0x39;
constexpr std::uint8_t SCAN_F1 = 0x3B;
constexpr std::uint8_t SCAN_F4 = 0x3E;
constexpr std::uint8_t SCAN_PAST_F10 = 0x45;
constexpr std::uint8_t SCAN_UP = 0x48;
constexpr std::uint8_t SCAN_LEFT = 0x4B;
constexpr std::uint8_t SCAN_RIGHT = 0x4D;
constexpr std::uint8_t SCAN_DOWN = 0x50;

// The text page: offsets in B800, 80 bytes a row.
constexpr std::uint16_t ROW_BYTES = 0x50;
constexpr std::uint16_t TITLE_OFFSET = 0x54;
constexpr std::uint16_t MESSAGE_OFFSET = 0x78;
constexpr std::uint16_t HELP_TEXT_OFFSET = 0xF4;
constexpr std::uint16_t MOUNT_HEADER_OFFSET = 0x144;
constexpr std::uint16_t SELL_LASER_OFFSET = 0x174;
constexpr std::uint16_t MOUNT_NAMES_OFFSET = 0x284;
constexpr std::uint16_t FIRST_ITEM_OFFSET = 0x326; // the first item's name
constexpr std::uint16_t EQUIP_MENU_FIRST_ROW = 10; // on the text page
constexpr std::uint16_t PRICE_COLUMN = 0x2A;
constexpr std::uint16_t RESALE_COLUMN = 0x38;
constexpr std::uint16_t SELL_PRICE_COLUMN = 0x39;
constexpr std::uint16_t MENU_ROW_ATTRIBUTE = 5; // a menu row's first byte the highlight changes: its third character's attribute

constexpr std::uint8_t SELL_PRICE_ATTRIBUTE = 0x79;
constexpr std::uint8_t MENU_ATTRIBUTE = 0x1E;
constexpr std::uint8_t HELP_ATTRIBUTE = 0x1F;
constexpr std::uint8_t MOUNT_HIGHLIGHT_ATTRIBUTE = 0x70;

// The equipment menu's messages, 19 characters each, and the mount menu's texts.

constexpr std::uint8_t RESALE_DIGITS_BLANKED = 3; // and those the equipment screen blanks in a resale price
constexpr std::size_t TENTHS_DIGIT = 4;           // priceText's tenths digit, which moves right for the point

// The equipment menu's rows, from 1 (EQUIPMENT_LIST_ROWS, Equipment.h). Row r's fitted count is the commander's equipment item
// r - 1 (EQUIPMENT_ITEMS): fuel's is row 1.
constexpr std::uint8_t FIRST_FITTED_ROW = 3;
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24;
constexpr std::array<std::uint8_t, 4> LASER_ROWS = {0x05, 0x06, 0x0D, 0x0E}; // pulse, beam, mining, military

constexpr std::uint8_t MISSILE_LIMIT = 4;
constexpr std::uint8_t FUEL_NEARLY_FULL = 0xFD;
constexpr std::uint8_t NO_FUEL_MISSION = 1;
constexpr std::uint8_t CARGO_BAY_SALE_TONNES = 0x15; // the extension sells only with less than this aboard
constexpr std::uint8_t ALL_MOUNTS = 0x0F;

constexpr std::uint16_t TICKS_PER_POLL = 100;

// The laser mount menu: four boxes of 8x3 cells, 12h bytes apart.
constexpr std::uint16_t MOUNT_COUNT = 4;
constexpr std::uint16_t FIRST_MOUNT_BOX = 0x1E5;
constexpr std::uint16_t LAST_MOUNT_BOX = 0x21B;
constexpr std::uint16_t MOUNT_BOX_STEP = 0x12;
constexpr std::uint8_t LAST_MOUNT = 3;
constexpr std::uint8_t MOUNT_TYPE_BITS = 3; // a mount's two bits of laserMountTypes
constexpr std::uint8_t MOUNT_BOX_ROWS = 3;
constexpr std::uint16_t MOUNT_BOX_COLUMNS = 8;
constexpr std::uint16_t MOUNT_BOX_ROW_SKIP = 0x40;

// LaunchEscapePod.
constexpr std::uint8_t ESCAPE_POD_FRAMES = 0x50;
constexpr std::uint16_t HALF_TURN = 0x400;
constexpr std::uint16_t ESCAPE_POD_SPEED = 0x14;
constexpr std::uint16_t ESCAPE_POD_MOVES = 12;
constexpr std::uint16_t CARGO_KINDS = 0x11;

// TryScoopObject: the scoop's box in view coordinates, and what is scooped.
constexpr std::uint16_t SCOOP_LOWEST = 0x1E;
constexpr std::uint16_t SCOOP_HEIGHT = 0xC8;
constexpr std::uint16_t SCOOP_HALF_WIDTH = 0x96;
constexpr std::uint8_t TYPE_CARGO_BARREL = 0x11;
constexpr std::uint8_t TYPE_ESCAPE_POD = 0x15;
constexpr std::uint8_t FLAG_MASKING_DEVICE = 0x40;
constexpr std::uint8_t FLAG_PRECIOUS = 0x10;
constexpr std::uint16_t SCOOP_MESSAGE_FRAMES = 0x14;
constexpr std::uint8_t RANDOM_PRODUCT_DIVISOR = 0x18; // 0-255 into products 0-10
constexpr std::uint8_t PRODUCT_SLAVES = 3;
constexpr std::uint8_t PRODUCT_FURS = 0x0B;
constexpr std::uint16_t PRODUCT_NAME_BYTES = 0x11;
constexpr std::uint16_t SCOOPED_NAME_BYTES = 0x0D;
constexpr std::uint8_t MINERALS_FROM = 0x28;

// screenPrices' row _row: its price, and its resale price.
[[nodiscard]] MenusState::ScreenPrice& RowPrices(GameState& _state, std::uint8_t _row)
{
  return ElementWithin(std::span(_state.Data().menus.screenPrices), _row);
}

// Row _row's fitted count in the commander's block, from 1, fuel's.
[[nodiscard]] std::uint8_t& Fitted(GameState& _state, std::uint8_t _row)
{
  return _state.Data().commander.*ElementWithin(std::span(EQUIPMENT_ITEMS), std::size_t{_row} - 1);
}

// The text in _table, a table of the reference's bytes, from its byte _at up to its NUL.
[[nodiscard]] std::string_view TextInTable(std::span<const std::uint8_t> _table, std::size_t _at) noexcept
{
  const std::string_view table(reinterpret_cast<const char*>(_table.data()), _table.size());
  return table.substr(_at, table.find('\0', _at) - _at);
}

// IMUL r/m8: AL times _factor, both signed, into AX.
[[nodiscard]] std::uint16_t SignedProduct(std::uint8_t _al, std::uint8_t _factor) noexcept
{
  return static_cast<std::uint16_t>(static_cast<std::int8_t>(_al) * static_cast<std::int8_t>(_factor));
}

// AND r16,r16 / JNS / NEG r16.
[[nodiscard]] std::uint16_t Magnitude(std::uint16_t _value) noexcept
{
  return (_value & 0x8000) != 0 ? Negate(_value) : _value;
}

// INC BYTE PTR [_byte].
void IncrementByte(std::uint8_t& _byte)
{
  _byte = static_cast<std::uint8_t>(_byte + 1);
}

// ADD [_field],_amount, held at 250 (FAh) from 251 up: gems, gold and platinum.
void AddPrecious(std::uint8_t& _field, std::uint8_t _amount)
{
  _field = static_cast<std::uint8_t>(_field + _amount);
  if (_field > PRECIOUS_HELD_MOST)
  {
    _field = PRECIOUS_HELD_MOST;
  }
}

// TryScoopObject's box test (4414-443F): whether _view is in the scoop's box: y from 30 to 229, then x and z within 149 either
// side.
[[nodiscard]] bool TestScoopBox(const Vector& _view) noexcept
{
  const auto y = static_cast<std::uint16_t>(_view.y);
  if ((y & 0x8000) != 0 || y < SCOOP_LOWEST || static_cast<std::uint16_t>(y - SCOOP_LOWEST) >= SCOOP_HEIGHT)
  {
    return false;
  }
  return Magnitude(static_cast<std::uint16_t>(_view.x)) < SCOOP_HALF_WIDTH &&
         Magnitude(static_cast<std::uint16_t>(_view.z)) < SCOOP_HALF_WIDTH;
}

// PrintEquipmentSellColumn (0x6949): _text in the resale column of menuSelectedRow's row, in the resale price's attribute, and
// the menu's attribute put back.
void PrintEquipmentSellColumn(GameState& _state, std::string_view _text)
{
  _state.Data().drawing.textAttribute = SELL_PRICE_ATTRIBUTE;
  // (row-1)*80 bytes, as (row-1)*256/4 and that /4 again.
  const auto quarter = static_cast<std::uint16_t>((static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow - 1) << 8) >> 2);
  const auto cell = static_cast<std::uint16_t>(MenuFirstRowCell(_state) + quarter + (quarter >> 2) + SELL_PRICE_COLUMN);
  PrintTextModeString(_state, _text, cell);
  _state.Data().drawing.textAttribute = MENU_ATTRIBUTE;
}

// The laser mounts, as the commander holds them.
[[nodiscard]] LaserMounts CurrentMounts(const GameState& _state)
{
  return LaserMounts{.fitted = _state.Data().commander.laserMountsFitted, .types = _state.Data().commander.laserMountTypes};
}

// What the shipyard's rules read of the commander: the counts fitted, the mounts, the tonnes aboard and the mission.
[[nodiscard]] Fitting CurrentFitting(GameState& _state)
{
  Fitting fitting{.counts = {},
                  .laserMountsFitted = _state.Data().commander.laserMountsFitted,
                  .cargoUsedTonnes = _state.Data().commander.cargoHold.cargoUsedTonnes,
                  .missionNumber = _state.Data().commander.missionNumber};
  for (std::uint8_t row = 1; row <= EQUIPMENT_LIST_ROWS; ++row)
  {
    fitting.counts[std::size_t{row} - 1] = Fitted(_state, row);
  }
  return fitting;
}

// The equipment menu's message for _refusal.
[[nodiscard]] std::string_view RefusalText(EquipmentRefusal _refusal) noexcept
{
  switch (_refusal)
  {
  case EquipmentRefusal::None:
    break;
  case EquipmentRefusal::NoFuelAvailable:
    return NO_FUEL_AVAILABLE_TEXT;
  case EquipmentRefusal::FuelFull:
    return FUEL_FULL_TEXT;
  case EquipmentRefusal::FourMissilesOnly:
    return FOUR_MISSILES_ONLY_TEXT;
  case EquipmentRefusal::AlreadyFitted:
    return ALREADY_FITTED_TEXT;
  case EquipmentRefusal::FourLasers:
    return FOUR_LASERS_TEXT;
  case EquipmentRefusal::ScoopNeeded:
    return SCOOP_NEEDED_TEXT;
  case EquipmentRefusal::NotEnoughCredits:
    return NOT_ENOUGH_CREDITS_TEXT;
  case EquipmentRefusal::FuelSaleIllegal:
    return FUEL_SALE_ILLEGAL_TEXT;
  case EquipmentRefusal::NoMissileToSell:
    return NO_MISSILE_TO_SELL_TEXT;
  case EquipmentRefusal::TooMuchCargo:
    return TOO_MUCH_CARGO_TEXT;
  case EquipmentRefusal::NoItemToSell:
    return NO_ITEM_TO_SELL_TEXT;
  case EquipmentRefusal::SellMiningLaser:
    return SELL_MINING_LASER_TEXT;
  }
  return {};
}

// A quarter of _rows rows of the text page, in bytes, as the menus measure it: _rows*256, shifted right twice.
[[nodiscard]] std::uint16_t MenuRowsQuarter(std::uint8_t _rows) noexcept
{
  return static_cast<std::uint16_t>(Join(_rows, 0) >> 2);
}

// 61CF and 628A: B and S push CX, SI and textAttribute for 6214 to pop after the message, and set the message's attribute.
// Returns the attribute, which the original pushes in AL.
[[nodiscard]] std::uint8_t OpenEquipMessage(GameState& _state)
{
  const std::uint8_t saved = _state.Data().drawing.textAttribute;
  _state.Data().drawing.textAttribute = MENU_ATTRIBUTE;
  return saved;
}

// 6214: the message _text on the message line, and textAttribute put back to _saved, which the original pops into AL before its
// jump back to the steering.
void ShowEquipMessage(GameState& _state, std::string_view _text, std::uint8_t _saved)
{
  PrintTextModeString(_state, _text, MESSAGE_OFFSET);
  _state.Data().drawing.textAttribute = _saved;
}

// 61DA: B on the equipment menu, once OpenEquipMessage has saved the attribute: the row refused (RefuseToFit) or paid for, and
// counted (CountAfterFitting), its resale price shown but for fuel's, and a laser's mount chosen. From the cargo bay on, the row's
// laser type is selected first, as the original selects it before its checks. Returns the message for ShowEquipMessage, after
// the jumps back the original takes on the way.
[[nodiscard]] std::string_view BuyEquipment(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t row = _state.Data().menus.menuSelectedRow;
  const bool laser = row >= FIRST_FITTED_ROW && SelectLaserType(_state);
  const EquipmentRefusal refusal = RefuseToFit(row, CurrentFitting(_state));
  if (refusal != EquipmentRefusal::None)
  {
    return RefusalText(refusal);
  }
  if (!PayForEquipmentItem(_state))
  {
    return RefusalText(EquipmentRefusal::NotEnoughCredits);
  }
  std::uint8_t& fitted = Fitted(_state, row);
  fitted = CountAfterFitting(row, fitted);
  if (row == FUEL_ROW)
  {
    return FUEL_PURCHASED_TEXT;
  }
  ShowEquipmentSellPrice(_state);
  if (row == MISSILE_ROW)
  {
    return MISSILE_PURCHASED_TEXT;
  }
  if (laser)
  {
    ChooseMountToFitLaser(_state, _hardware);
  }
  return ITEM_PURCHASED_TEXT;
}

// 6295: S on the equipment menu, once OpenEquipMessage has saved the attribute: the row refused (RefuseToRemove) or taken back
// for its resale price, its resale column cleared once the last is sold, and a laser's mount chosen. Returns the message for
// ShowEquipMessage, after the jumps back the original takes on the way.
[[nodiscard]] std::string_view SellEquipment(GameState& _state, Hardware& _hardware)
{
  const std::uint8_t row = _state.Data().menus.menuSelectedRow;
  const EquipmentRefusal refusal = RefuseToRemove(row, CurrentFitting(_state));
  if (refusal != EquipmentRefusal::None)
  {
    return RefusalText(refusal);
  }
  std::uint8_t& fitted = Fitted(_state, row);
  const auto left = static_cast<std::uint8_t>(fitted - 1);
  fitted = left;
  if (left == 0)
  {
    ClearEquipmentSellPrice(_state);
  }
  AddCredits(_state, RowPrices(_state, row).resale);
  if (row == MISSILE_ROW)
  {
    return MISSILE_SOLD_TEXT;
  }
  if (SelectLaserType(_state))
  {
    ChooseMountToRemoveLaser(_state, _hardware);
  }
  return ITEM_SOLD_TEXT;
}

// 63C3 and 6490: the mount box at _box painted in textAttribute, then the one left of it, from FORE round to LEFT,
// highlighted. Returns that box.
std::uint16_t MoveMountBoxLeft(GameState& _state, std::uint16_t _box)
{
  PaintLaserMountBox(_state, _box);
  auto box = static_cast<std::uint16_t>(_box - MOUNT_BOX_STEP);
  const auto mount = static_cast<std::uint8_t>(_state.Data().equipment.selectedLaserMount - 1);
  _state.Data().equipment.selectedLaserMount = mount;
  if ((mount & 0x80) != 0)
  {
    _state.Data().equipment.selectedLaserMount = LAST_MOUNT;
    box = LAST_MOUNT_BOX;
  }
  _state.Data().drawing.textAttribute = MOUNT_HIGHLIGHT_ATTRIBUTE;
  PaintLaserMountBox(_state, box);
  return box;
}

// 63E1 and 64AE: the mount box at _box painted in textAttribute, then the one right of it, from LEFT round to FORE,
// highlighted. Returns that box.
std::uint16_t MoveMountBoxRight(GameState& _state, std::uint16_t _box)
{
  PaintLaserMountBox(_state, _box);
  auto box = static_cast<std::uint16_t>(_box + MOUNT_BOX_STEP);
  const auto mount = static_cast<std::uint8_t>(_state.Data().equipment.selectedLaserMount + 1);
  _state.Data().equipment.selectedLaserMount = mount;
  if (mount == MOUNT_COUNT)
  {
    _state.Data().equipment.selectedLaserMount = 0;
    box = FIRST_MOUNT_BOX;
  }
  _state.Data().drawing.textAttribute = MOUNT_HIGHLIGHT_ATTRIBUTE;
  PaintLaserMountBox(_state, box);
  return box;
}

// 6438 and 6505: the mount message _text on the message line, in the menu's attribute; the second reached from 653D by the
// jump back to 6509, the tail the two messages share. The PUSH SI / POP SI round it keep the box, which the routine never
// touches.
void PrintMountMessage(GameState& _state, std::string_view _text)
{
  _state.Data().drawing.textAttribute = MENU_ATTRIBUTE;
  PrintTextModeString(_state, _text, MESSAGE_OFFSET);
}

// 642A: Space in ChooseMountToFitLaser. A free mount gets the bought laser (WithLaserFitted), and the help text is redrawn over
// the mount menu. Returns whether it did; otherwise it says the mount is occupied.
bool FitLaserOnMount(GameState& _state)
{
  const LaserMounts mounts = CurrentMounts(_state);
  const std::uint8_t mount = _state.Data().equipment.selectedLaserMount;
  if (RefuseToFitMount(mounts, mount) != MountRefusal::None)
  {
    PrintMountMessage(_state, MOUNT_OCCUPIED_TEXT);
    return false;
  }
  const LaserMounts fitted = WithLaserFitted(mounts, mount, _state.Data().equipment.selectedLaserType);
  _state.Data().commander.laserMountsFitted = fitted.fitted;
  _state.Data().commander.laserMountTypes = fitted.types;
  RedrawEquipHelpText(_state);
  return true;
}

// 64F7: Space in ChooseMountToRemoveLaser. A mount holding the sold laser's type is freed (WithLaserCleared), and the help
// text redrawn. Returns whether it was; otherwise it says why. "Wrong laser type!" jumps back into the message code the other
// message runs on into (653D to 6509).
bool RemoveLaserFromMount(GameState& _state)
{
  const LaserMounts mounts = CurrentMounts(_state);
  const std::uint8_t mount = _state.Data().equipment.selectedLaserMount;
  const MountRefusal refusal = RefuseToClearMount(mounts, mount, _state.Data().equipment.selectedLaserType);
  if (refusal != MountRefusal::None)
  {
    PrintMountMessage(_state, refusal == MountRefusal::NoLaser ? NO_LASER_ON_MOUNT_TEXT : WRONG_LASER_TYPE_TEXT);
    return false;
  }
  _state.Data().commander.laserMountsFitted = WithLaserCleared(mounts, mount).fitted;
  RedrawEquipHelpText(_state);
  return true;
}

// Space's work on the selected mount, for one of the two mount choosers: true once it is done.
using MountSpace = bool (*)(GameState&);

// 63B5-6448 and 6482-6515, one loop at two addresses: the box moved by the steering's roll or the arrow keys until Space does
// its work (_space), from the box at _start.
void RunMountChooser(GameState& _state, Hardware& _hardware, MountSpace _space, std::uint16_t _start)
{
  std::uint16_t box = _start;
  for (;;)
  {
    const std::uint8_t roll = ReadSteering(_state, _hardware).roll;
    if (roll != 0)
    {
      _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
      box = (roll & 0x80) != 0 ? MoveMountBoxLeft(_state, box) : MoveMountBoxRight(_state, box);
    }
    for (;;)
    {
      const KeyPress key = PollMenuKey(_state, _hardware);
      const std::uint8_t code = key.scanCode;
      if (code == 0)
      {
        break;
      }
      // PUSH AX and POP AX round it.
      ClearDockedMessageLine(_state);
      if (code == SCAN_SPACE)
      {
        if (_space(_state))
        {
          return;
        }
        break;
      }
      _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
      if (code == SCAN_LEFT || code == SCAN_RIGHT)
      {
        box = code == SCAN_LEFT ? MoveMountBoxLeft(_state, box) : MoveMountBoxRight(_state, box);
        continue;
      }
      break;
    }
  }
}

} // namespace

void LaunchEscapePod(GameState& _state)
{
  _state.Data().jump.hyperspaceCountdown = 0;
  _state.Data().playerShip.escapePodFrames = ESCAPE_POD_FRAMES;
  const SlotSearch free = FindFreeShipSlot(_state);
  SpaceObject& pod = _state.Slot(free.found ? free.slot : ReclaimShipSlot(_state).slot);
  ClearObjectSlot(pod);
  InitAbandonedCobra(pod);
  _state.Data().playerShip.playerPitchAngle = static_cast<std::uint16_t>(_state.Data().playerShip.playerPitchAngle + HALF_TURN);
  _state.Data().playerShip.viewAngle = HALF_TURN;
  _state.Data().playerShip.viewLocked = 1;
  _state.Data().playerShip.playerSpeed = ESCAPE_POD_SPEED;
  _state.Data().playerShip.velocityDirty = 1;
  _state.Data().commander.escapePodFitted = 0;
  UpdatePlayerVelocity(_state);
  for (std::uint16_t move = 0; move < ESCAPE_POD_MOVES; ++move)
  {
    MoveObjectsByVelocity(_state);
  }
  // MOV [DI],CH, which is 0 throughout: the 17 amounts held emptied.
  for (CommanderState::CargoHold::Lot& lot : _state.Data().commander.cargoHold.lots)
  {
    lot.held = 0;
  }
}

void TryScoopObject(GameState& _state, SpaceObject& _slot, const Vector& _view)
{
  std::uint8_t space = CargoBayTonnes(_state.Data().commander.largeCargoBayFitted == 1);
  space = static_cast<std::uint8_t>(space - _state.Data().commander.cargoHold.cargoUsedTonnes);
  _state.Data().equipment.freeCargoTonnes = space;
  if (!TestScoopBox(_view))
  {
    return;
  }

  const auto type = static_cast<std::uint8_t>((_slot.type >> 1) & TYPE_MASK);
  const std::uint8_t flags = _slot.flags;
  const bool full = _state.Data().equipment.freeCargoTonnes == 0;
  Message message = Message::ScoopRetrievalInactive;
  if (type == TYPE_CARGO_BARREL)
  {
    if ((flags & FLAG_MASKING_DEVICE) != 0)
    {
      (void)RemoveObject(_state, _slot);
      _state.Data().commander.maskingDeviceRecovered = 1;
      message = Message::MaskingDevice;
    }
    else if (full)
    {
      message = Message::CargoBayFull;
    }
    else
    {
      // A random product 0-10, furs for slaves, into the hold, and its name for the message. The divide cannot overflow.
      (void)RemoveObject(_state, _slot);
      const std::uint8_t random = Low(NextRandom(_state));
      std::uint8_t product = DivideByte(random, RANDOM_PRODUCT_DIVISOR).quotient;
      if (product == PRODUCT_SLAVES)
      {
        product = PRODUCT_FURS;
      }
      IncrementByte(_state.Data().commander.cargoHold.lots[product].held);
      _state.Data().commander.cargoHold.cargoUsedTonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + 1);
      // The product's name, its first 13 bytes, over the message's.
      const std::span<const std::uint8_t> name = std::span<const std::uint8_t>(_state.Data().docked.productNames)
                                                   .subspan(std::size_t{product} * PRODUCT_NAME_BYTES, SCOOPED_NAME_BYTES);
      const std::span<std::uint8_t> scooped = BytesWithin(std::span(_state.Data().equipment.scoopedCargoText), 0, SCOOPED_NAME_BYTES);
      for (std::size_t letter = 0; letter < SCOOPED_NAME_BYTES; ++letter)
      {
        scooped[letter] = std::uint8_t{name[letter]};
      }
      message = Message::ScoopedCargo;
    }
  }
  else if (type == TYPE_SPLINTER && (flags & FLAG_PRECIOUS) != 0)
  {
    // Gems, gold and platinum always; minerals or alloys too if there is room. Gold gets the gems' AL, as the original adds it.
    (void)RemoveObject(_state, _slot);
    const std::uint8_t gems = Low(NextRandom(_state)) & 7;
    AddPrecious(_state.Data().commander.cargoHold.lots[COMMODITY_GEM_STONES].held, gems);
    AddPrecious(_state.Data().commander.cargoHold.lots[COMMODITY_GOLD].held, gems);
    AddPrecious(_state.Data().commander.cargoHold.lots[COMMODITY_PLATINUM].held,
                static_cast<std::uint8_t>((High(NextRandom(_state)) & 3) + 1));
    if (_state.Data().equipment.freeCargoTonnes != 0)
    {
      CommanderState::CargoHold& hold = _state.Data().commander.cargoHold;
      IncrementByte(Low(NextRandom(_state)) >= MINERALS_FROM ? hold.lots[COMMODITY_MINERALS].held : hold.lots[COMMODITY_ALLOYS].held);
      _state.Data().commander.cargoHold.cargoUsedTonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + 1);
    }
    message = Message::PreciousMetals;
  }
  else if (type == TYPE_SPLINTER || type == TYPE_ESCAPE_POD || type == TYPE_THARGON)
  {
    if (full)
    {
      message = Message::CargoBayFull;
    }
    else
    {
      (void)RemoveObject(_state, _slot);
      CommanderState::CargoHold& hold = _state.Data().commander.cargoHold;
      std::uint8_t& held = type == TYPE_SPLINTER     ? hold.lots[COMMODITY_ALLOYS].held
                           : type == TYPE_ESCAPE_POD ? hold.lots[COMMODITY_SLAVES].held
                                                     : hold.lots[COMMODITY_ALIEN_ITEMS].held;
      held = static_cast<std::uint8_t>(held + 1);
      _state.Data().commander.cargoHold.cargoUsedTonnes = static_cast<std::uint8_t>(_state.Data().commander.cargoHold.cargoUsedTonnes + 1);
      message = type == TYPE_SPLINTER ? Message::MetalAlloys : type == TYPE_ESCAPE_POD ? Message::EscapePodRetrieved : Message::AlienItems;
    }
  }
  _state.Data().messages.messagePointer = message;
  _state.Data().messages.messageFrames = SCOOP_MESSAGE_FRAMES;
}

ScreenKey ShowEquipShipScreen(GameState& _state, Hardware& _hardware)
{
  PrintTextModeString(_state, DrawDockedFrame(_state, _hardware, EQUIP_SHIP_FRAME), TITLE_OFFSET);
  PrintCountedTextLines(_state, EQUIP_HELP_TEXT, HELP_TEXT_OFFSET);

  // The rows on sale at the system's tech level (EquipmentRowsOnSale), each its name and its price (EquipmentPriceTenths), and a
  // resale price for an item fitted. menuRowCount counts them as they are listed.
  const MarketPlace place{.economy = _state.Data().commander.currentSystem.economy,
                          .government = _state.Data().commander.currentSystem.government,
                          .techLevel = _state.Data().commander.currentSystem.techLevel};
  const std::uint8_t onSale = EquipmentRowsOnSale(place.techLevel);
  _state.Data().menus.menuRowCount = 0;
  std::uint16_t cell = FIRST_ITEM_OFFSET;
  const std::span<std::uint8_t> priceText(_state.Data().menus.priceText);
  for (std::uint8_t row = 1; row <= onSale; ++row)
  {
    _state.Data().menus.menuRowCount = row;
    const EquipmentItem item = EquipmentItemAt(row);
    PrintTextModeString(_state, item.name, cell);
    const std::uint16_t price = EquipmentPriceTenths(item, place);
    RowPrices(_state, row).price = price;
    RowPrices(_state, row).resale = 0;
    // A resale price only for an item fitted, and never for fuel.
    const bool resold = row != FUEL_ROW && Fitted(_state, row) != 0;
    _state.Data().menus.resalePriceInput = resold ? price : std::uint16_t{0};
    FormatTenths(_state, price);
    PrintTextModeString(_state, TextIn(priceText), Offset(cell, PRICE_COLUMN));
    const std::uint16_t resale = ComputeResalePrice(_state);
    std::string_view resaleText = NO_RESALE_TEXT;
    if (resale != 0)
    {
      RowPrices(_state, _state.Data().menus.menuRowCount).resale = resale;
      FormatDecimal5(resale, priceText);
      BlankLeadingZeros(priceText, RESALE_DIGITS_BLANKED);
      // Tenths: the last digit moved right one for the point.
      priceText[TENTHS_DIGIT + 1] = std::uint8_t{priceText[TENTHS_DIGIT]};
      priceText[TENTHS_DIGIT] = '.';
      resaleText = TextIn(priceText);
    }
    PrintTextModeString(_state, resaleText, Offset(cell, RESALE_COLUMN));
    cell = Offset(cell, ROW_BYTES);
  }
  return RunEquipShipMenu(_state, _hardware, EQUIP_MENU_FIRST_ROW);
}

bool SelectLaserType(GameState& _state)
{
  // The original counts selectedLaserType up a type at a time, and stops at the last when the row is no laser's.
  const std::optional<std::uint8_t> type = LaserTypeOfRow(_state.Data().menus.menuSelectedRow);
  _state.Data().equipment.selectedLaserType = type.value_or(static_cast<std::uint8_t>(LASER_ROWS.size() - 1));
  return type.has_value();
}

ScreenKey RunEquipShipMenu(GameState& _state, Hardware& _hardware, std::uint16_t _firstRow)
{
  _state.Data().menus.menuFirstRowAttr = _firstRow;
  std::uint16_t row = StartMenu(_state).row;
  for (;;)
  {
    // 612F: the steering; then the keys.
    row = SteerMenuCursor(_state, _hardware, row);
    for (;;)
    {
      const KeyPress key = PollMenuKey(_state, _hardware);
      const std::uint8_t code = key.scanCode;
      if (code == 0)
      {
        break;
      }
      // PUSH AX and POP AX round it.
      PrintCreditsOnMessageLine(_state);
      if (code == SCAN_B || code == SCAN_S)
      {
        // B buys and S sells the row's item, and its message.
        const std::uint8_t saved = OpenEquipMessage(_state);
        ShowEquipMessage(_state, code == SCAN_B ? BuyEquipment(_state, _hardware) : SellEquipment(_state, _hardware), saved);
        break;
      }
      if (code == SCAN_UP || code == SCAN_DOWN)
      {
        row = (code == SCAN_UP ? MoveMenuCursorUp(_state, row) : MoveMenuCursorDown(_state, row)).row;
        continue;
      }
      // The screen's own F4 does nothing here.
      if (code != SCAN_F4 && IsScreenKey(code))
      {
        return ScreenKey{code};
      }
      break;
    }
  }
}

void DrawLaserMountMenu(GameState& _state)
{
  _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
  PrintCountedTextLines(_state, MOUNT_HEADER_LINES, MOUNT_HEADER_OFFSET);
  // Each mount's laser, or Free, one after the other.
  const LaserMounts mounts = CurrentMounts(_state);
  std::uint16_t printed = MOUNT_NAMES_OFFSET;
  for (std::uint8_t mount = 0; mount < MOUNT_COUNT; ++mount)
  {
    const std::string_view name = MountHoldsLaser(mounts, mount) ? LASER_TYPE_NAMES[LaserTypeOnMount(mounts, mount)] : FREE_MOUNT_TEXT;
    printed = PrintTextModeString(_state, name, printed);
  }
  _state.Data().drawing.textAttribute = MOUNT_HIGHLIGHT_ATTRIBUTE;
  PaintLaserMountBox(_state, FIRST_MOUNT_BOX);
  _state.Data().equipment.selectedLaserMount = 0;
}

void ChooseMountToFitLaser(GameState& _state, Hardware& _hardware)
{
  DrawLaserMountMenu(_state);
  // DrawLaserMountMenu leaves SI on FORE's box.
  RunMountChooser(_state, _hardware, &FitLaserOnMount, FIRST_MOUNT_BOX);
}

void ChooseMountToRemoveLaser(GameState& _state, Hardware& _hardware)
{
  DrawLaserMountMenu(_state);
  _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
  PrintTextModeString(_state, SELL_LASER_TEXT, SELL_LASER_OFFSET);
  // SI, pushed and popped round the print, on FORE's box.
  RunMountChooser(_state, _hardware, &RemoveLaserFromMount, FIRST_MOUNT_BOX);
}

void RedrawEquipHelpText(GameState& _state)
{
  _state.Data().drawing.textAttribute = HELP_ATTRIBUTE;
  PrintCountedTextLines(_state, EQUIP_HELP_TEXT, HELP_TEXT_OFFSET);
  _state.Data().drawing.textAttribute = MENU_ATTRIBUTE;
}

void PaintLaserMountBox(GameState& _state, std::uint16_t _box)
{
  const std::uint8_t attribute = _state.Data().drawing.textAttribute;
  std::uint16_t cell = _box;
  for (std::uint8_t row = 0; row < MOUNT_BOX_ROWS; ++row)
  {
    for (std::uint16_t column = 0; column < MOUNT_BOX_COLUMNS; ++column)
    {
      _state.SetVideoByte(cell, attribute);
      cell = Offset(cell, 2);
    }
    cell = Offset(cell, MOUNT_BOX_ROW_SKIP);
  }
}

bool PayForEquipmentItem(GameState& _state)
{
  const std::uint8_t row = _state.Data().menus.menuSelectedRow;
  const std::uint16_t price = RowPrices(_state, row).price;
  return SubtractCredits(_state, row == FUEL_ROW ? FuelPriceTenths(_state.Data().commander.fuel, price) : price);
}

void ClearEquipmentSellPrice(GameState& _state)
{
  PrintEquipmentSellColumn(_state, NO_RESALE_TEXT);
}

void ShowEquipmentSellPrice(GameState& _state)
{
  // The row's prices in screenPrices, its price and the resale price after it. The original keeps the slot in BX on the stack
  // round ComputeResalePrice.
  MenusState::ScreenPrice& prices = RowPrices(_state, _state.Data().menus.menuSelectedRow);
  _state.Data().menus.resalePriceInput = std::uint16_t{prices.price};
  const std::uint16_t resale = ComputeResalePrice(_state);
  prices.resale = resale;
  FormatTenths(_state, resale);
  PrintEquipmentSellColumn(_state, TextWithin(_state.Data().menus.priceText, 0));
}

std::uint16_t MenuFirstRowCell(const GameState& _state) noexcept
{
  return static_cast<std::uint16_t>(_state.Data().menus.menuFirstRowAttr * ROW_BYTES + MENU_ROW_ATTRIBUTE);
}

MenuCursor StartMenu(GameState& _state)
{
  SwapTextAttributeNibbles(_state);
  PrintCreditsOnMessageLine(_state);
  SwapTextAttributeNibbles(_state);
  const std::uint16_t row = MenuFirstRowCell(_state);
  _state.Data().menus.menuSelectedRow = 1;
  ToggleMenuRowHighlight(_state, row);
  return MenuCursor{row};
}

MenuCursor MoveMenuCursorUp(GameState& _state, std::uint16_t _row)
{
  PrintCreditsOnMessageLine(_state);
  ToggleMenuRowHighlight(_state, _row);
  std::uint16_t row = _row;
  const auto selected = static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow - 1);
  _state.Data().menus.menuSelectedRow = selected;
  if (selected == 0)
  {
    // To the last row: count*80 bytes on, as count*256/4 and that /4 again.
    row = MenuFirstRowCell(_state);
    const std::uint8_t count = _state.Data().menus.menuRowCount;
    _state.Data().menus.menuSelectedRow = count;
    const std::uint16_t quarter = MenuRowsQuarter(count);
    row = Offset(Offset(row, quarter), static_cast<std::uint16_t>(quarter >> 2));
  }
  row = static_cast<std::uint16_t>(row - ROW_BYTES);
  ToggleMenuRowHighlight(_state, row);
  return MenuCursor{row};
}

MenuCursor MoveMenuCursorDown(GameState& _state, std::uint16_t _row)
{
  PrintCreditsOnMessageLine(_state);
  ToggleMenuRowHighlight(_state, _row);
  std::uint16_t row = _row;
  const auto selected = static_cast<std::uint8_t>(_state.Data().menus.menuSelectedRow + 1);
  _state.Data().menus.menuSelectedRow = selected;
  if (_state.Data().menus.menuRowCount < selected)
  {
    row = static_cast<std::uint16_t>(MenuFirstRowCell(_state) - ROW_BYTES);
    _state.Data().menus.menuSelectedRow = 1;
  }
  row = static_cast<std::uint16_t>(row + ROW_BYTES);
  ToggleMenuRowHighlight(_state, row);
  return MenuCursor{row};
}

bool IsScreenKey(std::uint8_t _key) noexcept
{
  return _key == SCAN_ESCAPE || (_key >= SCAN_F1 && _key < SCAN_PAST_F10);
}

std::uint16_t SteerMenuCursor(GameState& _state, Hardware& _hardware, std::uint16_t _row)
{
  // NEG AH: the pitch negated, and up when that is negative.
  const std::uint8_t pitch = Negate(ReadSteering(_state, _hardware).pitch);
  if (pitch == 0)
  {
    return _row;
  }
  return ((pitch & 0x80) != 0 ? MoveMenuCursorUp(_state, _row) : MoveMenuCursorDown(_state, _row)).row;
}

KeyPress PollMenuKey(GameState& _state, Hardware& _hardware)
{
  for (std::uint16_t ticks = TICKS_PER_POLL;;)
  {
    WaitForTimerTick(_state, _hardware);
    if (--ticks == 0)
    {
      break;
    }
  }
  return GetKey(_state, _hardware);
}

// ── The shipyard's rules ──

EquipmentItem EquipmentItemAt(std::uint8_t _row) noexcept
{
  // equipmentList: a count, then for each item its tech level, its name to a NUL, its government's and economy's factors and its
  // base price, a word.
  const std::span<const std::uint8_t> list(EQUIPMENT_LIST);
  std::size_t at = 1;
  for (std::uint8_t row = 1;; ++row)
  {
    const std::string_view name = TextInTable(list, at + 1);
    const std::size_t factors = at + 1 + name.size() + 1;
    if (row == _row)
    {
      return EquipmentItem{.techLevel = list[at],
                           .name = name,
                           .governmentFactor = static_cast<std::int8_t>(list[factors]),
                           .economyFactor = static_cast<std::int8_t>(list[factors + 1]),
                           .basePriceTenths = WordIn(list, factors + 2)};
    }
    at = factors + 4;
  }
}

std::uint8_t EquipmentRowsOnSale(std::uint8_t _techLevel) noexcept
{
  // The list is sorted by tech level: the first item beyond the system's level + 1 ends it, or else the count does.
  const auto limit = static_cast<std::uint8_t>(_techLevel + 2);
  std::uint8_t rows = 0;
  while (rows < EQUIPMENT_LIST_ROWS && EquipmentItemAt(static_cast<std::uint8_t>(rows + 1)).techLevel < limit)
  {
    ++rows;
  }
  return rows;
}

std::uint16_t EquipmentPriceTenths(const EquipmentItem& _item, const MarketPlace& _place) noexcept
{
  // The government's and the economy's factors, each by IMUL, and the base price.
  const std::uint16_t government = SignedProduct(static_cast<std::uint8_t>(_item.governmentFactor), _place.government);
  const std::uint16_t economy = SignedProduct(static_cast<std::uint8_t>(_item.economyFactor), _place.economy);
  return static_cast<std::uint16_t>(economy + government + _item.basePriceTenths);
}

std::uint16_t FuelPriceTenths(std::uint8_t _fuel, std::uint16_t _fuelPriceTenths)
{
  // What fills the tank: (255-fuel) * the price's low byte / 36, at least 1, by a DIV that can overflow into the game's trap.
  const auto missing = static_cast<std::uint8_t>(~_fuel);
  const auto product = static_cast<std::uint16_t>(missing * Low(_fuelPriceTenths));
  const std::uint8_t units = DivideByte(product, FUEL_UNITS_PER_TENTH).quotient;
  return units == 0 ? std::uint16_t{1} : std::uint16_t{units};
}

std::optional<std::uint8_t> LaserTypeOfRow(std::uint8_t _row) noexcept
{
  for (std::size_t type = 0; type < LASER_ROWS.size(); ++type)
  {
    if (_row == LASER_ROWS[type])
    {
      return static_cast<std::uint8_t>(type);
    }
  }
  return std::nullopt;
}

EquipmentRefusal RefuseToFit(std::uint8_t _row, const Fitting& _fitting) noexcept
{
  const std::uint8_t count = _fitting.counts[std::size_t{_row} - 1];
  if (_row == FUEL_ROW)
  {
    if (_fitting.missionNumber == NO_FUEL_MISSION)
    {
      return EquipmentRefusal::NoFuelAvailable;
    }
    return count >= FUEL_NEARLY_FULL ? EquipmentRefusal::FuelFull : EquipmentRefusal::None;
  }
  if (_row == MISSILE_ROW)
  {
    return count == MISSILE_LIMIT ? EquipmentRefusal::FourMissilesOnly : EquipmentRefusal::None;
  }
  if (!LaserTypeOfRow(_row))
  {
    return count != 0 ? EquipmentRefusal::AlreadyFitted : EquipmentRefusal::None;
  }
  // A laser: a free mount, and a mining laser only with fuel scoops.
  if (_fitting.laserMountsFitted == ALL_MOUNTS)
  {
    return EquipmentRefusal::FourLasers;
  }
  if (_row == MINING_LASER_ROW && _fitting.counts[FUEL_SCOOPS_ROW - 1] != 1)
  {
    return EquipmentRefusal::ScoopNeeded;
  }
  return EquipmentRefusal::None;
}

EquipmentRefusal RefuseToRemove(std::uint8_t _row, const Fitting& _fitting) noexcept
{
  const std::uint8_t count = _fitting.counts[std::size_t{_row} - 1];
  if (_row == FUEL_ROW)
  {
    return EquipmentRefusal::FuelSaleIllegal;
  }
  if (_row == MISSILE_ROW)
  {
    return count == 0 ? EquipmentRefusal::NoMissileToSell : EquipmentRefusal::None;
  }
  if (_row == CARGO_BAY_ROW && _fitting.cargoUsedTonnes >= CARGO_BAY_SALE_TONNES)
  {
    return EquipmentRefusal::TooMuchCargo;
  }
  if (count == 0)
  {
    return EquipmentRefusal::NoItemToSell;
  }
  if (_row == FUEL_SCOOPS_ROW && _fitting.counts[MINING_LASER_ROW - 1] != 0)
  {
    return EquipmentRefusal::SellMiningLaser;
  }
  return EquipmentRefusal::None;
}

std::uint8_t CountAfterFitting(std::uint8_t _row, std::uint8_t _count) noexcept
{
  return _row == FUEL_ROW ? FUEL_FULL : static_cast<std::uint8_t>(_count + 1);
}

bool MountHoldsLaser(const LaserMounts& _mounts, std::uint8_t _mount) noexcept
{
  // SHR CH,CL with CL the mount + 1: the mount's bit, shifted out into the carry.
  return ((_mounts.fitted >> _mount) & 1) != 0;
}

std::uint8_t LaserTypeOnMount(const LaserMounts& _mounts, std::uint8_t _mount) noexcept
{
  return static_cast<std::uint8_t>((_mounts.types >> (_mount * 2u)) & MOUNT_TYPE_BITS);
}

MountRefusal RefuseToFitMount(const LaserMounts& _mounts, std::uint8_t _mount) noexcept
{
  return MountHoldsLaser(_mounts, _mount) ? MountRefusal::Occupied : MountRefusal::None;
}

MountRefusal RefuseToClearMount(const LaserMounts& _mounts, std::uint8_t _mount, std::uint8_t _type) noexcept
{
  if (!MountHoldsLaser(_mounts, _mount))
  {
    return MountRefusal::NoLaser;
  }
  return LaserTypeOnMount(_mounts, _mount) != _type ? MountRefusal::WrongType : MountRefusal::None;
}

LaserMounts WithLaserFitted(const LaserMounts& _mounts, std::uint8_t _mount, std::uint8_t _type) noexcept
{
  // 3 and the type, shifted up together as AX to the mount's two bits: AL the bits to clear, AH the type to set.
  const auto bits = static_cast<std::uint16_t>(Join(_type, MOUNT_TYPE_BITS) << (_mount * 2u));
  return LaserMounts{.fitted = static_cast<std::uint8_t>(_mounts.fitted | (1u << _mount)),
                     .types = static_cast<std::uint8_t>((_mounts.types & ~Low(bits)) | High(bits))};
}

LaserMounts WithLaserCleared(const LaserMounts& _mounts, std::uint8_t _mount) noexcept
{
  return LaserMounts{.fitted = static_cast<std::uint8_t>(_mounts.fitted & ~(1u << _mount)), .types = _mounts.types};
}

} // namespace Elite
