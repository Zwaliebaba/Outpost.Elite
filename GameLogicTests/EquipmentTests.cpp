#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "TwinRig.h"

#include <array>
#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t LAUNCH_ESCAPE_POD = 0x2F0F;
constexpr std::uint16_t TRY_SCOOP_OBJECT = 0x4401;
constexpr std::uint16_t REDRAW_EQUIP_HELP_TEXT = 0x653F;
constexpr std::uint16_t PAY_FOR_EQUIPMENT_ITEM = 0x65A3;
constexpr std::uint16_t SCREEN_PRICES = 0x823B; // four bytes a row, the price first
constexpr std::uint8_t FUEL_ROW = 1;
constexpr std::uint8_t MISSILE_ROW = 2;

// An object slot: 64 bytes, the type in bits 1-5 of the first with the active bit, the flags at +1Eh.
constexpr std::uint16_t SLOT_BYTES = 0x40;
constexpr std::uint16_t SLOT_FLAGS = 0x1E;
constexpr std::uint8_t TYPE_THARGON = 0x07;
constexpr std::uint8_t TYPE_SPLINTER = 0x0B;
constexpr std::uint8_t TYPE_CARGO_BARREL = 0x11;
constexpr std::uint8_t TYPE_ESCAPE_POD = 0x15;
constexpr std::uint8_t TYPE_ASP = 0x18;
constexpr std::uint8_t FLAG_PRECIOUS = 0x10;
constexpr std::uint8_t FLAG_MASKING_DEVICE = 0x40;
constexpr std::uint8_t SHIP_SLOTS = 17;

// A fuel level and the price of a light year. A plain aggregate rather than std::pair: brace-initializing
// a pair of narrow integers from int literals narrows inside MSVC's <utility>, which /W4 /WX rejects.
struct FuelFill
{
  std::uint8_t fuel;
  std::uint16_t price;
};

// A view position TryScoopObject is given: AX, BX, CX.
struct ViewPosition
{
  std::uint16_t x;
  std::uint16_t y;
  std::uint16_t z;
};

// What is scooped: the slot's type and flags, and the tonnes already aboard.
struct Scooped
{
  std::uint8_t type;
  std::uint8_t flags;
  std::uint8_t usedTonnes;
};

// The data segment byte at _offset, set to _value on both twins.
void SetByte(TwinRig& _rig, std::uint16_t _offset, std::uint8_t _value)
{
  _rig.Both([_offset, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _offset, _value); });
}

void SetByte(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  SetByte(_rig, _field.offset, _value);
}

// The cash, in tenths of credits, on both twins.
void SetCredits(TwinRig& _rig, std::uint32_t _tenths)
{
  _rig.Both(
    [_tenths](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      const std::uint16_t data = Elite::DataSegment(_program);
      _pc.Ram().Write16(data, DS.creditsTenths.offset, static_cast<std::uint16_t>(_tenths));
      _pc.Ram().Write16(data, DS.data75F5.offset, static_cast<std::uint16_t>(_tenths >> 16));
    });
}

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

  // The escape pod from the title screen's slots: once with a slot free, once with every ship slot taken, so that
  // ReclaimShipSlot makes room.
  TEST_METHOD(LaunchEscapePodAgreesWithAndWithoutAFreeSlot)
  {
    ComparisonRig rig("LaunchEscapePod");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    rig.Call(LAUNCH_ESCAPE_POD, {});
    for (std::uint16_t slot = 0; slot < SHIP_SLOTS; ++slot)
    {
      const auto type = static_cast<std::uint16_t>(DS.firstShipSlot.offset + slot * SLOT_BYTES);
      ram.Write8(data, type, static_cast<std::uint8_t>(ram.Read8(data, type) | 1));
    }
    rig.Call(LAUNCH_ESCAPE_POD, {});
    rig.AssertAllAgreed(LAUNCH_ESCAPE_POD, 2);
  }

  // The scoop's box at each of its edges, and inside it everything there is to scoop: a barrel's random product
  // (furs for slaves), its masking device, the splinters with and without precious metals at their 250 cap, an
  // escape pod, a Thargon, a ship, each with the hold full and not, and with the cargo bay extension.
  TEST_METHOD(TryScoopObjectAgreesOnTheBoxAndEveryCargo)
  {
    ComparisonRig rig("TryScoopObject");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::uint16_t slot = DS.firstShipSlot.offset;
    std::uint64_t calls = 0;
    const auto scoop = [&](const Scooped& _what, const ViewPosition& _at)
    {
      ram.Write8(data, slot, static_cast<std::uint8_t>((_what.type << 1) | 1));
      ram.Write8(data, static_cast<std::uint16_t>(slot + SLOT_FLAGS), _what.flags);
      ram.Write8(data, DS.cargoUsedTonnes.offset, _what.usedTonnes);
      rig.Call(TRY_SCOOP_OBJECT, {.ax = _at.x, .bx = _at.y, .cx = _at.z, .dx = 0x4444, .si = 0x5555, .di = slot, .bp = 0x7777});
      ++calls;
    };

    constexpr Scooped BARREL{TYPE_CARGO_BARREL, 0, 0};
    constexpr std::array<ViewPosition, 12> EDGES = {{{0, 0x8000, 0},
                                                     {0, 0x001D, 0},
                                                     {0, 0x001E, 0},
                                                     {0, 0x00E5, 0},
                                                     {0, 0x00E6, 0},
                                                     {0x0095, 0x0080, 0},
                                                     {0x0096, 0x0080, 0},
                                                     {0xFF6B, 0x0080, 0},
                                                     {0xFF6A, 0x0080, 0},
                                                     {0x8000, 0x0080, 0},
                                                     {0, 0x0080, 0xFF6B},
                                                     {0, 0x0080, 0x0096}}};
    for (const ViewPosition& at : EDGES)
    {
      scoop(BARREL, at);
    }

    constexpr ViewPosition INSIDE{0x0010, 0x0080, 0xFFF0};
    // A barrel's product from the random state: AL/24, so 72-95 gives slaves, which become furs.
    for (std::uint16_t random = 0; random < 0x100; random = static_cast<std::uint16_t>(random + 12))
    {
      ram.Write16(data, DS.randomState0.offset, random);
      ram.Write16(data, DS.randomState1.offset, 0);
      scoop(BARREL, INSIDE);
    }
    ram.Write8(data, DS.largeCargoBayFitted.offset, 1);
    scoop({TYPE_CARGO_BARREL, 0, 0x22}, INSIDE);
    scoop({TYPE_CARGO_BARREL, 0, 0x23}, INSIDE);
    ram.Write8(data, DS.largeCargoBayFitted.offset, 0);
    scoop({TYPE_CARGO_BARREL, FLAG_MASKING_DEVICE, 0}, INSIDE);
    scoop({TYPE_CARGO_BARREL, FLAG_MASKING_DEVICE, 0x14}, INSIDE);
    scoop({TYPE_CARGO_BARREL, 0, 0x14}, INSIDE);

    // Precious metals: below, at and past the cap, with room for minerals or alloys and without.
    for (std::uint16_t random = 0; random < 0x200; random = static_cast<std::uint16_t>(random + 0x1D))
    {
      ram.Write16(data, DS.randomState0.offset, random);
      ram.Write16(data, DS.randomState1.offset, static_cast<std::uint16_t>(random * 0x0101));
      ram.Write16(data, DS.randomState2.offset, static_cast<std::uint16_t>(random << 7));
      const auto held = static_cast<std::uint8_t>(0xF0 + (random & 0x0F));
      ram.Write8(data, DS.cargoGemStonesGrams.offset, held);
      ram.Write8(data, DS.cargoGoldKg.offset, held);
      ram.Write8(data, DS.cargoPlatinumKg.offset, held);
      scoop({TYPE_SPLINTER, FLAG_PRECIOUS, static_cast<std::uint8_t>((random & 1) != 0 ? 0x14 : 0x02)}, INSIDE);
    }
    for (const std::uint8_t type : {TYPE_SPLINTER, TYPE_ESCAPE_POD, TYPE_THARGON, TYPE_ASP})
    {
      scoop({type, 0, 0}, INSIDE);
      scoop({type, 0, 0x14}, INSIDE);
    }
    rig.AssertAllAgreed(TRY_SCOOP_OBJECT, calls);
  }

  // Every message of the equipment menu, B and S on each kind of row, the cursor round both ends by keys and by the
  // steering, and the keys it ignores: what RunEquipShipMenu does that docked-screens.replay does not.
  TEST_METHOD(EquipmentMenuBuysAndSellsEveryKindOfItem)
  {
    TwinRig rig("TwinEquipmentMenu");
    rig.Play("key space; wait 4");
    SetCredits(rig, 1000000);
    SetByte(rig, DS.currentTechLevel, 9); // all 14 items
    SetByte(rig, DS.fuel, 100);
    SetByte(rig, DS.missionNumber, 1);
    rig.Play("key F4; wait 1\nkey b; wait 0.2"); // no fuel in the first mission
    SetByte(rig, DS.missionNumber, 0);
    // Fuel bought, then full, and never sold; a fourth missile, and not a fifth.
    rig.Play("key b; wait 0.2\nkey b; wait 0.2\nkey s; wait 0.2\nkey Down; wait 0.2\nkey b; wait 0.2\nkey b; wait 0.2");
    SetByte(rig, DS.missileCount, 1);
    rig.Play("key s; wait 0.2\nkey s; wait 0.2\nkey Down; wait 0.2"); // the last missile sold, then none
    SetByte(rig, DS.cargoUsedTonnes, 25);
    rig.Play("key s; wait 0.2"); // too much cargo to sell the bay extension
    SetByte(rig, DS.cargoUsedTonnes, 0);
    // None to sell, bought, already fitted, sold.
    rig.Play("key s; wait 0.2\nkey b; wait 0.2\nkey b; wait 0.2\nkey s; wait 0.2\ndigest items\nkey Down; wait 0.2\nkey Down; wait 0.2");
    SetByte(rig, DS.laserMountsFitted, 0x0F);
    rig.Play("key b; wait 0.2\nkey Down; wait 0.2\nkey b; wait 0.2"); // four lasers aboard: a pulse laser, and a beam laser
    SetByte(rig, DS.laserMountsFitted, 0x01);
    rig.Play("key Down; wait 0.2\nkey b; wait 0.2"); // fuel scoops
    SetByte(rig, DS.miningLaserCount, 1);
    rig.Play("key s; wait 0.2"); // not while a mining laser needs them
    SetByte(rig, DS.miningLaserCount, 0);
    SetByte(rig, DS.fuelScoopsFitted, 0);
    rig.Play("key Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\n"
             "key Down; wait 0.2\nkey b; wait 0.2\ndigest lasers"); // a mining laser needs fuel scoops
    SetCredits(rig, 10);
    // Too little cash for the hyperdrive, round the bottom to a missile, and up to fuel.
    rig.Play("key Up; wait 0.2\nkey b; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\n"
             "key Down; wait 0.2\nkey b; wait 0.2\nkey Up; wait 0.2");
    SetByte(rig, DS.fuel, 100);
    // Too little for fuel, round the top, and the keys the menu ignores.
    rig.Play("key b; wait 0.2\nkey Up; wait 0.2\nkey F4; wait 0.2\nkey x; wait 0.2\nkey Home; wait 0.2");
    SetByte(rig, DS.keyboardPitchRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, DS.keyboardPitchRate, 0xFB);
    rig.Play("wait 0.3\ndigest cursor\nkey F9; wait 0.5\ndigest left");
  }

  // A beam laser bought and sold: both mount choosers moved round both ends by the arrow keys and by the steering,
  // Space on an occupied mount, on an empty one and on the wrong laser, and the keys they ignore.
  TEST_METHOD(LaserMountsAreChosenToFitAndToRemove)
  {
    TwinRig rig("TwinLaserMounts");
    rig.Play("key space; wait 4");
    SetCredits(rig, 1000000);
    rig.Play("key F4; wait 1\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\n"
             "key b; wait 0.3\nkey space; wait 0.2\nkey Left; wait 0.2\nkey Right; wait 0.2\nkey Right; wait 0.2\nkey x; wait 0.2");
    SetByte(rig, DS.keyboardRollRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, DS.keyboardRollRate, 0xFB);
    rig.Play("wait 0.3\nkey space; wait 0.3\ndigest fitted\nkey s; wait 0.3\nkey space; wait 0.2\nkey Left; wait 0.2\n"
             "key space; wait 0.2\nkey Right; wait 0.2\nkey Right; wait 0.2\nkey x; wait 0.2");
    SetByte(rig, DS.keyboardRollRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, DS.keyboardRollRate, 0xFB);
    rig.Play("wait 0.3\nkey space; wait 0.3\ndigest removed");
  }

  // RedrawEquipHelpText, which only the mount choosers call once Space fits or removes a laser: they wait, so no replay
  // compares it. The help's lines in their attribute from the menu's attribute and from the mount box's highlight.
  TEST_METHOD(RedrawEquipHelpTextAgreesFromEitherAttribute)
  {
    ComparisonRig rig("RedrawEquipHelpText");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    for (const std::uint8_t attribute : std::initializer_list<std::uint8_t>{0x1E, 0x70})
    {
      ram.Write8(data, DS.textAttribute.offset, attribute);
      rig.Call(REDRAW_EQUIP_HELP_TEXT, {});
    }
    rig.AssertAllAgreed(REDRAW_EQUIP_HELP_TEXT, 2);
  }
};

} // namespace GameLogicTests
