#include "pch.h"

#include "ReferenceRig.h"
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
// The state's bytes as its members write them: what the 1987 data segment's members took, less the joystick's ten (D81), the Amstrad's flag (D12) and the
// screenshots' 24 (D86).
constexpr std::size_t STATE_BYTES = 14'340;

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
  // The state written by its members (StateWriter, D76) is 14,409 bytes, every member once, in its order, at its width,
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

  // What the digest covers (StateDigest.h): every byte of the game's state, from GameData's first group to its last, video memory and the
  // CGA's registers. The original's stack is not in the state at all.
  TEST_METHOD(CoversTheGameAndNotItsScratch)
  {
    ReferenceRig rig("Digest");
    Elite::GameState& state = rig.State();
    Elite::Cga& video = rig.Host().Video();
    const std::string before = Elite::GameStateDigest(state, video);

    state.Data().ai.activeObjectCount = static_cast<std::uint8_t>(state.Data().ai.activeObjectCount ^ 0x55);
    const std::string first = Elite::GameStateDigest(state, video);
    Assert::AreNotEqual(before.c_str(), first.c_str(), false, L"the state counts, its first group");

    state.Data().commander.hyperspaceTargetIndex = static_cast<std::uint8_t>(state.Data().commander.hyperspaceTargetIndex ^ 0x55);
    const std::string last = Elite::GameStateDigest(state, video);
    Assert::AreNotEqual(first.c_str(), last.c_str(), false, L"and its last");

    state.SetVideoByte(0x3FFF, 0x55);
    const std::string videoMemory = Elite::GameStateDigest(state, video);
    Assert::AreNotEqual(last.c_str(), videoMemory.c_str(), false, L"and video memory");

    video.SetColorSelect(static_cast<std::uint8_t>(video.ColorSelect() ^ 0x01));
    Assert::AreNotEqual(videoMemory.c_str(), Elite::GameStateDigest(state, video).c_str(), false, L"and the CGA's registers");
  }

  // The GameState owns the game's memory (D19), started at the segments DOS would have used (D33) as SetStartState sets it: the
  // state the 1987 image loaded, whose hash this was measured as when the image still loaded it (Phase 7, step 4); a write to a member
  // or to video memory lands there, and an offset past video memory is refused.
  TEST_METHOD(TheGameStateHoldsTheGamesMemory)
  {
    ReferenceRig rig("GameState");
    Elite::GameState& state = rig.State();
    Assert::AreEqual(std::uint32_t{Elite::PSP_SEGMENT + Elite::PSP_PARAGRAPHS}, std::uint32_t{state.CodeSegment()}, L"the code segment");
    Assert::AreEqual(std::uint32_t{state.CodeSegment()} + Elite::DATA_SEGMENT_PARAGRAPH, std::uint32_t{state.DataSegment()},
                     L"the data segment");

    // The state as the 1987 image loaded it, less the joystick's members (D81), the Amstrad's flag (D12) and the screenshots' (D86), and video memory as mode 3 leaves it.
    Elite::Sha256 start;
    start.Update(Elite::StateBytes(std::as_const(state).Data()));
    Assert::AreEqual("2cd124d640cc79dfd6d6c65508f90b0e5c05646aa308ef7651cf45ebc767877d", Elite::Sha256::ToHex(start.Finish()).c_str(),
                     L"the start state, as the image loaded it");
    for (std::uint16_t offset = 0; offset < Elite::Cga::VIDEO_MEMORY_BYTES; offset += 2)
    {
      if (state.VideoByte(offset) != 0x20 || state.VideoByte(static_cast<std::uint16_t>(offset + 1)) != 0x07)
        Assert::Fail(L"video memory as video mode 3 leaves it: 0720h words");
    }

    const std::uint8_t fuel = state.Data().commander.fuel;
    state.Data().commander.fuel = static_cast<std::uint8_t>(fuel ^ 0xFF);
    Assert::AreEqual(std::uint32_t{fuel ^ 0xFFu}, std::uint32_t{std::as_const(state).Data().commander.fuel}, L"the member is the state's");

    state.SetVideoWord(0x10, 0xF820);
    Assert::AreEqual(0x20u, std::uint32_t{state.VideoByte(0x10)}, L"a word into video memory, low byte first");
    Assert::AreEqual(0xF8u, std::uint32_t{state.VideoByte(0x11)}, L"then its high byte");

    Assert::ExpectException<std::out_of_range>([&state] { state.SetVideoByte(Elite::Cga::VIDEO_MEMORY_BYTES, 1); }, L"past video memory");
  }
};

} // namespace GameLogicTests
