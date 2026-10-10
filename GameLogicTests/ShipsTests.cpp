#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Ships.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t IS_OBJECT_NEAR = 0x3B9A;
constexpr std::uint16_t IS_OBJECT_NEAR_KEEP_BLIP = 0x460E;
constexpr std::uint16_t SPAWN_RANDOM_WOLF = 0x4D60;
constexpr std::uint16_t MOVE_OBJECT = 0x4F6E;
constexpr std::uint16_t FIND_FREE_SHIP_SLOT = 0x51E0;
constexpr std::uint16_t FIND_DEBRIS_SLOT = 0x52EC;
constexpr std::uint16_t IS_POLICE_VIPER = 0x541E;

constexpr std::uint8_t ALL_SLOTS = 36;
constexpr std::uint8_t OBJECT_SLOTS = 20;
constexpr std::uint8_t DEBRIS_SLOTS = 16;
constexpr int FIRST_DEBRIS_SLOT = 20;

/// The reference's data segment in a rig, where the slots and variables a routine reads are set up before a call.
class Space
{
public:
  explicit Space(ComparisonRig& _rig) noexcept
    : m_memory(_rig.Host().Ram()),
      m_segment(Elite::DataSegment(_rig.Program()))
  {
  }

  void SetByte(std::uint16_t _offset, std::uint8_t _value) noexcept
  {
    m_memory.Write8(m_segment, _offset, _value);
  }

  void SetWord(std::uint16_t _offset, std::uint16_t _value) noexcept
  {
    m_memory.Write16(m_segment, _offset, _value);
  }

  void Set(Elite::DataField<std::uint8_t> _field, std::uint8_t _value) noexcept
  {
    SetByte(_field.offset, _value);
  }

  /// Every slot cleared, and the slot counts SetUpLocalSpace gives.
  void Clear() noexcept
  {
    for (int offset = 0; offset < ALL_SLOTS * Elite::SLOT_BYTES; ++offset)
      SetByte(static_cast<std::uint16_t>(DS.shipSlots.offset + offset), 0);
    Set(DS.shipSlotCount, ALL_SLOTS);
    Set(DS.objectSlotCount, OBJECT_SLOTS);
    Set(DS.debrisSlotCount, DEBRIS_SLOTS);
  }

  /// NextRandom's next two results.
  void Random(std::uint16_t _first, std::uint16_t _second) noexcept
  {
    SetWord(DS.randomState0.offset, _first);
    SetWord(DS.randomState1.offset, 0);
    SetWord(DS.randomState2.offset, _second);
  }

  /// Slot _index made an active object of _type at the signed position (_x, _y, _z). Out: its offset.
  std::uint16_t Object(int _index, std::uint8_t _type, std::int16_t _x, std::int16_t _y, std::int16_t _z) noexcept
  {
    const auto slot = static_cast<std::uint16_t>(DS.shipSlots.offset + _index * Elite::SLOT_BYTES);
    SetByte(slot, static_cast<std::uint8_t>((_type << 1) | Elite::SLOT_ACTIVE));
    const std::int16_t position[] = {_x, _y, _z};
    for (int axis = 0; axis < 3; ++axis)
    {
      SetWord(static_cast<std::uint16_t>(slot + Elite::SLOT_X + 2 * axis), static_cast<std::uint16_t>(position[axis]));
      SetByte(static_cast<std::uint16_t>(slot + Elite::SLOT_X_HIGH + axis), position[axis] < 0 ? std::uint8_t{0xFF} : std::uint8_t{0});
    }
    return slot;
  }

private:
  Machine::Memory& m_memory;
  std::uint16_t m_segment;
};

} // namespace

// Constructed inputs for the ported ship-slot routines (plan §6.3): the branches the replays do not reach.
TEST_CLASS(ShipsTests)
{
public:
  // Negative words have FFh high bytes; a high byte that disagrees with its word's sign is far, on each axis.
  TEST_METHOD(NearTestsAgreeOnNegativeAndMismatchedHighBytes)
  {
    ComparisonRig rig("ShipsNear");
    Space space(rig);
    space.Clear();
    const std::uint16_t slot = space.Object(4, Elite::TYPE_ASTEROID, -5, -6, -7);
    const std::uint16_t entries[] = {IS_OBJECT_NEAR_KEEP_BLIP, IS_OBJECT_NEAR};
    for (const std::uint16_t entry : entries)
    {
      rig.Call(entry, {.ax = 0x1234, .di = slot});
      for (int axis = 0; axis < 3; ++axis)
      {
        const auto high = static_cast<std::uint16_t>(slot + Elite::SLOT_X_HIGH + axis);
        space.SetByte(high, 0x00);
        rig.Call(entry, {.di = slot});
        space.SetByte(high, 0x02);
        rig.Call(entry, {.di = slot});
        space.SetByte(high, 0xFF);
      }
    }
    rig.AssertAllAgreed(IS_OBJECT_NEAR_KEEP_BLIP, 7);
    rig.AssertAllAgreed(IS_OBJECT_NEAR, 7);
  }

  // A step that carries a coordinate past a signed word takes the object off the scanner and out of play.
  TEST_METHOD(MoveObjectAgreesWhenItRemoves)
  {
    ComparisonRig rig("ShipsMoveObject");
    Space space(rig);
    space.Clear();
    const std::uint16_t slot = space.Object(5, Elite::TYPE_ASTEROID, 0x7FFF, -0x7FFF, 100);
    space.SetByte(static_cast<std::uint16_t>(slot + Elite::SLOT_VELOCITY), 1);
    space.SetByte(static_cast<std::uint16_t>(slot + Elite::SLOT_VELOCITY + 1), 0xFE);
    space.SetByte(static_cast<std::uint16_t>(slot + Elite::SLOT_FLAGS), 0x02);
    space.SetByte(static_cast<std::uint16_t>(slot + Elite::SLOT_SPIN_ROLL), 0x10);
    rig.Call(MOVE_OBJECT, {.di = slot});
    rig.AssertAllAgreed(MOVE_OBJECT, 1);
  }

  // Every ship slot taken, then every debris slot: no free ship slot, and the oldest fragment, the last of equals, reused.
  TEST_METHOD(SlotSearchesAgreeWhenFull)
  {
    ComparisonRig rig("ShipsFull");
    Space space(rig);
    space.Clear();
    for (int index = 0; index < ALL_SLOTS; ++index)
      space.Object(index, Elite::TYPE_SPLINTER, 0, 0, 0);
    constexpr std::uint8_t AGES[DEBRIS_SLOTS] = {3, 9, 1, 9, 0, 7, 9, 2, 5, 5, 4, 8, 0, 6, 1, 2};
    for (int index = 0; index < DEBRIS_SLOTS; ++index)
      space.SetByte(static_cast<std::uint16_t>(DS.shipSlots.offset + (FIRST_DEBRIS_SLOT + index) * Elite::SLOT_BYTES + Elite::SLOT_AGE),
                    AGES[index]);
    rig.Call(FIND_FREE_SHIP_SLOT, {.cx = 0x5555});
    rig.Call(FIND_DEBRIS_SLOT, {.ax = 1, .bx = 2, .cx = 3});
    rig.AssertAllAgreed(FIND_FREE_SHIP_SLOT, 1);
    rig.AssertAllAgreed(FIND_DEBRIS_SLOT, 1);
  }

  // A Viper is police only with word +3Ah = 1.
  TEST_METHOD(IsPoliceViperAgreesOnVipers)
  {
    ComparisonRig rig("ShipsPolice");
    Space space(rig);
    space.Clear();
    const std::uint16_t slot = space.Object(6, Elite::TYPE_VIPER, 0, 0, 0);
    const std::uint16_t owners[] = {1, 0, 0x0101};
    for (const std::uint16_t owner : owners)
    {
      space.SetWord(static_cast<std::uint16_t>(slot + Elite::SLOT_OWNER), owner);
      rig.Call(IS_POLICE_VIPER, {.ax = 0xABCD, .di = slot});
    }
    rig.AssertAllAgreed(IS_POLICE_VIPER, 3);
  }

  // In witch space the wolf is always a Thargoid, which gets 2-5 Thargons.
  TEST_METHOD(SpawnRandomWolfAgreesInWitchSpace)
  {
    ComparisonRig rig("ShipsWolf");
    Space space(rig);
    space.Clear();
    space.Set(DS.witchspaceCountdown, 1);
    const std::uint16_t randoms[] = {0x0000, 0x8F3A, 0xFFFF};
    for (const std::uint16_t random : randoms)
    {
      space.Random(random, static_cast<std::uint16_t>(random ^ 0x5A5A));
      rig.Call(SPAWN_RANDOM_WOLF, {.di = static_cast<std::uint16_t>(DS.shipSlots.offset + 7 * Elite::SLOT_BYTES)});
    }
    rig.AssertAllAgreed(SPAWN_RANDOM_WOLF, 3);
  }
};

} // namespace GameLogicTests
