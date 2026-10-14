#include "pch.h"

#include "GameState.h"
#include "Maths.h"
#include "SpaceObject.h"
#include "Ships.h"
#include "Text.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Commander = Elite::CommanderState;

// An object slot's type, in bits 1-5 of its first byte with the active bit; the flags a splinter and a barrel carry.
constexpr std::uint8_t TYPE_SPLINTER = 0x0B;
constexpr std::uint8_t FLAG_PRECIOUS = 0x10;
constexpr std::uint8_t FLAG_MASKING_DEVICE = 0x40;

// The byte _member names, set to _value on the twin.
template <typename Struct> void SetByte(TwinRig& _rig, DataByte<Struct> _member, std::uint8_t _value)
{
  _rig.Both([_member, _value](Elite::Host&, Elite::GameState& _state) { DataByteIn(_state, _member) = _value; });
}

// The cash, in tenths of credits, on the twin.
void SetCredits(TwinRig& _rig, std::uint32_t _tenths)
{
  _rig.Both([_tenths](Elite::Host&, Elite::GameState& _state) { _state.Data().commander.creditsTenths = _tenths; });
}

// The byte _member names, as the twin holds it.
template <typename Struct> [[nodiscard]] std::uint8_t TwinByte(TwinRig& _rig, DataByte<Struct> _member)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _member](Elite::Host&, Elite::GameState& _state) { value = DataByteIn(_state, _member); });
  return value;
}

// The units of commodity _commodity held, as the twin holds them.
[[nodiscard]] std::uint8_t TwinHeld(TwinRig& _rig, std::uint8_t _commodity)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _commodity](Elite::Host&, Elite::GameState& _state)
            { value = _state.Data().commander.cargoHold.lots[_commodity].held; });
  return value;
}

// The message line's message, as the twin has it.
[[nodiscard]] Elite::Message TwinMessage(TwinRig& _rig)
{
  Elite::Message message = Elite::Message::None;
  _rig.Both([&message](Elite::Host&, Elite::GameState& _state) { message = _state.Data().messages.messagePointer; });
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
void MakeMineralSplinter(Elite::GameState& _state, Elite::SpaceObject& _slot)
{
  Elite::InitObjectFromTemplate(_slot, DRIFTER_TEMPLATES, ASTEROID_DRIFTER);
  _slot.aiState = 0;
  const std::uint16_t spin = Elite::NextRandom(_state);
  _slot.SpinRoll() = static_cast<std::uint8_t>(spin & 0xFF);
  _slot.SpinPitch() = static_cast<std::uint8_t>(spin >> 8);
  const auto scatter = static_cast<std::uint16_t>(spin & FRAGMENT_SCATTER_MASK);
  _slot.velocityX = HalveSigned(static_cast<std::uint8_t>((scatter & 0xFF) - FRAGMENT_SCATTER_CENTER));
  _slot.velocityY = HalveSigned(static_cast<std::uint8_t>((scatter >> 8) - FRAGMENT_SCATTER_CENTER));
  _slot.velocityZ = HalveSigned(static_cast<std::uint8_t>(((spin >> 3) & 0x1F) - FRAGMENT_SCATTER_CENTER));
  for (int draws = 0;
       static_cast<std::uint16_t>(_state.Data().commander.randomState0 + _state.Data().commander.randomState1) >= MINERALS_ODDS; ++draws)
  {
    Assert::IsTrue(draws < MOST_DRAWS, L"a draw that gives minerals");
    (void)Elite::NextRandom(_state);
  }
  (void)Elite::NextRandom(_state);
  _slot.flags = static_cast<std::uint8_t>(FLAG_RESTING | FLAG_PRECIOUS);
  _slot.energy = 0;
  _slot.cargo = 0;
  _slot.fragments = 0;
  _slot.age = 0;
  _slot.behaviorClass = DEBRIS_CLASS;
  _slot.lifetime = static_cast<std::uint8_t>((Elite::NextRandom(_state) & FRAGMENT_LIFETIME_MASK) + MINED_FRAGMENT_LIFETIME);
  _slot.type = SPLINTER_ACTIVE;
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
  Elite::GameState& state = _rig.State();
  Elite::SlotIndex slot = Elite::SlotIndex::Sun;
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
  Elite::SpaceObject& object = state.Slot(slot);
  Elite::ClearObjectSlot(object);
  switch (_what)
  {
  case Scoopable::Barrel:
  case Scoopable::MaskingDeviceBarrel:
  {
    Elite::InitCargoBarrel(object);
    object.flags = static_cast<std::uint8_t>(FLAG_RESTING | (_what == Scoopable::MaskingDeviceBarrel ? FLAG_MASKING_DEVICE : 0));
    const std::uint16_t heading = Elite::NextRandom(state);
    object.pitch = heading;
    object.yaw = static_cast<std::uint16_t>((heading << 8) | (heading >> 8));
    (void)Elite::ComputeVelocity(state, object);
    break;
  }
  case Scoopable::Thargon:
    Elite::InitThargon(object);
    (void)Elite::ComputeVelocity(state, object);
    object.owner = Elite::SlotAfter(slot);
    break;
  case Scoopable::MineralSplinter:
    MakeMineralSplinter(state, object);
    break;
  }
  object.x = 0;
  object.y = static_cast<std::uint16_t>(SCOOP_BELOW);
  object.z = static_cast<std::uint16_t>(SCOOP_AHEAD);
  object.xHigh = 0;
  object.yHigh = 0;
  object.zHigh = 0;
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
    rig.Both([](Elite::Host&, Elite::GameState& _state) { _state.Data().commander.currentSystem.techLevel = 9; }); // all 14 items
    SetByte(rig, &Commander::fuel, 100);
    SetByte(rig, &Commander::missionNumber, 1);
    rig.Play("key F4; wait 1\nkey b; wait 0.2"); // no fuel in the first mission
    SetByte(rig, &Commander::missionNumber, 0);
    // Fuel bought, then full, and never sold; a fourth missile, and not a fifth.
    rig.Play("key b; wait 0.2\nkey b; wait 0.2\nkey s; wait 0.2\nkey Down; wait 0.2\nkey b; wait 0.2\nkey b; wait 0.2");
    SetByte(rig, &Commander::missileCount, 1);
    rig.Play("key s; wait 0.2\nkey s; wait 0.2\nkey Down; wait 0.2"); // the last missile sold, then none
    SetByte(rig, &Commander::CargoHold::cargoUsedTonnes, 25);
    rig.Play("key s; wait 0.2"); // too much cargo to sell the bay extension
    SetByte(rig, &Commander::CargoHold::cargoUsedTonnes, 0);
    // None to sell, bought, already fitted, sold.
    rig.Play("key s; wait 0.2\nkey b; wait 0.2\nkey b; wait 0.2\nkey s; wait 0.2\ndigest items\nkey Down; wait 0.2\nkey Down; wait 0.2");
    SetByte(rig, &Commander::laserMountsFitted, 0x0F);
    rig.Play("key b; wait 0.2\nkey Down; wait 0.2\nkey b; wait 0.2"); // four lasers aboard: a pulse laser, and a beam laser
    SetByte(rig, &Commander::laserMountsFitted, 0x01);
    rig.Play("key Down; wait 0.2\nkey b; wait 0.2"); // fuel scoops
    SetByte(rig, &Commander::miningLaserCount, 1);
    rig.Play("key s; wait 0.2"); // not while a mining laser needs them
    SetByte(rig, &Commander::miningLaserCount, 0);
    SetByte(rig, &Commander::fuelScoopsFitted, 0);
    rig.Play("key Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\n"
             "key Down; wait 0.2\nkey b; wait 0.2\ndigest lasers"); // a mining laser needs fuel scoops
    SetCredits(rig, 10);
    // Too little cash for the hyperdrive, round the bottom to a missile, and up to fuel.
    rig.Play("key Up; wait 0.2\nkey b; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\nkey Down; wait 0.2\n"
             "key Down; wait 0.2\nkey b; wait 0.2\nkey Up; wait 0.2");
    SetByte(rig, &Commander::fuel, 100);
    // Too little for fuel, round the top, and the keys the menu ignores.
    rig.Play("key b; wait 0.2\nkey Up; wait 0.2\nkey F4; wait 0.2\nkey x; wait 0.2\nkey Home; wait 0.2");
    SetByte(rig, &Elite::InputState::keyboardPitchRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, &Elite::InputState::keyboardPitchRate, 0xFB);
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
    SetByte(rig, &Elite::InputState::keyboardRollRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, &Elite::InputState::keyboardRollRate, 0xFB);
    rig.Play("wait 0.3\nkey space; wait 0.3\ndigest fitted\nkey s; wait 0.3\nkey space; wait 0.2\nkey Left; wait 0.2\n"
             "key space; wait 0.2\nkey Right; wait 0.2\nkey Right; wait 0.2\nkey x; wait 0.2");
    SetByte(rig, &Elite::InputState::keyboardRollRate, 5);
    rig.Play("wait 0.3");
    SetByte(rig, &Elite::InputState::keyboardRollRate, 0xFB);
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
    SetByte(rig, &Commander::fuelScoopsFitted, 1);
    // All 11 tonnes of food Lave sells, and 7 of textiles, the next row, on the market (D01): + to each quantity, then B.
    const auto more = [](int _times)
    {
      std::string steps;
      for (int time = 0; time < _times; ++time)
      {
        steps += "key equal; wait 0.05\n";
      }
      return steps;
    };
    rig.Play("key F3; wait 0.5\n" + more(10) + "key b; wait 0.05\nkey Down; wait 0.05\n" + more(6) + "key b; wait 0.3\ndigest cargo");
    Assert::AreEqual(std::uint8_t{18}, TwinByte(rig, &Commander::CargoHold::cargoUsedTonnes), L"18 tonnes aboard");
    rig.Play("key F1; wait 1.5\ndigest launched");
    MakeBelowTheNose(rig, Scoopable::MaskingDeviceBarrel);
    rig.Play("wait 0.2\ndigest masking-device");
    Assert::AreEqual(std::uint8_t{1}, TwinByte(rig, &Commander::maskingDeviceRecovered), L"the masking device recovered");
    Assert::IsTrue(TwinMessage(rig) == Elite::Message::MaskingDevice, L"Masking Device!");
    MakeBelowTheNose(rig, Scoopable::Thargon);
    rig.Play("wait 0.2\ndigest alien-items");
    Assert::AreEqual(std::uint8_t{1}, TwinHeld(rig, Elite::COMMODITY_ALIEN_ITEMS), L"a tonne of alien items");
    MakeBelowTheNose(rig, Scoopable::MineralSplinter);
    rig.Play("wait 0.2\ndigest minerals");
    Assert::IsTrue(TwinMessage(rig) == Elite::Message::PreciousMetals, L"precious metals");
    Assert::AreEqual(std::uint8_t{1}, TwinHeld(rig, Elite::COMMODITY_MINERALS), L"a tonne of minerals");
    Assert::AreEqual(std::uint8_t{20}, TwinByte(rig, &Commander::CargoHold::cargoUsedTonnes), L"the hold full");
    MakeBelowTheNose(rig, Scoopable::Barrel);
    rig.Play("wait 0.2\ndigest hold-full");
    Assert::IsTrue(TwinMessage(rig) == Elite::Message::CargoBayFull, L"Cargo Bay Full");
  }
};

} // namespace GameLogicTests
