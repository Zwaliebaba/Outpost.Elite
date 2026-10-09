#include "pch.h"

#include "Memory.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

// The 8088 has 20 address lines and nothing to stop a carry out of the top one: FFFF:0010 is
// linear 0, and a word at linear FFFFF takes its high byte from linear 0.
TEST_CLASS(MemoryTests)
{
public:
  TEST_METHOD(LinearAddressWrapsAtOneMegabyte)
  {
    Assert::AreEqual(0x00000u, Machine::Memory::Linear(0xFFFF, 0x0010));
    Assert::AreEqual(0xFFFFFu, Machine::Memory::Linear(0xFFFF, 0x000F));
    Assert::AreEqual(0x0FFEFu, Machine::Memory::Linear(0xFFFF, 0xFFFF));

    Machine::Memory memory;
    memory.Write8(0xFFFF, 0x0010, 0xAB);
    Assert::AreEqual(0xABu, std::uint32_t{memory.Read8(0x00000u)});
    memory.Write8(0x100000u, 0xCD);
    Assert::AreEqual(0xCDu, std::uint32_t{memory.Read8(0x00000u)});
  }

  TEST_METHOD(WordAtTopOfAddressSpaceWrapsToZero)
  {
    Machine::Memory memory;
    memory.Write16(0xFFFFFu, 0x1234);
    Assert::AreEqual(0x34u, std::uint32_t{memory.Read8(0xFFFFFu)});
    Assert::AreEqual(0x12u, std::uint32_t{memory.Read8(0x00000u)});
    Assert::AreEqual(0x1234u, std::uint32_t{memory.Read16(0xFFFFFu)});
  }

  // A word access by segment and offset wraps within the segment: offset FFFF pairs with offset
  // 0000 of the same segment, not with the next paragraph.
  TEST_METHOD(WordAtOffsetFfffWrapsWithinSegment)
  {
    Machine::Memory memory;
    memory.Write16(0x1000, 0xFFFF, 0xBEEF);
    Assert::AreEqual(0xEFu, std::uint32_t{memory.Read8(0x1FFFFu)});
    Assert::AreEqual(0xBEu, std::uint32_t{memory.Read8(0x10000u)});
    Assert::AreEqual(0x00u, std::uint32_t{memory.Read8(0x20000u)});
    Assert::AreEqual(0xBEEFu, std::uint32_t{memory.Read16(0x1000, 0xFFFF)});
  }
};

} // namespace MachineTests
