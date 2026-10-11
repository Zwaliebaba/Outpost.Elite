#include "pch.h"

#include "GameState.h"

#include <memory>
#include <stdexcept>
#include <type_traits>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Byte = Elite::Counted<std::uint8_t>;
using Word = Elite::Counted<std::uint16_t>;

// A copy from one member to another does not compile, nor do compound assignment and increment: the only write is an
// assignment from a value, which counts.
static_assert(!std::is_assignable_v<Byte&, const Byte&>);
static_assert(!std::is_assignable_v<Word&, Word&&>);
static_assert(std::is_assignable_v<Byte&, std::uint8_t> && std::is_assignable_v<Word&, std::uint16_t>);
template <typename Field>
concept CompoundAssignable = requires(Field& _field) { _field += 1; };
template <typename Field>
concept Incrementable = requires(Field& _field) { ++_field; };
template <typename Field>
concept Decrementable = requires(Field& _field) { _field--; };
static_assert(!CompoundAssignable<Byte> && !Incrementable<Byte> && !Decrementable<Word>);

// The image still loads into the struct as bytes.
static_assert(std::is_trivially_copyable_v<Elite::GameData> && std::is_standard_layout_v<Elite::GameData>);
static_assert(alignof(Elite::GameData) == 1 && alignof(Elite::SpaceObject) == 1);

// The 1987 offsets of the members these tests check (Design/Symbols.tsv). The tests read the game's state, and reach a record
// through ObjectAt, at these offsets on purpose rather than at an offset taken from the member: what they check is that the
// original's address still finds a member's bytes where the state holds them now (D38), as the accessors by offset, ObjectAt
// and the loops' turns take it.
constexpr std::uint16_t STARDUST_OFFSET = 0x27F5; // 30 entries of STARDUST_ENTRY_BYTES
constexpr std::size_t STARDUST_ENTRY_BYTES = 6;
constexpr std::uint16_t PLAYER_ENERGY_OFFSET = 0x41F8;
constexpr std::uint16_t PLANET_SLOT_OFFSET = 0x6970;
constexpr std::uint16_t STATION_SLOT_OFFSET = 0x69B0;
constexpr std::uint16_t FIRST_SHIP_SLOT_OFFSET = 0x69F0;
constexpr std::uint16_t DEBRIS_SLOTS_OFFSET = 0x6E30;
constexpr std::uint16_t LAST_SLOT_OFFSET = 0x71F0;
constexpr std::uint16_t FUEL_OFFSET = 0x75E2;
constexpr std::uint16_t KEY_BUFFER_OFFSET = 0xA0B0; // 16 bytes

// A helper that writes a member takes it by reference.
void StoreSeven(Byte& _field)
{
  _field = 7;
}

} // namespace

TEST_CLASS(CountedTests)
{
public:
  // A member's write counts in its GameState's change count when it changes the value, once for a byte or a word, and a write
  // of the value already there counts nothing; the bytes are the 8088's, and the original's address finds them. A GameState on
  // the stack keeps its data segment on the heap, so its members find its count.
  TEST_METHOD(AWriteCountsWhenItChangesTheValue)
  {
    Elite::GameState state;
    Elite::GameData& data = state.Data();
    const std::uint64_t changes = state.ChangeCount();

    data.commander.fuel = 70;
    Assert::AreEqual(changes + 1, state.ChangeCount(), L"a byte that changes");
    data.commander.fuel = 70;
    Assert::AreEqual(changes + 1, state.ChangeCount(), L"the same byte again");
    Assert::AreEqual(70u, std::uint32_t{state.Byte(FUEL_OFFSET)}, L"at its 1987 offset");

    data.playerShip.playerEnergy = 0x1234;
    Assert::AreEqual(changes + 2, state.ChangeCount(), L"a word that changes counts once");
    data.playerShip.playerEnergy = 0x1234;
    Assert::AreEqual(changes + 2, state.ChangeCount(), L"the same word again");
    Assert::AreEqual(0x1234u, std::uint32_t{state.Word(PLAYER_ENERGY_OFFSET)}, L"little-endian, at its 1987 offset");
    const std::size_t energy = GAME_DATA_OFFSET(playerShip.playerEnergy);
    Assert::AreEqual(0x34u, std::uint32_t{state.DataBytes()[energy]}, L"its low byte first in GameData");

    const std::uint32_t read = std::uint32_t{data.commander.fuel} + data.playerShip.playerEnergy;
    Assert::AreEqual(70u + 0x1234u, read);
    Assert::AreEqual(changes + 2, state.ChangeCount(), L"a read counts nothing");

    data.commander.fuel = std::uint8_t{data.commander.legalStatus};
    Assert::AreEqual(changes + 3, state.ChangeCount(), L"a copy from another member, through its value");
  }

  // Every way to reach a member counts: an array's element, a table's, a nested struct's member, a union's, a slot's field by
  // name and through ObjectAt, a gap's byte, a helper's reference, and a byte by the original's address.
  TEST_METHOD(EveryWritePathCounts)
  {
    Elite::GameState state;
    Elite::GameData& data = state.Data();
    std::uint64_t changes = state.ChangeCount();
    const auto counted = [&state, &changes](const wchar_t* _what)
    {
      Assert::AreEqual(changes + 1, state.ChangeCount(), _what);
      changes = state.ChangeCount();
    };

    data.input.keyBuffer[3] = 0x42;
    counted(L"an array's element");
    Assert::AreEqual(0x42u, std::uint32_t{state.Byte(KEY_BUFFER_OFFSET + 3)});
    data.flight.stardust[2][5] = 9;
    counted(L"a table's");
    Assert::AreEqual(9u, std::uint32_t{state.Byte(STARDUST_OFFSET + 2 * STARDUST_ENTRY_BYTES + 5)});
    data.commander.cargoHold.cargoAlloysTonnes = 4;
    counted(L"a nested struct's member");
    data.galaxy.systemSeed0Bytes.systemY = 0x80;
    counted(L"a union's");
    Assert::AreEqual(0x8000u, std::uint32_t{data.galaxy.systemSeed0}, L"in the word's high byte");
    data.world.shipSlots[7].flags = 0x21;
    counted(L"a slot's field");
    const auto ship = static_cast<std::uint16_t>(FIRST_SHIP_SLOT_OFFSET + 4 * Elite::SpaceObject::BYTES);
    Assert::AreEqual(0x21u, std::uint32_t{state.ObjectAt(ship).flags}, L"the same record through ObjectAt");
    state.ObjectAt(STATION_SLOT_OFFSET).yaw = 0x0101;
    counted(L"a record's word through ObjectAt");
    Assert::AreEqual(0x0101u, std::uint32_t{data.world.shipSlots[2].yaw});
    data.drawing.gap0000[63] = 1;
    counted(L"a gap's byte");
    StoreSeven(data.commander.legalStatus);
    counted(L"a helper's reference");
    StoreSeven(data.commander.legalStatus);
    Assert::AreEqual(changes, state.ChangeCount(), L"which stores the value already there");
    state.SetByte(KEY_BUFFER_OFFSET, 5);
    counted(L"a byte by the original's address");
    Assert::AreEqual(5u, std::uint32_t{data.input.keyBuffer[0]}, L"which is the member's");
  }

  // Each GameState counts its own changes, with nothing shared between them: tests run several games in one process.
  TEST_METHOD(EachGameStateCountsItsOwn)
  {
    Elite::GameState first;
    const auto second = std::make_unique<Elite::GameState>();
    const std::uint64_t firstChanges = first.ChangeCount();
    const std::uint64_t secondChanges = second->ChangeCount();
    second->Data().commander.fuel = 1;
    second->Data().world.shipSlots[35].detail = 1;
    Assert::AreEqual(firstChanges, first.ChangeCount(), L"the other's writes are not this one's");
    Assert::AreEqual(secondChanges + 2, second->ChangeCount());
    Assert::AreEqual(0u, std::uint32_t{first.Data().commander.fuel});
  }

  // ObjectAt gives the record at an offset the original holds, and OffsetOf its offset back; a record the state does not hold
  // together, or one past the data segment's end, which the original would wrap at 64 KiB, is refused.
  TEST_METHOD(ObjectAtFindsTheRecordAtAnOffset)
  {
    Elite::GameState state;
    const Elite::GameState& view = state;
    Assert::IsTrue(&state.ObjectAt(STATION_SLOT_OFFSET) == &state.Data().world.shipSlots[2], L"the station's slot");
    Assert::IsTrue(&view.ObjectAt(PLANET_SLOT_OFFSET) == &view.Data().world.shipSlots[1], L"the planet's, read only");
    Assert::AreEqual(std::uint32_t{DEBRIS_SLOTS_OFFSET}, std::uint32_t{state.OffsetOf(state.Data().world.shipSlots[20])},
                     L"an offset back");
    Assert::AreEqual(std::uint32_t{LAST_SLOT_OFFSET}, std::uint32_t{state.OffsetOf(state.ObjectAt(LAST_SLOT_OFFSET))}, L"the last slot");
    Assert::ExpectException<std::out_of_range>([&state] { (void)state.ObjectAt(static_cast<std::uint16_t>(LAST_SLOT_OFFSET + 1)); },
                                               L"one that would run past the slots");
    Assert::ExpectException<std::out_of_range>([&state] { (void)state.ObjectAt(0xFFC1); }, L"one that would wrap");
  }
};

} // namespace GameLogicTests
