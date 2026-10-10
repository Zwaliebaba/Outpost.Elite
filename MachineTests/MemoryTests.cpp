#include "pch.h"

#include "Memory.h"

#include <array>
#include <span>
#include <stdexcept>

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

  // A mapped region (Map) takes over the bytes already there, uncounted, and from then on every read and write of it, by
  // linear address or by segment, reaches the storage behind it; a write that changes a byte is counted as anywhere else.
  TEST_METHOD(MappedRegionReachesItsStorage)
  {
    Machine::Memory memory;
    memory.Write8(0x2000, 0x0010, 0x5A);
    const std::uint64_t changes = memory.ChangeCount();
    std::array<std::uint8_t, 0x20> storage{};
    memory.Map(0x20000u, storage);
    Assert::AreEqual(0x5Au, std::uint32_t{storage[0x10]}, L"taken over");
    Assert::AreEqual(changes, memory.ChangeCount(), L"and not counted");

    memory.Write16(0x2001, 0x000F, 0xBEEF); // the region's last byte, then the first past it
    Assert::AreEqual(0xEFu, std::uint32_t{storage[0x1F]}, L"into the storage");
    Assert::AreEqual(0xBEu, std::uint32_t{memory.Bytes()[0x20020]}, L"past the region, into Memory's own bytes");
    Assert::AreEqual(changes + 2, memory.ChangeCount(), L"two bytes changed");
    memory.Write8(0x20010u, 0x5A);
    Assert::AreEqual(changes + 2, memory.ChangeCount(), L"the value already there: no change");

    storage[3] = 0x77;
    Assert::AreEqual(0x77u, std::uint32_t{memory.Read8(0x2000, 0x0003)}, L"reads reach the storage");
    Assert::AreEqual(0xBEEFu, std::uint32_t{memory.Read16(0x2001, 0x000F)}, L"a word across its end");
  }

  // A region is whole paragraphs from a paragraph boundary, inside the 1 MiB, and clear of every region mapped before.
  TEST_METHOD(MapRefusesWhatIsNotWholeParagraphsOrOverlaps)
  {
    Machine::Memory memory;
    std::array<std::uint8_t, 0x20> storage{};
    std::array<std::uint8_t, 0x10> other{};
    const auto refused = [&memory](std::uint32_t _first, std::span<std::uint8_t> _bytes)
    {
      try
      {
        memory.Map(_first, _bytes);
      }
      catch (const std::invalid_argument&)
      {
        return true;
      }
      return false;
    };
    Assert::IsTrue(refused(0x20008u, storage), L"off a paragraph boundary");
    Assert::IsTrue(refused(0x20000u, std::span<std::uint8_t>(storage).first(0x18)), L"part of a paragraph");
    Assert::IsTrue(refused(0xFFFF0u, storage), L"past the 1 MiB");
    Assert::IsFalse(refused(0x20000u, storage), L"whole paragraphs");
    Assert::IsTrue(refused(0x20010u, other), L"over a region mapped before");
    Assert::IsFalse(refused(0x20020u, other), L"beside it");
  }
};

} // namespace MachineTests
