#include "pch.h"

#include "Equipment.h"

#include "DataOverlay.h"
#include "Text.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t COMPUTE_RESALE_PRICE = 0x6995;

// DivideOverflowInterrupt's scratch words in the code segment, which it writes before anything else.
constexpr std::uint16_t DIVIDE_SAVED_BX_OFFSET = 0x02A1;
constexpr std::uint16_t DIVIDE_SAVED_DS_OFFSET = 0x02A3;
// What it leaves in AL after a byte divide overflows: the positive maximum (plan §7.1).
constexpr std::uint8_t DIVIDE_OVERFLOW_BYTE = 0x7F;

constexpr std::uint16_t SCREEN_PRICES = 0x823B; // four bytes a row: the price, then the resale price, in tenths
constexpr std::uint8_t FUEL_ROW = 1;
constexpr std::uint8_t FUEL_UNITS_PER_TENTH = 0x24;
constexpr std::array<std::uint8_t, 4> LASER_ROWS = {0x05, 0x06, 0x0D, 0x0E}; // pulse, beam, mining, military

constexpr std::uint8_t SELL_PRICE_ATTRIBUTE = 0x79;
constexpr std::uint8_t MENU_ATTRIBUTE = 0x1E;
constexpr std::uint16_t SELL_PRICE_COLUMN = 0x39;

[[nodiscard]] std::uint8_t Low(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word);
}

[[nodiscard]] std::uint8_t High(std::uint16_t _word) noexcept
{
  return static_cast<std::uint8_t>(_word >> 8);
}

[[nodiscard]] std::uint16_t WithLow(std::uint16_t _word, std::uint8_t _low) noexcept
{
  return static_cast<std::uint16_t>((_word & 0xFF00) | _low);
}

[[nodiscard]] std::uint16_t PriceSlot(std::uint8_t _row) noexcept
{
  return static_cast<std::uint16_t>(SCREEN_PRICES + _row * 4);
}

// PrintEquipmentSellColumn (0x6949): the text at SI in the resale column of menuSelectedRow's row.
void PrintEquipmentSellColumn(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Set(DS.textAttribute, SELL_PRICE_ATTRIBUTE);
  // (row-1)*80 bytes, as (row-1)*256/4 and that /4 again.
  regs.ax = static_cast<std::uint16_t>(static_cast<std::uint8_t>(_guest.Get(DS.menuSelectedRow) - 1) << 8);
  regs.ax = static_cast<std::uint16_t>(regs.ax >> 2);
  regs.di = static_cast<std::uint16_t>(_guest.Get(DS.menuFirstRowAttr) + regs.ax);
  regs.ax = static_cast<std::uint16_t>(regs.ax >> 2);
  regs.di = static_cast<std::uint16_t>(regs.di + regs.ax + SELL_PRICE_COLUMN);
  PrintTextModeString(_guest);
  _guest.Set(DS.textAttribute, MENU_ATTRIBUTE);
}

} // namespace

void SelectLaserType(Guest& _guest)
{
  const std::uint8_t row = _guest.Get(DS.menuSelectedRow);
  std::uint8_t type = 0;
  _guest.Set(DS.selectedLaserType, type);
  for (;;)
  {
    const bool laser = row == LASER_ROWS[type];
    if (laser || type + 1u == LASER_ROWS.size())
    {
      _guest.SetFlag(Machine::FLAG_ZERO, laser);
      return;
    }
    ++type;
    _guest.Set(DS.selectedLaserType, type);
  }
}

void PayForEquipmentItem(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t row = _guest.Get(DS.menuSelectedRow);
  regs.bx = WithLow(regs.bx, row);
  if (row == FUEL_ROW)
  {
    // What fills the tank: (255-fuel) * the price's low byte / 36, at least 1.
    const auto missing = static_cast<std::uint8_t>(~_guest.Get(DS.fuel));
    regs.dx = _guest.Get(DS.data823F);
    const auto product = static_cast<std::uint16_t>(missing * Low(regs.dx));
    regs.dx = WithLow(regs.dx, FUEL_UNITS_PER_TENTH);
    if (product / FUEL_UNITS_PER_TENTH > 0xFF)
    {
      // DIV DL overflows: DivideOverflowInterrupt saves BX and DS and saturates AL, leaving AH.
      _guest.SetCodeWord(DIVIDE_SAVED_BX_OFFSET, regs.bx);
      _guest.SetCodeWord(DIVIDE_SAVED_DS_OFFSET, regs.ds);
      regs.ax = static_cast<std::uint16_t>((High(product) << 8) | DIVIDE_OVERFLOW_BYTE);
    }
    else
    {
      regs.ax = static_cast<std::uint16_t>(((product % FUEL_UNITS_PER_TENTH) << 8) | (product / FUEL_UNITS_PER_TENTH));
    }
    if (Low(regs.ax) == 0)
    {
      regs.ax = WithLow(regs.ax, 1);
    }
    regs.ax = Low(regs.ax);
  }
  else
  {
    regs.bx = PriceSlot(row);
    regs.ax = _guest.Word(regs.bx);
  }
  regs.bx = 0;

  // SubtractCredits (0x65BA): the 32-bit credits less AX, put back if that borrows.
  const std::uint32_t credits = _guest.Word(DS.creditsTenths.offset) | (std::uint32_t{_guest.Get(DS.data75F5)} << 16);
  if (credits < regs.ax)
  {
    _guest.SetFlag(Machine::FLAG_CARRY, true);
    return;
  }
  const std::uint32_t left = credits - regs.ax;
  _guest.SetWord(DS.creditsTenths.offset, static_cast<std::uint16_t>(left));
  _guest.Set(DS.data75F5, static_cast<std::uint16_t>(left >> 16));
  FormatCredits(_guest);
  _guest.SetFlag(Machine::FLAG_CARRY, false);
}

void ShowEquipmentSellPrice(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.bx = PriceSlot(_guest.Get(DS.menuSelectedRow));
  regs.ax = _guest.Word(regs.bx);
  _guest.Set(DS.resalePriceInput, regs.ax);
  const std::uint16_t slot = regs.bx;
  _guest.Call(COMPUTE_RESALE_PRICE);
  regs.bx = slot;
  _guest.SetWord(static_cast<std::uint16_t>(regs.bx + 2), regs.ax);
  FormatTenths(_guest);
  regs.si = DS.priceText.offset;
  PrintEquipmentSellColumn(_guest);
}

namespace
{

using Machine::FLAG_CARRY;
using Machine::FLAG_ZERO;

constexpr std::array ENTRIES = {
  NativeEntry{0x633B, "SelectLaserType", &SelectLaserType, Machine::NativeContract{0, FLAG_ZERO}},
  NativeEntry{0x65A3, "PayForEquipmentItem", &PayForEquipmentItem, Machine::NativeContract{0, FLAG_CARRY}},
  NativeEntry{0x6972, "ShowEquipmentSellPrice", &ShowEquipmentSellPrice, PRESERVES_ALL},
};

} // namespace

std::span<const NativeEntry> EquipmentEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
