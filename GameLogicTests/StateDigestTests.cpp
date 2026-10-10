#include "pch.h"

#include "ReferenceRig.h"
#include "StateDigest.h"

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
    Assert::IsTrue(rig.Loaded(), L"ELITES.EXE at the repository root");
    Machine::Pc& pc = rig.Host();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::string before = Elite::GameStateDigest(pc, rig.Program());

    pc.Processor().Regs().ax = 0x1234;
    pc.Ram().Write8(data, Elite::STACK_FIRST_OFFSET, 0x55);
    pc.Ram().Write8(data, static_cast<std::uint16_t>(Elite::STACK_END_OFFSET - 1), 0x55);
    pc.Ram().Write8(rig.Program().loadSegment, 0x1CC9, 0x2B); // a patch site of the triangle filler
    Assert::AreEqual(before.c_str(), Elite::GameStateDigest(pc, rig.Program()).c_str(), false, L"registers, stack and code left out");

    pc.Ram().Write8(data, static_cast<std::uint16_t>(Elite::STACK_FIRST_OFFSET - 1), 0x55);
    const std::string belowStack = Elite::GameStateDigest(pc, rig.Program());
    Assert::AreNotEqual(before.c_str(), belowStack.c_str(), false, L"the data segment counts");

    pc.Ram().Write8(data, Elite::STACK_END_OFFSET, 0x55);
    const std::string aboveStack = Elite::GameStateDigest(pc, rig.Program());
    Assert::AreNotEqual(belowStack.c_str(), aboveStack.c_str(), false, L"so does the cockpit image above the stack");

    pc.Ram().Write8(Machine::Cga::VIDEO_MEMORY_LINEAR + 0x3FFF, 0x55);
    Assert::AreNotEqual(aboveStack.c_str(), Elite::GameStateDigest(pc, rig.Program()).c_str(), false, L"and video memory");
  }
};

} // namespace GameLogicTests
