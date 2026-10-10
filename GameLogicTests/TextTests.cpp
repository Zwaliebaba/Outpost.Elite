#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t UPDATE_MESSAGE_LINE = 0x35A3;
constexpr std::uint16_t TOGGLE_INPUT_CURSOR = 0x7727;
constexpr std::uint16_t PRINT_STRING_FOR_LAYOUT = 0x7750;
constexpr std::uint8_t GRAPHICS_LAYOUT = 0;

// A view angle and the text naming the view. A plain aggregate rather than std::pair, for the reason
// EquipmentTests gives.
struct ViewName
{
  std::uint16_t angle;
  std::uint16_t text;
};

} // namespace

// Constructed inputs for the text routines (plan §6.3): the views and the layout no replay shows them in.
TEST_CLASS(TextTests)
{
public:
  // The message line falls back to the view's name when a message's frames run out: rear, left and right.
  TEST_METHOD(UpdateMessageLineAgreesOnEveryViewName)
  {
    ComparisonRig rig("UpdateMessageLine");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::initializer_list<ViewName> views = {
      {0x400, DS.rearViewText.offset}, {0x200, DS.leftViewText.offset}, {0x600, DS.rightViewText.offset}};
    for (const auto& [angle, text] : views)
    {
      ram.Write8(data, DS.warningFrames.offset, 0);
      ram.Write8(data, DS.messageDrawn.offset, 0xFF);
      ram.Write8(data, DS.messageFrames.offset, 0);
      ram.Write16(data, DS.messageShown.offset, 0);
      ram.Write16(data, DS.viewAngle.offset, angle);
      rig.Call(UPDATE_MESSAGE_LINE, {});
      Assert::IsTrue(ram.Read16(data, DS.messagePointer.offset) == text, L"the view's name is the message");
    }
    rig.AssertAllAgreed(UPDATE_MESSAGE_LINE, views.size());
  }

  // The graphics layouts: the cursor's other glyph, and the string drawn rather than printed.
  TEST_METHOD(InputLineRoutinesAgreeInTheGraphicsLayout)
  {
    ComparisonRig rig("GraphicsLayout");
    rig.Host().Ram().Write8(Elite::DataSegment(rig.Program()), DS.screenLayout.offset, GRAPHICS_LAYOUT);
    rig.Call(TOGGLE_INPUT_CURSOR, {});
    rig.Call(PRINT_STRING_FOR_LAYOUT, {.bx = 0xFFFF, .si = DS.frontViewText.offset, .di = 0x0100});
    rig.AssertAllAgreed(TOGGLE_INPUT_CURSOR, 1);
    rig.AssertAllAgreed(PRINT_STRING_FOR_LAYOUT, 1);
  }
};

} // namespace GameLogicTests
