#include "pch.h"

#include "ReferenceRig.h"
#include "StateDigest.h"

#include <algorithm>
#include <stdexcept>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// fuel's 1987 offset in the data segment (Design/Symbols.tsv). The far accessors and the accessors by offset address the game's
// memory as the original does, by segment and offset, so the test reaches the member there on purpose: the original's address
// finds the member where the state holds it now (D38).
constexpr std::uint16_t FUEL_OFFSET = 0x75E2;

// The original's stack, DS:AD70-B15F, a gap the state does not hold (D38), and the reference's image's bytes there.
constexpr std::uint16_t STACK_OFFSET = 0xAD70;

} // namespace

TEST_CLASS(StateDigestTests)
{
public:
  // What the digest covers, and what it leaves out on purpose (StateDigest.h): every byte of the game's state, from GameData's
  // first group to its last, video memory and the CGA's registers, and not the original's stack, which the state does not hold.
  TEST_METHOD(CoversTheGameAndNotItsScratch)
  {
    ReferenceRig rig("Digest");
    Elite::GameState& state = rig.State();
    Elite::Cga& video = rig.Host().Video();
    const std::string before = Elite::GameStateDigest(state, video);

    Assert::ExpectException<std::out_of_range>([&state] { state.SetByte(STACK_OFFSET, 0x55); }, L"the stack is not the state's");
    Assert::AreEqual(before.c_str(), Elite::GameStateDigest(state, video).c_str(), false, L"the stack left out");

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

  // The GameState owns the game's memory (D19), loaded from the checked-in image at the segments DOS would have used (D33): the
  // load's bytes are its own, a write either way is counted once when it changes a byte, a read counts nothing, and an
  // address outside its two regions, the code segment's too (D33, piece 3), is not the game's memory. A byte of the data
  // segment the state does not hold (D38) reads as the image's, and is not written.
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
    Assert::AreEqual(std::uint32_t{fuel ^ 0xFFu}, std::uint32_t{state.FarByte(state.DataSegment(), FUEL_OFFSET)}, L"a far read finds it");
    (void)state.Word(FUEL_OFFSET);
    (void)state.VideoByte(0);
    Assert::AreEqual(changes + 1, state.ChangeCount(), L"a read counts nothing");

    state.SetFarByte(Elite::GameState::VIDEO_SEGMENT, 0x10, static_cast<std::uint8_t>(state.VideoByte(0x10) ^ 0xFF));
    state.SetFarByte(state.CodeSegment(), Elite::CODE_SEGMENT_BYTES, static_cast<std::uint8_t>(state.Byte(0) ^ 0xFF));
    Assert::AreEqual(std::uint32_t{0x20 ^ 0xFFu}, std::uint32_t{state.VideoByte(0x10)}, L"far writes reach video memory");
    Assert::AreEqual(std::uint32_t{data[0] ^ 0xFFu}, std::uint32_t{state.Byte(0)},
                     L"and past the code segment, the data segment, which follows it");
    Assert::AreEqual(changes + 3, state.ChangeCount());

    Assert::AreEqual(0u, std::uint32_t{state.Byte(STACK_OFFSET)}, L"a byte the state does not hold reads as the image's");
    Assert::ExpectException<std::out_of_range>([&state] { state.SetFarByte(state.DataSegment(), STACK_OFFSET, 1); }, L"and is not written");
    Assert::ExpectException<std::out_of_range>([&state] { (void)state.FarByte(state.CodeSegment(), 0x07D0); },
                                               L"the code segment is not the game's memory");
    Assert::ExpectException<std::out_of_range>([&state] { (void)state.FarByte(0x0000, 0x0020); }, L"nor is the vector table");
    Assert::ExpectException<std::out_of_range>([&state] { state.SetFarByte(0xF000, 0xFFFE, 0); }, L"nor the ROM");
    Assert::AreEqual(changes + 3, state.ChangeCount(), L"and nothing is written there");
  }
};

} // namespace GameLogicTests
