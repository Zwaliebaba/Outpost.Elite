#include "pch.h"

#include "CgaFont.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

// The frame DrawDockedFrame draws (0xBA, 0xCD and the six corners and tees at DS:0xA403) and the
// block ToggleInputCursor makes in text mode.
constexpr std::uint8_t FRAME_AND_CURSOR_CHARACTERS[] = {0xBA, 0xCD, 0xC9, 0xBB, 0xCC, 0xB9, 0xC8, 0xBC, 0xDB};

} // namespace

TEST_CLASS(CgaFontTests)
{
public:
  // Every character the reference can put in text mode has a glyph of its own: the docked screens'
  // strings are printable ASCII, and the rest is the frame and the cursor.
  TEST_METHOD(EveryCharacterTheGameUsesIsDrawn)
  {
    for (std::uint32_t code = 0x20; code <= 0x7E; ++code)
    {
      Assert::IsTrue(Machine::CgaFont::IsDrawn(static_cast<std::uint8_t>(code)));
    }
    for (const std::uint8_t code : FRAME_AND_CURSOR_CHARACTERS)
    {
      Assert::IsTrue(Machine::CgaFont::IsDrawn(code));
    }
    for (std::uint32_t row = 0; row < Machine::CgaFont::GLYPH_ROWS; ++row)
    {
      Assert::AreEqual(0xFFu, std::uint32_t{Machine::CgaFont::Row(0xDB, row)}, L"0xDB is a full block");
      Assert::AreEqual(0u, std::uint32_t{Machine::CgaFont::Row(' ', row)}, L"a space is blank");
    }
  }

  // The double lines sit on columns 1-2 and 5-6 and rows 2 and 4, and run to the cell's edges, so
  // the frame's pieces join.
  TEST_METHOD(DoubleLinePiecesJoin)
  {
    for (std::uint32_t row = 0; row < Machine::CgaFont::GLYPH_ROWS; ++row)
    {
      Assert::AreEqual(0x66u, std::uint32_t{Machine::CgaFont::Row(0xBA, row)});
      const std::uint32_t horizontal = row == 2 || row == 4 ? 0xFFu : 0x00u;
      Assert::AreEqual(horizontal, std::uint32_t{Machine::CgaFont::Row(0xCD, row)});
    }
    Assert::AreEqual(0x7Fu, std::uint32_t{Machine::CgaFont::Row(0xC9, 2)}, L"top-left corner's outer line");
    Assert::AreEqual(0x66u, std::uint32_t{Machine::CgaFont::Row(0xC9, 7)}, L"top-left corner meets 0xBA below");
    Assert::AreEqual(0xFEu, std::uint32_t{Machine::CgaFont::Row(0xBC, 4)}, L"bottom-right corner's outer line");
    Assert::AreEqual(0x66u, std::uint32_t{Machine::CgaFont::Row(0xBC, 0)}, L"bottom-right corner meets 0xBA above");
  }

  TEST_METHOD(AnUndrawnCodePointIsAHollowBox)
  {
    const std::uint32_t box[] = {0xFE, 0x82, 0x82, 0x82, 0x82, 0x82, 0xFE, 0x00};
    for (const std::uint8_t code : {std::uint8_t{0x01}, std::uint8_t{0x7F}, std::uint8_t{0xE0}})
    {
      Assert::IsFalse(Machine::CgaFont::IsDrawn(code));
      for (std::uint32_t row = 0; row < Machine::CgaFont::GLYPH_ROWS; ++row)
      {
        Assert::AreEqual(box[row], std::uint32_t{Machine::CgaFont::Row(code, row)});
      }
    }
  }
};

} // namespace MachineTests
