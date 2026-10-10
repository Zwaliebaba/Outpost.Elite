#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t PAY_FOR_EQUIPMENT_ITEM = 0x65A3;
constexpr std::uint16_t SCREEN_PRICES = 0x823B; // four bytes a row, the price first
constexpr std::uint8_t FUEL_ROW = 1;
constexpr std::uint8_t MISSILE_ROW = 2;

// A fuel level and the price of a light year. A plain aggregate rather than std::pair: brace-initializing
// a pair of narrow integers from int literals narrows inside MSVC's <utility>, which /W4 /WX rejects.
struct FuelFill
{
  std::uint8_t fuel;
  std::uint16_t price;
};

} // namespace

// Constructed inputs for buying equipment (plan §6.3): too little cash, and fuel, whose price divides.
TEST_CLASS(EquipmentTests)
{
public:
  TEST_METHOD(PayForEquipmentItemAgreesOnShortCashAndFuel)
  {
    ComparisonRig rig("PayForEquipmentItem");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const auto credits = [&](std::uint32_t _tenths)
    {
      ram.Write16(data, DS.creditsTenths.offset, static_cast<std::uint16_t>(_tenths));
      ram.Write16(data, DS.data75F5.offset, static_cast<std::uint16_t>(_tenths >> 16));
    };
    std::uint64_t calls = 0;

    // More than the cash: nothing is paid.
    credits(100);
    ram.Write8(data, DS.menuSelectedRow.offset, MISSILE_ROW);
    ram.Write16(data, static_cast<std::uint16_t>(SCREEN_PRICES + MISSILE_ROW * 4), 0xFFFF);
    rig.Call(PAY_FOR_EQUIPMENT_ITEM, {});
    ++calls;

    // Fuel: a full tank (still 1), some, and an empty tank at a price whose quotient overflows the divide.
    credits(0x10000);
    ram.Write8(data, DS.menuSelectedRow.offset, FUEL_ROW);
    const std::initializer_list<FuelFill> fills = {{255, 0x0002}, {100, 0x0114}, {0, 0x00FF}};
    for (const auto& [fuel, price] : fills)
    {
      ram.Write8(data, DS.fuel.offset, fuel);
      ram.Write16(data, DS.data823F.offset, price);
      rig.Call(PAY_FOR_EQUIPMENT_ITEM, {.bx = 0x5A00});
      ++calls;
    }
    rig.AssertAllAgreed(PAY_FOR_EQUIPMENT_ITEM, calls);
  }
};

} // namespace GameLogicTests
