#include "pch.h"

#include "DataOverlay.h"
#include "ReferenceRig.h"
#include "StateDigest.h"

#include <algorithm>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

TEST_CLASS(StateDigestTests)
{
public:
  // What the digest covers, and what it leaves out on purpose (StateDigest.h).
  TEST_METHOD(CoversTheGameAndNotItsScratch)
  {
    ReferenceRig rig("Digest");
    Machine::Pc& pc = rig.Host();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::string before = Elite::GameStateDigest(rig.State(), pc.Video());

    pc.Processor().Regs().ax = 0x1234;
    pc.Ram().Write8(data, Elite::STACK_FIRST_OFFSET, 0x55);
    pc.Ram().Write8(data, static_cast<std::uint16_t>(Elite::STACK_END_OFFSET - 1), 0x55);
    pc.Ram().Write8(rig.Program().loadSegment, 0x1CC9, 0x2B); // a patch site of the triangle filler
    Assert::AreEqual(before.c_str(), Elite::GameStateDigest(rig.State(), pc.Video()).c_str(), false, L"registers, stack and code left out");

    pc.Ram().Write8(data, static_cast<std::uint16_t>(Elite::STACK_FIRST_OFFSET - 1), 0x55);
    const std::string belowStack = Elite::GameStateDigest(rig.State(), pc.Video());
    Assert::AreNotEqual(before.c_str(), belowStack.c_str(), false, L"the data segment counts");

    pc.Ram().Write8(data, Elite::STACK_END_OFFSET, 0x55);
    const std::string aboveStack = Elite::GameStateDigest(rig.State(), pc.Video());
    Assert::AreNotEqual(belowStack.c_str(), aboveStack.c_str(), false, L"so does the cockpit image above the stack");

    pc.Ram().Write8(Machine::Cga::VIDEO_MEMORY_LINEAR + 0x3FFF, 0x55);
    Assert::AreNotEqual(aboveStack.c_str(), Elite::GameStateDigest(rig.State(), pc.Video()).c_str(), false, L"and video memory");
  }

  // The GameState owns the game's memory, and the PC maps it (D19): the load's bytes are taken over, a write either way
  // reaches the GameState's storage and is counted once by the PC's memory, and a read counts nothing.
  TEST_METHOD(TheGameStateHoldsWhatThePcMaps)
  {
    ReferenceRig rig("GameState");
    Machine::Pc& pc = rig.Host();
    Elite::GameState& state = rig.State();
    Assert::AreEqual(std::uint32_t{rig.Program().loadSegment}, std::uint32_t{state.CodeSegment()});
    Assert::AreEqual(std::uint32_t{Elite::DataSegment(rig.Program())}, std::uint32_t{state.DataSegment()});

    // Memory's own bytes keep what the load wrote under the regions (Machine::Memory::Bytes).
    const std::span<const std::uint8_t> own = pc.Ram().Bytes();
    const std::uint32_t data = Machine::Memory::Linear(state.DataSegment(), 0);
    const std::uint32_t code = Machine::Memory::Linear(state.CodeSegment(), 0);
    Assert::IsTrue(std::ranges::equal(state.DataBytes(), own.subspan(data, 0x10000)), L"the data segment taken over");
    Assert::IsTrue(std::ranges::equal(state.VideoBytes(), own.subspan(Machine::Cga::VIDEO_MEMORY_LINEAR, Machine::Cga::VIDEO_MEMORY_BYTES)),
                   L"video memory taken over");
    for (std::uint16_t offset = 0; offset < Elite::CODE_SEGMENT_BYTES; ++offset)
    {
      if (state.CodeByte(offset) != own[code + offset])
        Assert::Fail(L"the code segment taken over");
    }
    Assert::AreEqual(1u, std::uint32_t{state.Get(Elite::DS.protectionShown)}, L"with the D5 byte");

    const std::uint64_t changes = pc.Ram().ChangeCount();
    const std::uint8_t fuel = state.Get(Elite::DS.fuel);
    state.Set(Elite::DS.fuel, static_cast<std::uint8_t>(fuel ^ 0xFF));
    Assert::AreEqual(changes + 1, pc.Ram().ChangeCount(), L"the GameState's write is counted by the PC's memory");
    Assert::AreEqual(std::uint32_t{fuel ^ 0xFFu}, std::uint32_t{pc.Ram().Read8(state.DataSegment(), Elite::DS.fuel.offset)},
                     L"the PC reads it");
    Assert::AreEqual(std::uint32_t{fuel}, std::uint32_t{own[data + Elite::DS.fuel.offset]}, L"from the GameState's storage");
    (void)state.Word(Elite::DS.fuel.offset);
    (void)state.CodeWord(0);
    (void)state.VideoWord(0);
    Assert::AreEqual(changes + 1, pc.Ram().ChangeCount(), L"a read counts nothing");

    pc.Ram().Write8(Elite::GameState::VIDEO_SEGMENT, 0x10, static_cast<std::uint8_t>(state.VideoByte(0x10) ^ 0xFF));
    pc.Ram().Write8(state.CodeSegment(), 0x07D0, static_cast<std::uint8_t>(state.CodeByte(0x07D0) ^ 0xFF));
    Assert::AreEqual(std::uint32_t{own[Machine::Cga::VIDEO_MEMORY_LINEAR + 0x10] ^ 0xFFu}, std::uint32_t{state.VideoByte(0x10)},
                     L"the PC's writes reach the GameState");
    Assert::AreEqual(std::uint32_t{own[code + 0x07D0] ^ 0xFFu}, std::uint32_t{state.CodeByte(0x07D0)});
    Assert::AreEqual(changes + 3, pc.Ram().ChangeCount());
  }
};

} // namespace GameLogicTests
