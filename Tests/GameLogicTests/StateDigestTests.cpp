#include "pch.h"

#include "ReferenceRig.h"
#include "StateDigest.h"

#include <algorithm>
#include <stdexcept>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

TEST_CLASS(StateDigestTests)
{
public:
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

    state.Data().unused.dockingDeniedMessage[0] = static_cast<std::uint8_t>(state.Data().unused.dockingDeniedMessage[0] ^ 0x55);
    const std::string last = Elite::GameStateDigest(state, video);
    Assert::AreNotEqual(first.c_str(), last.c_str(), false, L"and its last");

    state.SetVideoByte(0x3FFF, 0x55);
    const std::string videoMemory = Elite::GameStateDigest(state, video);
    Assert::AreNotEqual(last.c_str(), videoMemory.c_str(), false, L"and video memory");

    video.SetColorSelect(static_cast<std::uint8_t>(video.ColorSelect() ^ 0x01));
    Assert::AreNotEqual(videoMemory.c_str(), Elite::GameStateDigest(state, video).c_str(), false, L"and the CGA's registers");
  }

  // The GameState owns the game's memory (D19), loaded from the checked-in image at the segments DOS would have used (D33): the load's
  // bytes are its own, a write to a member or to video memory is counted once when it changes a byte, a read counts nothing, and an offset
  // past video memory is refused. Nothing reaches the state by the original's addresses (change 3c).
  TEST_METHOD(TheGameStateHoldsTheGamesMemory)
  {
    ReferenceRig rig("GameState");
    Elite::GameState& state = rig.State();
    Assert::AreEqual(std::uint32_t{Elite::PSP_SEGMENT + Elite::PSP_PARAGRAPHS}, std::uint32_t{state.CodeSegment()}, L"the code segment");
    Assert::AreEqual(std::uint32_t{state.CodeSegment()} + Elite::DATA_SEGMENT_PARAGRAPH, std::uint32_t{state.DataSegment()},
                     L"the data segment");

    // The state as the image loads it, with the D5 byte; and video memory as mode 3 leaves it.
    const std::vector<std::uint8_t> image = Elite::ReferenceLoadImage().value_or(std::vector<std::uint8_t>{});
    Assert::IsFalse(image.empty(), L"the image loads");
    const std::span<const std::uint8_t> data(image);
    Assert::IsTrue(std::ranges::equal(state.DataBytes(), data), L"the state, from the data segment's image");
    Assert::AreEqual(1u, std::uint32_t{state.Data().unused.protectionShown}, L"with the D5 byte");
    for (std::uint16_t offset = 0; offset < Elite::Cga::VIDEO_MEMORY_BYTES; offset += 2)
    {
      if (state.VideoByte(offset) != 0x20 || state.VideoByte(static_cast<std::uint16_t>(offset + 1)) != 0x07)
        Assert::Fail(L"video memory as video mode 3 leaves it: 0720h words");
    }

    const std::uint64_t changes = state.ChangeCount();
    const std::uint8_t fuel = state.Data().commander.fuel;
    state.Data().commander.fuel = static_cast<std::uint8_t>(fuel ^ 0xFF);
    Assert::AreEqual(changes + 1, state.ChangeCount(), L"a write that changes a byte is counted");
    state.Data().commander.fuel = static_cast<std::uint8_t>(fuel ^ 0xFF);
    Assert::AreEqual(changes + 1, state.ChangeCount(), L"a write of the byte already there is not");
    Assert::AreEqual(std::uint32_t{fuel ^ 0xFFu}, std::uint32_t{state.DataBytes()[GAME_DATA_OFFSET(commander.fuel)]},
                     L"the member's byte is the state's");
    (void)state.Data().commander.fuel;
    (void)state.VideoByte(0);
    Assert::AreEqual(changes + 1, state.ChangeCount(), L"a read counts nothing");

    state.SetVideoWord(0x10, 0xF820); // its high byte, 07h as mode 3 leaves it, changes; its low, 20h, does not
    Assert::AreEqual(0x20u, std::uint32_t{state.VideoByte(0x10)}, L"a word into video memory, low byte first");
    Assert::AreEqual(0xF8u, std::uint32_t{state.VideoByte(0x11)}, L"then its high byte");
    Assert::AreEqual(changes + 2, state.ChangeCount(), L"counted for the byte it changed, not the one it kept");

    Assert::ExpectException<std::out_of_range>([&state] { state.SetVideoByte(Elite::Cga::VIDEO_MEMORY_BYTES, 1); }, L"past video memory");
    Assert::AreEqual(changes + 2, state.ChangeCount(), L"and nothing is written there");
  }
};

} // namespace GameLogicTests
