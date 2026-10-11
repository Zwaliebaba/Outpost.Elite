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

// The offset of the last object record that ends inside the data segment.
constexpr auto LAST_RECORD = static_cast<std::uint16_t>(0x10000 - Elite::SpaceObject::BYTES);

// The 1987 offsets of the members these tests check (Design/Symbols.tsv). The tests read the data segment, and reach a record
// through ObjectAt, at these offsets on purpose rather than at an offset taken from the member: what they check is that a
// member keeps its bytes where the original kept them, in GameState::DataBytes(), which the state digests hash and whose
// offsets ObjectAt takes.
constexpr std::uint16_t STARDUST_OFFSET = 0x27F5; // 30 entries of STARDUST_ENTRY_BYTES
constexpr std::size_t STARDUST_ENTRY_BYTES = 6;
constexpr std::uint16_t PLAYER_ENERGY_OFFSET = 0x41F8;
constexpr std::uint16_t PLANET_SLOT_OFFSET = 0x6970;
constexpr std::uint16_t STATION_SLOT_OFFSET = 0x69B0;
constexpr std::uint16_t FIRST_SHIP_SLOT_OFFSET = 0x69F0;
constexpr std::uint16_t DEBRIS_SLOTS_OFFSET = 0x6E30;
constexpr std::uint16_t FUEL_OFFSET = 0x75E2;
constexpr std::uint16_t NOTE_PERIODS_OFFSET = 0xA158; // 46 words

// The word at _offset of _state's data segment, little-endian as the 8088 keeps it.
[[nodiscard]] std::uint32_t WordAt(const Elite::GameState& _state, std::size_t _offset)
{
  return _state.DataBytes()[_offset] | (std::uint32_t{_state.DataBytes()[_offset + 1]} << 8);
}

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
  // of the value already there counts nothing; the bytes are the 8088's, at the member's offset. A GameState on the stack keeps
  // its data segment on the heap, so its members find its count.
  TEST_METHOD(AWriteCountsWhenItChangesTheValue)
  {
    Elite::GameState state;
    Elite::GameData& data = state.Data();
    const std::uint64_t changes = state.ChangeCount();

    data.commanderBlock.fuel = 70;
    Assert::AreEqual(changes + 1, state.ChangeCount(), L"a byte that changes");
    data.commanderBlock.fuel = 70;
    Assert::AreEqual(changes + 1, state.ChangeCount(), L"the same byte again");
    Assert::AreEqual(70u, std::uint32_t{state.DataBytes()[FUEL_OFFSET]}, L"at its offset");

    data.playerEnergy = 0x1234;
    Assert::AreEqual(changes + 2, state.ChangeCount(), L"a word that changes counts once");
    data.playerEnergy = 0x1234;
    Assert::AreEqual(changes + 2, state.ChangeCount(), L"the same word again");
    Assert::AreEqual(0x1234u, WordAt(state, PLAYER_ENERGY_OFFSET), L"little-endian, at its offset");

    const std::uint32_t read = std::uint32_t{data.commanderBlock.fuel} + data.playerEnergy;
    Assert::AreEqual(70u + 0x1234u, read);
    Assert::AreEqual(changes + 2, state.ChangeCount(), L"a read counts nothing");

    data.commanderBlock.fuel = std::uint8_t{data.commanderBlock.legalStatus};
    Assert::AreEqual(changes + 3, state.ChangeCount(), L"a copy from another member, through its value");
  }

  // Every way to reach a member counts: an array's element, a table's, a nested struct's member, a union's, a slot's field by
  // name and through ObjectAt, a gap's byte, and a helper's reference.
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

    data.notePeriods[3] = 0x0102;
    counted(L"an array's element");
    Assert::AreEqual(0x0102u, WordAt(state, NOTE_PERIODS_OFFSET + 3 * sizeof(std::uint16_t)));
    data.stardust[2][5] = 9;
    counted(L"a table's");
    Assert::AreEqual(9u, std::uint32_t{state.DataBytes()[STARDUST_OFFSET + 2 * STARDUST_ENTRY_BYTES + 5]});
    data.commanderBlock.cargoHold.cargoAlloysTonnes = 4;
    counted(L"a nested struct's member");
    data.systemSeed0Bytes.systemY = 0x80;
    counted(L"a union's");
    Assert::AreEqual(0x8000u, std::uint32_t{data.systemSeed0}, L"in the word's high byte");
    data.shipSlots.firstShipSlot[4].flags = 0x21;
    counted(L"a slot's field");
    const auto ship = static_cast<std::uint16_t>(FIRST_SHIP_SLOT_OFFSET + 4 * Elite::SpaceObject::BYTES);
    Assert::AreEqual(0x21u, std::uint32_t{state.ObjectAt(ship).flags}, L"the same record through ObjectAt");
    state.ObjectAt(STATION_SLOT_OFFSET).yaw = 0x0101;
    counted(L"a record's word through ObjectAt");
    Assert::AreEqual(0x0101u, std::uint32_t{data.shipSlots.stationSlot.yaw});
    data.gap0000[63] = 1;
    counted(L"a gap's byte");
    StoreSeven(data.commanderBlock.legalStatus);
    counted(L"a helper's reference");
    StoreSeven(data.commanderBlock.legalStatus);
    Assert::AreEqual(changes, state.ChangeCount(), L"which stores the value already there");
  }

  // Each GameState counts its own changes, with nothing shared between them: tests run several games in one process.
  TEST_METHOD(EachGameStateCountsItsOwn)
  {
    Elite::GameState first;
    const auto second = std::make_unique<Elite::GameState>();
    const std::uint64_t firstChanges = first.ChangeCount();
    const std::uint64_t secondChanges = second->ChangeCount();
    second->Data().commanderBlock.fuel = 1;
    second->Data().shipSlots.debrisSlots[15].detail = 1;
    Assert::AreEqual(firstChanges, first.ChangeCount(), L"the other's writes are not this one's");
    Assert::AreEqual(secondChanges + 2, second->ChangeCount());
    Assert::AreEqual(0u, std::uint32_t{first.Data().commanderBlock.fuel});
  }

  // ObjectAt gives the record at an offset the original holds, and OffsetOf its offset back; a record past the data segment's
  // end, which the original would wrap at 64 KiB, is refused.
  TEST_METHOD(ObjectAtFindsTheRecordAtAnOffset)
  {
    Elite::GameState state;
    const Elite::GameState& view = state;
    Assert::IsTrue(&state.ObjectAt(STATION_SLOT_OFFSET) == &state.Data().shipSlots.stationSlot, L"the station's slot");
    Assert::IsTrue(&view.ObjectAt(PLANET_SLOT_OFFSET) == &view.Data().shipSlots.planetSlot, L"the planet's, read only");
    Assert::AreEqual(std::uint32_t{DEBRIS_SLOTS_OFFSET}, std::uint32_t{state.OffsetOf(state.Data().shipSlots.debrisSlots[0])},
                     L"an offset back");
    Assert::AreEqual(std::uint32_t{LAST_RECORD}, std::uint32_t{state.OffsetOf(state.ObjectAt(LAST_RECORD))}, L"the last record that fits");
    Assert::ExpectException<std::out_of_range>([&state] { (void)state.ObjectAt(static_cast<std::uint16_t>(LAST_RECORD + 1)); },
                                               L"one that would wrap");
  }
};

} // namespace GameLogicTests
