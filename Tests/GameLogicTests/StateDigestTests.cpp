#include "pch.h"

#include "GameRig.h"
#include "Sha256.h"
#include "StateDigest.h"
#include "StateWriter.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The .cdr file's size: the commander's bytes (D78).
constexpr std::size_t COMMANDER_FILE_BYTES = 208;
// The state's bytes as its members write them: what the original data segment's members took, less the joystick's ten (D81), the Amstrad's flag (D12), the
// screenshots' 24 (D86) and the text page's 17, its attribute and the working texts the status and price screens printed (ADR-069).
constexpr std::size_t STATE_BYTES = 14'322;

// A byte unlike its neighbours for every place in a struct of _bytes bytes: what fills one before it is written by its members.
[[nodiscard]] std::vector<std::uint8_t> Pattern(std::size_t _bytes)
{
  std::vector<std::uint8_t> pattern(_bytes);
  for (std::size_t index = 0; index < _bytes; ++index)
  {
    pattern[index] = static_cast<std::uint8_t>(index * 31 + (index >> 8) * 7 + 1);
  }
  return pattern;
}

} // namespace

TEST_CLASS(StateDigestTests)
{
public:
  // The state written by its members (StateWriter, D76) is 14,322 bytes, every member once, in its order, at its width,
  // whatever the structs' layout. Read back from bytes and written again, the state gives the same bytes.
  TEST_METHOD(TheStateWrittenByItsMembersIsItsBytes)
  {
    const std::vector<std::uint8_t> pattern = Pattern(STATE_BYTES);
    const auto data = std::make_unique<Elite::GameData>();
    Assert::AreEqual(STATE_BYTES, Elite::ReadStateBytes(*data, pattern), L"every byte read");
    Assert::IsTrue(std::ranges::equal(Elite::StateBytes(*data), pattern), L"and written by its members, the same bytes");
    const auto read = std::make_unique<Elite::GameData>();

    // The commander file (D78) is the commander's 208 bytes.
    Assert::AreEqual(COMMANDER_FILE_BYTES, Elite::StateBytes(data->commander).size(), L"the .cdr file's size");

    // A shorter list of bytes than the state is an error.
    const std::span<const std::uint8_t> shorter(pattern.data(), pattern.size() - 1);
    Assert::ExpectException<std::out_of_range>([&read, shorter]() { (void)Elite::ReadStateBytes(*read, shorter); }, L"too few bytes");
  }

  // What the digest covers (StateDigest.h): every byte of the game's state, from GameData's first group to its last, and the
  // display's colour select. The original's stack is not in the state at all.
  TEST_METHOD(CoversTheGameAndNotItsScratch)
  {
    GameRig rig("Digest");
    Elite::GameState& state = rig.State();
    Elite::Display& display = rig.Host().Screen();
    const std::string before = Elite::GameStateDigest(state, display);

    state.Data().ai.activeObjectCount = static_cast<std::uint8_t>(state.Data().ai.activeObjectCount ^ 0x55);
    const std::string first = Elite::GameStateDigest(state, display);
    Assert::AreNotEqual(before.c_str(), first.c_str(), false, L"the state counts, its first group");

    state.Data().commander.hyperspaceTargetIndex = static_cast<std::uint8_t>(state.Data().commander.hyperspaceTargetIndex ^ 0x55);
    const std::string last = Elite::GameStateDigest(state, display);
    Assert::AreNotEqual(first.c_str(), last.c_str(), false, L"and its last");

    display.SetColorSelect(static_cast<std::uint8_t>(display.ColorSelect() ^ 0x01));
    Assert::AreNotEqual(last.c_str(), Elite::GameStateDigest(state, display).c_str(), false, L"and the colour select");
  }

  // The GameState owns the game's memory (D19), started as SetStartState sets it: the
  // state the original's image loaded, whose hash this was measured as when the image still loaded it (Phase 7, step 4), less
  // the members since removed; a write to a member lands there.
  TEST_METHOD(TheGameStateHoldsTheGamesMemory)
  {
    GameRig rig("GameState");
    Elite::GameState& state = rig.State();
    // The state as the original's image loaded it, less the joystick's members (D81), the Amstrad's flag (D12), the screenshots' (D86) and the text page's (ADR-069).
    Elite::Sha256 start;
    start.Update(Elite::StateBytes(std::as_const(state).Data()));
    Assert::AreEqual("cb198987133eb299765a5787b43d338a500fe36d923c0cb49dda403e581196f7", Elite::Sha256::ToHex(start.Finish()).c_str(),
                     L"the start state, as the image loaded it");

    const std::uint8_t fuel = state.Data().commander.fuel;
    state.Data().commander.fuel = static_cast<std::uint8_t>(fuel ^ 0xFF);
    Assert::AreEqual(std::uint32_t{fuel ^ 0xFFu}, std::uint32_t{std::as_const(state).Data().commander.fuel}, L"the member is the state's");
  }
};

} // namespace GameLogicTests
