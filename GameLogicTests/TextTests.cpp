#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "TwinRig.h"

#include <initializer_list>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

constexpr std::uint16_t UPDATE_MESSAGE_LINE = 0x35A3;
constexpr std::uint16_t SHOW_SHIP_IDENTITY = 0x364D;
constexpr std::uint16_t CLEAR_DOCKED_MESSAGE_LINE = 0x6553;
constexpr std::uint16_t PRINT_TEXT_LINES = 0x6DDE;
constexpr std::uint16_t TOGGLE_MENU_ROW_HIGHLIGHT = 0x6328;
constexpr std::uint16_t TOGGLE_INPUT_CURSOR = 0x7727;
constexpr std::uint16_t PRINT_STRING_FOR_LAYOUT = 0x7750;
constexpr std::uint8_t GRAPHICS_LAYOUT = 0;
constexpr std::uint16_t SLOT_BYTES = 0x40;
constexpr std::uint16_t SPARE_SLOT = 4;
constexpr std::uint16_t LINES = 0x1000; // the space-view buffer, which nothing reads between frames of the title
constexpr std::uint16_t VIDEO_SEGMENT = 0xB800;
constexpr std::uint16_t EQUIP_MENU_FIRST_ROW = 0x0325; // the equipment menu's first row, at its first attribute byte
constexpr std::uint16_t TEXT_ROW_BYTES = 0xA0;

// A ship's type and class, as ShowShipIdentity takes them in AL and AH.
struct Identity
{
  std::uint8_t type;
  std::uint8_t shipClass;
};

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

  // The class and type names of every class, class 3 for debris and for the rest, the Hermit and the police.
  TEST_METHOD(ShipIdentityAgreesForEveryClass)
  {
    ComparisonRig rig("ShowShipIdentity");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const auto slot = static_cast<std::uint16_t>(DS.shipSlots.offset + SPARE_SLOT * SLOT_BYTES);
    const std::initializer_list<Identity> identities = {{1, 0}, {2, 1}, {5, 3}, {9, 3}, {5, 4}, {6, 4}, {0x1C, 2}, {0x1F, 7}};
    for (const Identity& identity : identities)
    {
      // The slot's byte 0 holds the type, which IsDebrisType reads.
      ram.Write8(data, slot, static_cast<std::uint8_t>(identity.type << 1 | 1));
      rig.Call(SHOW_SHIP_IDENTITY, {.ax = static_cast<std::uint16_t>(identity.shipClass << 8 | identity.type), .di = slot});
    }
    rig.AssertAllAgreed(SHOW_SHIP_IDENTITY, identities.size());
  }

  // The docked message line cleared, and lines printed a row apart, two and one.
  TEST_METHOD(DockedTextRoutinesAgree)
  {
    ComparisonRig rig("DockedText");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    Machine::Registers& regs = rig.Host().Processor().Regs();
    const Machine::Registers saved = regs;
    regs.ds = data;
    regs.es = VIDEO_SEGMENT;
    regs.ax = 0x1234;
    regs.cx = 0x5678;
    regs.di = 0x9ABC;
    rig.Host().CallNear(CLEAR_DOCKED_MESSAGE_LINE);
    regs = saved;
    constexpr std::string_view TEXT{"FIRST\0SECOND\0THIRD\0", 19};
    std::uint16_t line = LINES;
    for (const char character : TEXT)
    {
      ram.Write8(data, line, static_cast<std::uint8_t>(character));
      ++line;
    }
    rig.Call(PRINT_TEXT_LINES, {.ax = 0x1111, .cx = 3, .si = LINES, .di = 0x00A0});
    rig.Call(PRINT_TEXT_LINES, {.ax = 0x1111, .cx = 1, .si = LINES, .di = 0x0140});
    rig.AssertAllAgreed(CLEAR_DOCKED_MESSAGE_LINE, 1);
    rig.AssertAllAgreed(PRINT_TEXT_LINES, 2);
  }

  // A menu row highlighted, the highlight taken off again, and the next row highlighted, as a menu's cursor moves.
  // The menus call it as a value routine since level 2 of the de-assembly (ADR-012 item 12), so only a call like
  // this one compares it with the original.
  TEST_METHOD(MenuRowHighlightAgreesOnAndOff)
  {
    ComparisonRig rig("MenuRowHighlight");
    Machine::Registers& regs = rig.Host().Processor().Regs();
    const Machine::Registers saved = regs;
    for (const std::uint16_t row :
         {EQUIP_MENU_FIRST_ROW, EQUIP_MENU_FIRST_ROW, static_cast<std::uint16_t>(EQUIP_MENU_FIRST_ROW + TEXT_ROW_BYTES)})
    {
      regs.ds = Elite::DataSegment(rig.Program());
      regs.es = VIDEO_SEGMENT;
      regs.si = row;
      rig.Host().CallNear(TOGGLE_MENU_ROW_HIGHLIGHT);
      regs = saved;
    }
    rig.AssertAllAgreed(TOGGLE_MENU_ROW_HIGHLIGHT, 3);
  }

  // A line typed on the galactic chart's F: a capital with Shift, one character more than fits, every one
  // deleted and one Backspace more, then Enter while the cursor shows.
  TEST_METHOD(ReadTextLineAgreesWhileEditing)
  {
    TwinRig rig("TwinReadTextLine");
    rig.Play("key space; wait 4\nkey F5; wait 0.3\nkey f; wait 0.1\n"
             "down Shift_L; wait 0.02; key l; wait 0.02; up Shift_L; wait 0.02\n"
             "key a; key v; key e; key q; wait 0.02; key q; key q; key q; key q; wait 0.05\n"
             "key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace; wait 0.02\n"
             "key BackSpace; key BackSpace; key BackSpace; key BackSpace; key BackSpace; wait 0.05\n"
             "key Return; wait 0.1\nkey Escape; wait 0.3\ndigest closed");
  }
};

} // namespace GameLogicTests
