#include "pch.h"

#include "DataOverlay.h"
#include "GameState.h"
#include "Maths.h"
#include "ObjectSlot.h"
#include "Ships.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

// An object slot: 64 bytes, the type in bits 1-5 of the first with the active bit; the flags a splinter and a barrel carry.
constexpr std::uint16_t SLOT_BYTES = 0x40;
constexpr std::uint8_t TYPE_SPLINTER = 0x0B;
constexpr std::uint8_t FLAG_PRECIOUS = 0x10;
constexpr std::uint8_t FLAG_MASKING_DEVICE = 0x40;

// The data segment byte at _offset, set to _value on the twin.
void SetByte(TwinRig& _rig, std::uint16_t _offset, std::uint8_t _value)
{
  _rig.Both([_offset, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _offset, _value); });
}

void SetByte(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  SetByte(_rig, _field.offset, _value);
}

// The cash, in tenths of credits, on the twin.
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

// The byte _field as the twin holds it.
[[nodiscard]] std::uint8_t TwinByte(TwinRig& _rig, Elite::DataField<std::uint8_t> _field)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _field](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { value = _pc.Ram().Read8(Elite::DataSegment(_program), _field.offset); });
  return value;
}

// The message line's text, as the twin has it.
[[nodiscard]] std::uint16_t TwinMessage(TwinRig& _rig)
{
  std::uint16_t message = 0;
  _rig.Both([&message](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { message = _pc.Ram().Read16(Elite::DataSegment(_program), DS.messagePointer.offset); });
  return message;
}

// What the scoop twin puts below the nose.
enum class Scoopable : std::uint8_t
{
  Barrel,
  MaskingDeviceBarrel,
  Thargon,
  MineralSplinter
};

// Below the nose: TryScoopObject scoops what is from 1Eh to E5h down and within 95h either side and ahead, and CheckCollision
// hits what is within 64h of the player on every axis.
constexpr std::int16_t SCOOP_BELOW = 0x80;
constexpr std::int16_t SCOOP_AHEAD = 0x60;
// What DropCargo and SpawnFragments make.
constexpr std::uint8_t FLAG_RESTING = 0x08;
constexpr std::uint8_t SPLINTER_ACTIVE = (TYPE_SPLINTER << 1) | 1;
constexpr std::uint8_t DEBRIS_CLASS = 7;
constexpr std::uint16_t DRIFTER_TEMPLATES = 1; // spawnTemplates' entry of the first drifter
constexpr std::uint8_t ASTEROID_DRIFTER = 4;   // the asteroid among them
constexpr std::uint16_t FRAGMENT_SCATTER_MASK = 0x1F1F;
constexpr std::uint8_t FRAGMENT_SCATTER_CENTER = 0x0F;
constexpr std::uint16_t MINERALS_ODDS = 2000;
constexpr std::uint8_t FRAGMENT_LIFETIME_MASK = 0x0F;
constexpr std::uint8_t MINED_FRAGMENT_LIFETIME = 0x3C + 0x14;
constexpr int MOST_DRAWS = 1000;

// SAR AL,1.
[[nodiscard]] std::uint8_t HalveSigned(std::uint8_t _value) noexcept
{
  return static_cast<std::uint8_t>(static_cast<std::int8_t>(_value) >> 1);
}

// The splinter at _slot, cleared, made as SpawnFragments makes one of an asteroid the mining laser split: a copy of the asteroid
// (here its template's bytes, InitObjectFromTemplate, at rest), its spin a random word, its velocity the asteroid's halved and
// the spin's scatter of -15 to 16 halved again on each axis, at rest; minerals by a draw below 2000 in 65536, with the random
// state drawn on by NextRandom until that draw comes; and a mined fragment's lifetime.
void MakeMineralSplinter(Elite::GameState& _state, Elite::ObjectSlot _slot)
{
  Elite::InitObjectFromTemplate(_state, _slot, DS.spawnTemplates.At(DRIFTER_TEMPLATES), ASTEROID_DRIFTER);
  _slot.Set(Elite::SlotByte::State, 0);
  const std::uint16_t spin = Elite::NextRandom(_state);
  _slot.Set(Elite::SlotWord::Spin, spin);
  const auto scatter = static_cast<std::uint16_t>(spin & FRAGMENT_SCATTER_MASK);
  _slot.Set(Elite::SlotByte::VelocityX, HalveSigned(static_cast<std::uint8_t>((scatter & 0xFF) - FRAGMENT_SCATTER_CENTER)));
  _slot.Set(Elite::SlotByte::VelocityY, HalveSigned(static_cast<std::uint8_t>((scatter >> 8) - FRAGMENT_SCATTER_CENTER)));
  _slot.Set(Elite::SlotByte::VelocityZ, HalveSigned(static_cast<std::uint8_t>(((spin >> 3) & 0x1F) - FRAGMENT_SCATTER_CENTER)));
  for (int draws = 0; static_cast<std::uint16_t>(_state.Get(DS.randomState0) + _state.Get(DS.randomState1)) >= MINERALS_ODDS; ++draws)
  {
    Assert::IsTrue(draws < MOST_DRAWS, L"a draw that gives minerals");
    (void)Elite::NextRandom(_state);
  }
  (void)Elite::NextRandom(_state);
  _slot.Set(Elite::SlotByte::Flags, static_cast<std::uint8_t>(FLAG_RESTING | FLAG_PRECIOUS));
  _slot.Set(Elite::SlotByte::Energy, 0);
  _slot.Set(Elite::SlotWord::Cargo, 0);
  _slot.Set(Elite::SlotByte::Age, 0);
  _slot.Set(Elite::SlotByte::Class, DEBRIS_CLASS);
  _slot.Set(Elite::SlotByte::Lifetime,
            static_cast<std::uint8_t>((Elite::NextRandom(_state) & FRAGMENT_LIFETIME_MASK) + MINED_FRAGMENT_LIFETIME));
  _slot.Set(Elite::SlotByte::Type, SPLINTER_ACTIVE);
}

// _what made below the nose of the twin's ship by the game's own routines, as the game makes it where it is made in play, in
// the slot it would take (FindFreeShipSlot, or for a splinter FindDebrisSlot), cleared (ClearObjectSlot) where the game copies
// the object it comes from; and put below the nose, where a thing the player flies over passes.
// * A barrel as DropCargo drops one: InitCargoBarrel, resting, with the masking device's bit when the ship it came from
//   carried the device (the mask ship), a random heading, and its velocity (ComputeVelocity).
// * A Thargon as LaunchShipFromObject launches one: InitThargon and its velocity, with its mother's slot, here the next one,
//   empty: the mother is gone.
// * A splinter with minerals (MakeMineralSplinter).
void MakeBelowTheNose(TwinRig& _rig, Scoopable _what)
{
  _rig.Both(
    [_what](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      std::uint16_t slot = 0;
      if (_what == Scoopable::MineralSplinter)
      {
        slot = Elite::FindDebrisSlot(state);
      }
      else
      {
        const Elite::SlotSearch free = Elite::FindFreeShipSlot(state);
        Assert::IsTrue(free.found, L"a free ship slot");
        slot = free.slot;
      }
      Elite::ClearObjectSlot(state, slot);
      Elite::ObjectSlot object(state, slot);
      switch (_what)
      {
      case Scoopable::Barrel:
      case Scoopable::MaskingDeviceBarrel:
      {
        Elite::InitCargoBarrel(state, object);
        object.Set(Elite::SlotByte::Flags,
                   static_cast<std::uint8_t>(FLAG_RESTING | (_what == Scoopable::MaskingDeviceBarrel ? FLAG_MASKING_DEVICE : 0)));
        const std::uint16_t heading = Elite::NextRandom(state);
        object.Set(Elite::SlotWord::Pitch, heading);
        object.Set(Elite::SlotWord::Yaw, static_cast<std::uint16_t>((heading << 8) | (heading >> 8)));
        (void)Elite::ComputeVelocity(state, object);
        break;
      }
      case Scoopable::Thargon:
        Elite::InitThargon(state, object);
        (void)Elite::ComputeVelocity(state, object);
        object.Set(Elite::SlotWord::Owner, static_cast<std::uint16_t>(slot + SLOT_BYTES));
        break;
      case Scoopable::MineralSplinter:
        MakeMineralSplinter(state, object);
        break;
      }
      object.Set(Elite::SlotWord::X, 0);
      object.Set(Elite::SlotWord::Y, static_cast<std::uint16_t>(SCOOP_BELOW));
      object.Set(Elite::SlotWord::Z, static_cast<std::uint16_t>(SCOOP_AHEAD));
      object.Set(Elite::SlotWord::XYHigh, 0);
      object.Set(Elite::SlotByte::ZHigh, 0);
    });
}

} // namespace

// Twins for the equipment screen and the fuel scoops in states no replay reaches (ADR-016).
TEST_CLASS(EquipmentTests)
{
public:
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

  // The fuel scoops' rare paths (ADR-008 item 8, D20), which TryScoopObject takes for what TransformShip offers it below the
  // nose: the masking device's barrel, which the mask ship drops; a Thargon, alien items; a splinter with minerals, gems, gold and
  // platinum and, with room, minerals; and a barrel with the hold full. With fuel scoops, which the equipment screen fits with
  // a byte of 1 (Jameson's 100 credits do not buy them), and 18 tonnes of cargo bought at Lave, the Thargon and the splinter fill
  // the hold. Each is made below the nose as the game makes it (MakeBelowTheNose) and scooped, or refused, by the next frame.
  TEST_METHOD(FuelScoopsTakeTheRarePaths)
  {
    TwinRig rig("TwinScoop");
    rig.Play("key space; wait 4");
    SetByte(rig, DS.fuelScoopsFitted, 1);
    // All 11 tonnes of food Lave sells, and 7 of textiles, the next row.
    rig.Play("key F3; wait 0.5\nkey b; wait 0.2; key 1; key 1; key Return; wait 0.3\nkey Down; wait 0.15\n"
             "key b; wait 0.2; key 7; key Return; wait 0.3\ndigest cargo");
    Assert::AreEqual(std::uint8_t{18}, TwinByte(rig, DS.cargoUsedTonnes), L"18 tonnes aboard");
    rig.Play("key F1; wait 1.5\ndigest launched");
    MakeBelowTheNose(rig, Scoopable::MaskingDeviceBarrel);
    rig.Play("wait 0.2\ndigest masking-device");
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, DS.maskingDeviceRecovered), L"the masking device recovered");
    Assert::AreEqual(DS.maskingDeviceText.offset, TwinMessage(rig), L"Masking Device!");
    MakeBelowTheNose(rig, Scoopable::Thargon);
    rig.Play("wait 0.2\ndigest alien-items");
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, DS.cargoAlienItemsTonnes), L"a tonne of alien items");
    MakeBelowTheNose(rig, Scoopable::MineralSplinter);
    rig.Play("wait 0.2\ndigest minerals");
    Assert::AreEqual(DS.preciousMetalsText.offset, TwinMessage(rig), L"precious metals");
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, DS.cargoMineralsTonnes), L"a tonne of minerals");
    Assert::AreEqual(std::uint8_t{20}, TwinByte(rig, DS.cargoUsedTonnes), L"the hold full");
    MakeBelowTheNose(rig, Scoopable::Barrel);
    rig.Play("wait 0.2\ndigest hold-full");
    Assert::AreEqual(DS.cargoBayFullText.offset, TwinMessage(rig), L"Cargo Bay Full");
  }
};

} // namespace GameLogicTests
