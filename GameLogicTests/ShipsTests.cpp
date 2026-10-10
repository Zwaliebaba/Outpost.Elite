#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "Ships.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t CLEAR_OBJECT_SLOT = 0x2FD8;
constexpr std::uint16_t IS_OBJECT_NEAR = 0x3B9A;
constexpr std::uint16_t IS_OBJECT_NEAR_KEEP_BLIP = 0x460E;
constexpr std::uint16_t INIT_POLICE_VIPER = 0x4C76;
constexpr std::uint16_t INIT_ABANDONED_COBRA = 0x4CA6;
constexpr std::uint16_t INIT_ESCAPE_POD = 0x4CB3;
constexpr std::uint16_t INIT_SHUTTLE = 0x4CC0;
constexpr std::uint16_t INIT_KRAIT_HUNTER = 0x4CCD;
constexpr std::uint16_t INIT_THARGON = 0x4CDA;
constexpr std::uint16_t SPAWN_RANDOM_DRIFTER = 0x4CE7;
constexpr std::uint16_t SPAWN_RANDOM_TRADER = 0x4D08;
constexpr std::uint16_t SPAWN_RANDOM_WOLF = 0x4D60;
constexpr std::uint16_t SPAWN_MASK_MISSION_SHIP = 0x4DAE;
constexpr std::uint16_t SPAWN_INVASION_THARGOID = 0x4DF0;
constexpr std::uint16_t RANDOMIZE_ORIENTATION = 0x4F35;
constexpr std::uint16_t MOVE_OBJECT = 0x4F6E;
constexpr std::uint16_t FIND_FREE_SHIP_SLOT = 0x51E0;
constexpr std::uint16_t RECLAIM_SHIP_SLOT = 0x51FD;
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

  // A slot full of bytes cleared; and each fixed record made in a slot that held something else.
  TEST_METHOD(ClearObjectSlotAndTheFixedRecordsAgree)
  {
    ComparisonRig rig("ShipsFixedRecords");
    Space space(rig);
    space.Clear();
    const auto slot = static_cast<std::uint16_t>(DS.shipSlots.offset + 8 * Elite::SLOT_BYTES);
    for (std::uint16_t offset = 0; offset < Elite::SLOT_BYTES; ++offset)
      space.SetByte(static_cast<std::uint16_t>(slot + offset), static_cast<std::uint8_t>(0xA5 ^ offset));
    rig.Call(CLEAR_OBJECT_SLOT, {.cx = 0x9999, .si = slot, .di = 0x1234});
    const std::uint16_t entries[] = {INIT_POLICE_VIPER, INIT_ABANDONED_COBRA, INIT_ESCAPE_POD,
                                     INIT_SHUTTLE,      INIT_KRAIT_HUNTER,    INIT_THARGON};
    for (const std::uint16_t entry : entries)
    {
      space.Object(8, Elite::TYPE_ASP, 0x123, -0x456, 0x789);
      rig.Call(entry, {.ax = 0x4321, .bx = 0x8765, .cx = 0x1111, .di = slot});
    }
    rig.AssertAllAgreed(CLEAR_OBJECT_SLOT, 1);
    for (const std::uint16_t entry : entries)
      rig.AssertAllAgreed(entry, 1);
  }

  // Each of the drifters' and the traders' records at random, a Viper as police and not, the mask mission's three types, the
  // invasion's Thargoid and a random orientation.
  TEST_METHOD(SpawnsAgreeOnEveryRecord)
  {
    ComparisonRig rig("ShipsSpawns");
    Space space(rig);
    space.Clear();
    space.Set(DS.legalStatus, 0x23);
    const auto slot = static_cast<std::uint16_t>(DS.shipSlots.offset + 7 * Elite::SLOT_BYTES);
    const std::uint16_t drifters[] = {0x0000, 0x0003, 0x1234, 0x8007, 0xFFFF};
    for (const std::uint16_t random : drifters)
    {
      space.Random(random, static_cast<std::uint16_t>(random ^ 0x0F0F));
      rig.Call(SPAWN_RANDOM_DRIFTER, {.di = slot});
    }
    // The record is the first random byte / 43: from D7h, a Viper, police when that first word is odd (the fifth is 6s + 3f).
    const std::uint16_t traders[] = {0x0010, 0x0056, 0x00D7, 0x00D8, 0x00FF};
    for (const std::uint16_t random : traders)
    {
      space.Random(random, static_cast<std::uint16_t>(random * 3));
      rig.Call(SPAWN_RANDOM_TRADER, {.di = slot});
    }
    Machine::Registers& regs = rig.Host().Processor().Regs();
    const std::uint16_t flags = regs.flags;
    for (const std::uint16_t random : {std::uint16_t{0x1234}, std::uint16_t{0x9234}})
    {
      for (const bool maskShip : {true, false})
      {
        regs.flags = static_cast<std::uint16_t>(maskShip ? (flags | Machine::FLAG_CARRY) : (flags & ~Machine::FLAG_CARRY));
        space.Random(random, static_cast<std::uint16_t>(~random));
        rig.Call(SPAWN_MASK_MISSION_SHIP, {.ax = 0x5555, .di = slot});
      }
    }
    regs.flags = flags;
    space.Random(0x2222, 0x3333);
    rig.Call(SPAWN_INVASION_THARGOID, {.di = slot});
    space.Random(0x4444, 0x5555);
    rig.Call(RANDOMIZE_ORIENTATION, {.ax = 0x1, .di = slot});
    rig.AssertAllAgreed(SPAWN_RANDOM_DRIFTER, std::size(drifters));
    rig.AssertAllAgreed(SPAWN_RANDOM_TRADER, std::size(traders));
    rig.AssertAllAgreed(SPAWN_MASK_MISSION_SHIP, 4);
    rig.AssertAllAgreed(SPAWN_INVASION_THARGOID, 1);
    rig.AssertAllAgreed(RANDOMIZE_ORIENTATION, 1);
  }

  // The first ship slot without a blip; or, when every one has a blip, one of slots 4-19 removed at random.
  TEST_METHOD(ReclaimShipSlotAgreesWithAndWithoutABlipFree)
  {
    ComparisonRig rig("ShipsReclaim");
    Space space(rig);
    space.Clear();
    for (int index = 3; index < OBJECT_SLOTS; ++index)
    {
      const std::uint16_t slot = space.Object(index, Elite::TYPE_ASTEROID, static_cast<std::int16_t>(0x40 * index), 0, 0x400);
      space.SetByte(static_cast<std::uint16_t>(slot + Elite::SLOT_FLAGS), 0x02);
    }
    space.SetByte(static_cast<std::uint16_t>(DS.shipSlots.offset + 12 * Elite::SLOT_BYTES + Elite::SLOT_FLAGS), 0x01);
    rig.Call(RECLAIM_SHIP_SLOT, {.ax = 0x7777, .bx = 0x1, .cx = 0x2, .dx = 0x3, .di = 0x4});
    space.SetByte(static_cast<std::uint16_t>(DS.shipSlots.offset + 12 * Elite::SLOT_BYTES + Elite::SLOT_FLAGS), 0x03);
    for (const std::uint16_t random : {std::uint16_t{0x0000}, std::uint16_t{0x0F00}, std::uint16_t{0xF5FF}})
    {
      space.Random(random, 0);
      rig.Call(RECLAIM_SHIP_SLOT, {.ax = 0x7777, .bx = 0x1, .cx = 0x2, .dx = 0x3, .di = 0x4});
    }
    rig.AssertAllAgreed(RECLAIM_SHIP_SLOT, 4);
  }
};

} // namespace GameLogicTests
