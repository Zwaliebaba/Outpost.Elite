#include "pch.h"

#include "Galaxy.h"
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

// A helper that writes a member takes it by reference.
void StoreSeven(Byte& _field)
{
  _field = 7;
}

} // namespace

TEST_CLASS(CountedTests)
{
public:
  // A member's write stores its bytes as the 8088's, at the member's place in GameData. A GameState on the stack keeps GameData on the
  // heap.
  TEST_METHOD(AWriteStoresTheBytesAtTheMembersPlace)
  {
    Elite::GameState state;
    Elite::GameData& data = state.Data();

    data.commander.fuel = 70;
    Assert::AreEqual(70u, std::uint32_t{state.DataBytes()[GAME_DATA_OFFSET(commander.fuel)]}, L"at its place in GameData");

    data.playerShip.playerEnergy = 0x1234;
    const std::size_t energy = GAME_DATA_OFFSET(playerShip.playerEnergy);
    Assert::AreEqual(0x34u, std::uint32_t{state.DataBytes()[energy]}, L"its low byte first in GameData");
    Assert::AreEqual(0x12u, std::uint32_t{state.DataBytes()[energy + 1]}, L"then its high byte");

    const std::uint32_t read = std::uint32_t{data.commander.fuel} + data.playerShip.playerEnergy;
    Assert::AreEqual(70u + 0x1234u, read);

    data.commander.legalStatus = 0x40;
    data.commander.fuel = std::uint8_t{data.commander.legalStatus};
    Assert::AreEqual(0x40u, std::uint32_t{state.DataBytes()[GAME_DATA_OFFSET(commander.fuel)]},
                     L"a copy from another member, through its value");
  }

  // Every way to reach a member writes the state's bytes: an array's element, a table's, a nested struct's member, a word's high byte, a
  // slot's field by name and by its number, a buffer's byte, a helper's reference, and video memory.
  TEST_METHOD(EveryWritePathReachesTheState)
  {
    Elite::GameState state;
    Elite::GameData& data = state.Data();
    const auto at = [&state](std::size_t _offset) { return std::uint32_t{state.DataBytes()[_offset]}; };

    data.input.keyBuffer[3] = 0x42;
    Assert::AreEqual(0x42u, at(GAME_DATA_OFFSET(input.keyBuffer) + 3), L"an array's element");
    data.flight.stardust[2][5] = 9;
    Assert::AreEqual(9u, std::uint32_t{data.flight.stardust[2][5]}, L"a table's");
    data.commander.cargoHold.cargoAlloysTonnes = 4;
    Assert::AreEqual(4u, at(GAME_DATA_OFFSET(commander.cargoHold.cargoAlloysTonnes)), L"a nested struct's member");
    data.galaxy.systemSeed0 = Elite::WithHigh(data.galaxy.systemSeed0, 0x80);
    Assert::AreEqual(0x80u, std::uint32_t{Elite::SystemY(state)}, L"a word's high byte, the byte the original names on its own");
    data.world.shipSlots[7].flags = 0x21;
    Assert::AreEqual(0x21u, std::uint32_t{state.Slot(Elite::SlotIndex{7}).flags}, L"a slot's field, the same record by its number");
    state.Slot(Elite::SlotIndex::Station).yaw = 0x0101;
    Assert::AreEqual(0x0101u, std::uint32_t{data.world.shipSlots[2].yaw}, L"a record's word by its number");
    data.drawing.drawingBuffer[63] = 1;
    Assert::AreEqual(1u, at(GAME_DATA_OFFSET(drawing.drawingBuffer) + 63), L"a buffer's byte");
    StoreSeven(data.commander.legalStatus);
    Assert::AreEqual(7u, at(GAME_DATA_OFFSET(commander.legalStatus)), L"a helper's reference");
    state.SetVideoByte(0x10, 0x20);
    Assert::AreEqual(0x20u, std::uint32_t{state.VideoByte(0x10)}, L"a byte of video memory");
  }

  // Each GameState holds its own state, with nothing shared between them: tests run several games in one process.
  TEST_METHOD(EachGameStateHoldsItsOwn)
  {
    Elite::GameState first;
    const auto second = std::make_unique<Elite::GameState>();
    second->Data().commander.fuel = 1;
    second->Data().world.shipSlots[35].detail = 1;
    Assert::AreEqual(0u, std::uint32_t{first.Data().commander.fuel}, L"the other's writes are not this one's");
    Assert::AreEqual(0u, std::uint32_t{first.Data().world.shipSlots[35].detail});
    Assert::AreEqual(1u, std::uint32_t{second->Data().commander.fuel});
  }

  // IndexOf gives a slot's number; a record that is not one of the slots and an offset past video memory are refused (change 3c).
  TEST_METHOD(SlotsAndPlacesAreFoundByMember)
  {
    Elite::GameState state;
    const Elite::GameState& view = state;
    Assert::AreEqual(std::uint32_t{2}, std::uint32_t{Number(state.IndexOf(state.Data().world.shipSlots[2]))}, L"the station's slot");
    Assert::AreEqual(std::uint32_t{35}, std::uint32_t{Number(view.IndexOf(view.Data().world.shipSlots[35]))}, L"the last, read only");
    const Elite::SpaceObject outside{};
    Assert::ExpectException<std::out_of_range>([&state, &outside] { (void)state.IndexOf(outside); }, L"a record that is no slot");
    Assert::ExpectException<std::out_of_range>([&state] { (void)state.Slot(Elite::SlotIndex::End); }, L"a number past the last");

    Assert::ExpectException<std::out_of_range>([&state] { (void)state.VideoByte(0x4000); }, L"past video memory");
  }
};

} // namespace GameLogicTests
