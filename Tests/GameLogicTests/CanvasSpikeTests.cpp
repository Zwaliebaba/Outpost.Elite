#include "pch.h"


#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

/*
 * A measurement, not a port. Read this before slice 1d writes a line of Canvas.cpp.
 *
 * ADR-002 section 4 fixes the canvas as "320x200 logical pixels, one byte per pixel holding a
 * C64 colour index". That was asserted rather than derived, and the drawing code does not
 * support it. The C64 screen is a MULTICOLOUR bitmap: 160 double-width pixels across 200 rows,
 * two bits each, with the colours for %01 and %10 coming from a per-8x8-cell byte in screen RAM
 * and %11 from colour RAM. Every drawing routine EORs whole BYTES into that bitmap.
 *
 * The question this file exists to settle is whether an index-per-pixel canvas can reproduce
 * those writes. The tests ask the shipped game rather than reasoning about it: each one snapshots
 * the bitmap, calls a routine, and reports what actually changed -- it does not model the
 * routine's address arithmetic, because a spike that assumes the answer measures nothing. Each
 * logs what it saw, so the finding can be read off a test run and written into an ADR-002
 * amendment, and each asserts what must hold, so this does not decay into a log nobody notices
 * going stale.
 *
 * DELETE THIS FILE when slice 1d closes. Its findings belong in ADR-002 and in Canvas.h's
 * commentary; a spike that outlives its slice becomes furniture nobody dares move.
 */
namespace GameLogicTests
{

  namespace
  {
    /// 6502: SCBASE -- the bitmap base, and 0x2000 bytes long. It is an assembler constant rather
    /// than a label, so it is not in the label map; ylookup's first entry is SCBASE plus the space
    /// view's left margin, and deriving it from there measures the margin at the same time.
    constexpr std::uint16_t SPACE_VIEW_MARGIN = 0x20;
    constexpr std::uint16_t BITMAP_SIZE = 0x2000;
    constexpr std::uint16_t SCREEN_RAM_OFFSET = 0x2000;

    /// A multicolour byte holds four two-bit pixels. A mask is ALIGNED when every bit it sets falls
    /// inside one pixel's pair, and STRADDLES when it sets one bit of one pixel and one of the next.
    /// A straddling mask cannot be expressed as "plot colour C at pixel P", which is the whole
    /// question this file asks.
    [[nodiscard]] bool IsMulticolourAligned(std::uint8_t _mask) noexcept
    {
      return ((_mask >> 1) & 0x55) == (_mask & 0x55);
    }

    /// The two-bit colour code at pixel _index (0 is the leftmost) of a multicolour byte.
    [[nodiscard]] std::uint8_t PixelCode(std::uint8_t _byte, int _index) noexcept
    {
      return static_cast<std::uint8_t>((_byte >> (6 - 2 * _index)) & 0x03);
    }

    /// One byte the routine under test changed, found by comparison rather than by predicting where
    /// the routine would write.
    struct Change
    {
      std::uint16_t address = 0;
      std::uint8_t before = 0;
      std::uint8_t after = 0;

      [[nodiscard]] std::uint8_t Mask() const noexcept
      {
        return static_cast<std::uint8_t>(before ^ after);
      }
    };

  } // namespace

  TEST_CLASS(CanvasRepresentationSpike)
  {
  public:
    /// A guard on the reasoning above rather than on the game. If IsMulticolourAligned is wrong,
    /// every conclusion in this file is wrong, and it is four lines nobody would think to test.
    TEST_METHOD(TheAlignmentPredicateItselfIsCorrect)
    {
      Assert::IsTrue(IsMulticolourAligned(0b11000000), L"pixel 0 set");
      Assert::IsTrue(IsMulticolourAligned(0b00110000), L"pixel 1 set");
      Assert::IsTrue(IsMulticolourAligned(0b00000011), L"pixel 3 set");
      Assert::IsTrue(IsMulticolourAligned(0b11111111), L"every pixel set");
      Assert::IsTrue(IsMulticolourAligned(0b00000000), L"nothing set");
      Assert::IsFalse(IsMulticolourAligned(0b01100000), L"one bit of pixel 0 and one of pixel 1");
      Assert::IsFalse(IsMulticolourAligned(0b00011000), L"one bit of pixel 1 and one of pixel 2");
      Assert::IsFalse(IsMulticolourAligned(0b00000110), L"one bit of pixel 2 and one of pixel 3");

      Assert::AreEqual<std::uint32_t>(0u, PixelCode(0b00110000, 0), L"pixel 0 of %00110000");
      Assert::AreEqual<std::uint32_t>(3u, PixelCode(0b00110000, 1), L"pixel 1 of %00110000");
      Assert::AreEqual<std::uint32_t>(1u, PixelCode(0b01000000, 0), L"pixel 0 of %01000000");
    }
  };

} // namespace GameLogicTests
