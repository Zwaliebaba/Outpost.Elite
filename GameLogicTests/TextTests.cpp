#include "pch.h"

#include "Combat.h"
#include "DataOverlay.h"
#include "GameState.h"
#include "Maths.h"
#include "ObjectSlot.h"
#include "Ships.h"
#include "TwinRig.h"

#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

// The spawners the game makes ships with outside witch space, each picking one of its records by its first random draw.
enum class Spawner : std::uint8_t
{
  Drifter, // SpawnRandomDrifter
  Trader,  // SpawnRandomTrader
  Hunter,  // SpawnRandomHunter
  Wolf     // SpawnRandomWolf
};

// Where shipIdentityText's class name goes, and its length: "CLASS: " comes first.
constexpr std::uint16_t IDENTITY_CLASS_OFFSET = 7;
constexpr std::size_t IDENTITY_CLASS_BYTES = 7;
// How far ahead a ship to identify is put: a second or two before one flying at the player reaches it.
constexpr std::uint16_t IDENTIFY_AHEAD = 0x600;
// LaunchShipFromObject's kind for a missile at the player, as TryLaunchMissileAtPlayer launches one.
constexpr std::uint8_t MISSILE_LAUNCH = 0x14;
// The draws a spawner's record is looked for in: every record comes up far sooner.
constexpr int MOST_DRAWS = 1000;

// The record _spawner's first draw picks when NextRandom gives _random: a random byte over 37, 43 or 52 (hunters, traders,
// wolves), or a drifter's, the word rotated right and its two bytes XORed, of eight.
[[nodiscard]] std::uint8_t PickedRecord(Spawner _spawner, std::uint16_t _random) noexcept
{
  const auto low = static_cast<std::uint8_t>(_random & 0xFF);
  switch (_spawner)
  {
  case Spawner::Trader:
    return static_cast<std::uint8_t>(low / 43);
  case Spawner::Hunter:
    return static_cast<std::uint8_t>(low / 37);
  case Spawner::Wolf:
    return static_cast<std::uint8_t>(low / 52);
  case Spawner::Drifter:
    break;
  }
  const auto rotated = static_cast<std::uint16_t>((_random >> 1) | (_random << 15));
  return static_cast<std::uint8_t>(((rotated & 0xFF) ^ (rotated >> 8)) & 7);
}

// A ship of record _record of _spawner's, in the first free ship slot of the twin, made by the game's own spawner, with the
// random state drawn on by NextRandom until the spawner's first draw picks that record, as some later moment of play would have
// it; then put _ahead straight ahead of the player, where a ship flying at the player passes, and turned to the player and set
// going (FacePlayer, ComputeVelocity), as a spawner turns and sets going a ship it has placed. Returns the slot.
std::uint16_t SpawnAhead(TwinRig& _rig, Spawner _spawner, std::uint8_t _record, std::uint16_t _ahead)
{
  std::uint16_t placed = 0;
  _rig.Both(
    [&placed, _spawner, _record, _ahead](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      for (int draws = 0;; ++draws)
      {
        // NextRandom's next result is the sum of the first two words of the state.
        const auto next = static_cast<std::uint16_t>(state.Get(DS.randomState0) + state.Get(DS.randomState1));
        if (PickedRecord(_spawner, next) == _record)
          break;
        Assert::IsTrue(draws < MOST_DRAWS, L"the spawner's record comes up");
        (void)Elite::NextRandom(state);
      }
      const Elite::SlotSearch free = Elite::FindFreeShipSlot(state);
      Assert::IsTrue(free.found, L"a free ship slot");
      Elite::ObjectSlot ship(state, free.slot);
      switch (_spawner)
      {
      case Spawner::Drifter:
        Elite::SpawnRandomDrifter(state, ship);
        break;
      case Spawner::Trader:
        (void)Elite::SpawnRandomTrader(state, ship);
        break;
      case Spawner::Hunter:
        Elite::SpawnRandomHunter(state, ship);
        break;
      case Spawner::Wolf:
        Elite::SpawnRandomWolf(state, ship);
        break;
      }
      ship.Set(Elite::SlotWord::X, 0);
      ship.Set(Elite::SlotWord::Y, 0);
      ship.Set(Elite::SlotWord::Z, _ahead);
      ship.Set(Elite::SlotWord::XYHigh, 0);
      ship.Set(Elite::SlotByte::ZHigh, 0);
      (void)Elite::FacePlayer(state, ship);
      (void)Elite::ComputeVelocity(state, ship);
      placed = free.slot;
    });
  return placed;
}

// The ship at _slot gone from the twin, as RemoveObject takes one out of play: its active bit cleared and its blip erased.
void RemoveShip(TwinRig& _rig, std::uint16_t _slot)
{
  _rig.Both(
    [_slot](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      (void)Elite::RemoveObject(state, Elite::ObjectSlot(state, _slot));
    });
}

// A missile launched at the player by the ship at _launcher in the twin, as TryLaunchMissileAtPlayer launches one
// (LaunchShipFromObject): a copy of the ship in a free slot, made a missile and moved three frames on. Returns its slot.
std::uint16_t LaunchMissileAtPlayer(TwinRig& _rig, std::uint16_t _launcher)
{
  std::uint16_t launched = 0;
  _rig.Both(
    [&launched, _launcher](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      const std::optional<std::uint16_t> slot =
        Elite::LaunchShipFromObject(state, Elite::ObjectSlot(state, _launcher), MISSILE_LAUNCH, false);
      Assert::IsTrue(slot.has_value(), L"a free slot for the missile");
      launched = slot.value_or(0);
    });
  return launched;
}

// The class name ShowShipIdentity put in shipIdentityText, as the twin has it.
[[nodiscard]] std::string IdentifiedClass(TwinRig& _rig)
{
  std::string name;
  _rig.Both(
    [&name](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      name.clear();
      for (std::size_t index = 0; index < IDENTITY_CLASS_BYTES; ++index)
        name += static_cast<char>(_pc.Ram().Read8(Elite::DataSegment(_program),
                                                  static_cast<std::uint16_t>(DS.shipIdentityText.offset + IDENTITY_CLASS_OFFSET + index)));
    });
  return name;
}

// I, held until the ship in the sights is identified (HandleIdentifyKey looks each frame until one is), digested as _label; its
// class name checked; then U, which unarms the missile I locked on it, so that the next I is read.
void Identify(TwinRig& _rig, std::string_view _label, std::string_view _className)
{
  _rig.Play("down i; wait 0.1; up i; wait 0.2\ndigest " + std::string(_label));
  const std::string identified = IdentifiedClass(_rig);
  Assert::AreEqual(std::wstring(_className.begin(), _className.end()), std::wstring(identified.begin(), identified.end()),
                   std::wstring(_label.begin(), _label.end()).c_str());
  _rig.Play("down u; wait 0.1; up u; wait 0.1");
}

} // namespace

// Twins for a text line's editing and the ship identity's labels, which no replay shows (ADR-016).
TEST_CLASS(TextTests)
{
public:
  // A line typed on the galactic chart's F: a capital with Shift, one character more than fits, every one
  // deleted and one Backspace more, then Enter while the cursor shows.
  TEST_METHOD(ReadTextLineAgreesWhileEditing)
  {
    TwinRig rig("TwinReadTextLine");
    rig.Play("key space; wait 4\nkey F5; wait 0.3\nkey f; wait 0.1\n"
             "down Shift_L; wait 0.02; key l; wait 0.02; up Shift_L; wait 0.02\n"
             "key a; key v; key e; key q; wait 0.02; key q; key q; key q; key q; wait 0.05\n"
             "key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace; wait 0.02\n"
             "key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace; wait 0.05\n"
             "key Return; wait 0.1\nkey Escape; wait 0.3\ndigest closed");
  }

  // ShowShipIdentity's labels other than Debris (ADR-008 item 8, D20): one ship of each class it names, each made by the game's
  // own spawner, put ahead of the player launched from Lave, and identified with I. A trader; a Hermit, the trader that is a
  // rock; a Viper, Police whatever its class; a hunter; a Shuttle, Simple, of class 3 but no rock; a wolf; and the missile the
  // wolf launches at the player, Attack. Not None: class 0 is only the Cobra an escape capsule leaves, and while the capsule
  // flies ProcessFlightKeys reads no key; nor Station, which HandleIdentifyKey passes over.
  TEST_METHOD(ShipIdentityNamesEveryClass)
  {
    TwinRig rig("TwinShipIdentity");
    rig.Play("key space; wait 4\nkey F1; wait 1.5\ndigest launched");
    struct Named
    {
      Spawner spawner;
      std::uint8_t record;
      std::string_view label;
      std::string_view className;
    };
    const Named ships[] = {{Spawner::Trader, 0, "trader", "Trader "},
                           {Spawner::Trader, 4, "hermit", "Hermit "},
                           {Spawner::Trader, 5, "police", "Police "},
                           {Spawner::Hunter, 0, "hunter", "Hunter "},
                           {Spawner::Drifter, 6, "simple", "Simple "}};
    for (const Named& ship : ships)
    {
      const std::uint16_t slot = SpawnAhead(rig, ship.spawner, ship.record, IDENTIFY_AHEAD);
      Identify(rig, ship.label, ship.className);
      RemoveShip(rig, slot);
    }
    const std::uint16_t wolf = SpawnAhead(rig, Spawner::Wolf, 0, IDENTIFY_AHEAD);
    Identify(rig, "wolf", "Wolf   ");
    const std::uint16_t missile = LaunchMissileAtPlayer(rig, wolf);
    // A frame for the missile to be drawn, which FindShipInCrosshairs needs; I is read before the frame's drawing.
    rig.Play("wait 0.1");
    Identify(rig, "attack", "Attack ");
    RemoveShip(rig, missile);
    RemoveShip(rig, wolf);
    rig.Play("wait 0.1");
  }
};

} // namespace GameLogicTests
