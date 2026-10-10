#include "pch.h"

#include "Combat.h"
#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "GameState.h"
#include "Maths.h"
#include "ObjectSlot.h"
#include "Ships.h"
#include "TwinRig.h"

#include <initializer_list>
#include <string>
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
constexpr std::uint16_t REDRAW_INPUT_LINE = 0x773A;
constexpr std::uint16_t PRINT_STRING_FOR_LAYOUT = 0x7750;
constexpr std::uint8_t GRAPHICS_LAYOUT = 0;
constexpr std::uint8_t TEXT_LAYOUT = 2;
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

// The spawners the game makes ships with outside witch space, each picking one of its records by its first random draw.
enum class Spawner : std::uint8_t
{
  Drifter, // SpawnRandomDrifter
  Trader,  // SpawnRandomTrader
  Hunter,  // SpawnRandomHunter
  Wolf     // SpawnRandomWolf
};

// Where shipIdentityText's class name goes, and its length: "CLASS: " comes first.
constexpr std::uint16_t IDENTITY_CLASS_OFFSET = 7;
constexpr std::size_t IDENTITY_CLASS_BYTES = 7;
// How far ahead a ship to identify is put: a second or two before one flying at the player reaches it.
constexpr std::uint16_t IDENTIFY_AHEAD = 0x600;
// LaunchShipFromObject's kind for a missile at the player, as TryLaunchMissileAtPlayer launches one.
constexpr std::uint8_t MISSILE_LAUNCH = 0x14;
// The draws a spawner's record is looked for in: every record comes up far sooner.
constexpr int MOST_DRAWS = 1000;

// The record _spawner's first draw picks when NextRandom gives _random: a random byte over 37, 43 or 52 (hunters, traders,
// wolves), or a drifter's, the word rotated right and its two bytes XORed, of eight.
[[nodiscard]] std::uint8_t PickedRecord(Spawner _spawner, std::uint16_t _random) noexcept
{
  const auto low = static_cast<std::uint8_t>(_random & 0xFF);
  switch (_spawner)
  {
  case Spawner::Trader:
    return static_cast<std::uint8_t>(low / 43);
  case Spawner::Hunter:
    return static_cast<std::uint8_t>(low / 37);
  case Spawner::Wolf:
    return static_cast<std::uint8_t>(low / 52);
  case Spawner::Drifter:
    break;
  }
  const auto rotated = static_cast<std::uint16_t>((_random >> 1) | (_random << 15));
  return static_cast<std::uint8_t>(((rotated & 0xFF) ^ (rotated >> 8)) & 7);
}

// A ship of record _record of _spawner's, in the first free ship slot of both twins, made by the game's own spawner, with the
// random state drawn on by NextRandom until the spawner's first draw picks that record, as some later moment of play would have
// it; then put _ahead straight ahead of the player, where a ship flying at the player passes, and turned to the player and set
// going (FacePlayer, ComputeVelocity), as a spawner turns and sets going a ship it has placed. Returns the slot.
std::uint16_t SpawnAhead(TwinRig& _rig, Spawner _spawner, std::uint8_t _record, std::uint16_t _ahead)
{
  std::uint16_t placed = 0;
  _rig.Both(
    [&placed, _spawner, _record, _ahead](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      for (int draws = 0;; ++draws)
      {
        // NextRandom's next result is the sum of the first two words of the state.
        const auto next = static_cast<std::uint16_t>(state.Get(DS.randomState0) + state.Get(DS.randomState1));
        if (PickedRecord(_spawner, next) == _record)
          break;
        Assert::IsTrue(draws < MOST_DRAWS, L"the spawner's record comes up");
        (void)Elite::NextRandom(state);
      }
      const Elite::SlotSearch free = Elite::FindFreeShipSlot(state);
      Assert::IsTrue(free.found, L"a free ship slot");
      Elite::ObjectSlot ship(state, free.slot);
      switch (_spawner)
      {
      case Spawner::Drifter:
        Elite::SpawnRandomDrifter(state, ship);
        break;
      case Spawner::Trader:
        (void)Elite::SpawnRandomTrader(state, ship);
        break;
      case Spawner::Hunter:
        Elite::SpawnRandomHunter(state, ship);
        break;
      case Spawner::Wolf:
        Elite::SpawnRandomWolf(state, ship);
        break;
      }
      ship.Set(Elite::SlotWord::X, 0);
      ship.Set(Elite::SlotWord::Y, 0);
      ship.Set(Elite::SlotWord::Z, _ahead);
      ship.Set(Elite::SlotWord::XYHigh, 0);
      ship.Set(Elite::SlotByte::ZHigh, 0);
      (void)Elite::FacePlayer(state, ship);
      (void)Elite::ComputeVelocity(state, ship);
      placed = free.slot;
    });
  return placed;
}

// The ship at _slot gone from both twins, as RemoveObject takes one out of play: its active bit cleared and its blip erased.
void RemoveShip(TwinRig& _rig, std::uint16_t _slot)
{
  _rig.Both(
    [_slot](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      (void)Elite::RemoveObject(state, Elite::ObjectSlot(state, _slot));
    });
}

// A missile launched at the player by the ship at _launcher in both twins, as TryLaunchMissileAtPlayer launches one
// (LaunchShipFromObject): a copy of the ship in a free slot, made a missile and moved three frames on. Returns its slot.
std::uint16_t LaunchMissileAtPlayer(TwinRig& _rig, std::uint16_t _launcher)
{
  std::uint16_t launched = 0;
  _rig.Both(
    [&launched, _launcher](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      Elite::GameState state(_pc.Ram(), _program.loadSegment, Elite::DataSegment(_program));
      const std::optional<std::uint16_t> slot =
        Elite::LaunchShipFromObject(state, Elite::ObjectSlot(state, _launcher), MISSILE_LAUNCH, false);
      Assert::IsTrue(slot.has_value(), L"a free slot for the missile");
      launched = slot.value_or(0);
    });
  return launched;
}

// The class name ShowShipIdentity put in shipIdentityText, as the native twin has it; the interpreted twin's is the same, or the
// digests would differ.
[[nodiscard]] std::string IdentifiedClass(TwinRig& _rig)
{
  std::string name;
  _rig.Both(
    [&name](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
    {
      name.clear();
      for (std::size_t index = 0; index < IDENTITY_CLASS_BYTES; ++index)
        name += static_cast<char>(_pc.Ram().Read8(Elite::DataSegment(_program),
                                                  static_cast<std::uint16_t>(DS.shipIdentityText.offset + IDENTITY_CLASS_OFFSET + index)));
    });
  return name;
}

// I, held until the ship in the sights is identified (HandleIdentifyKey looks each frame until one is), digested as _label; its
// class name checked; then U, which unarms the missile I locked on it, so that the next I is read.
void Identify(TwinRig& _rig, std::string_view _label, std::string_view _className)
{
  _rig.Play("down i; wait 0.1; up i; wait 0.2\ndigest " + std::string(_label));
  const std::string identified = IdentifiedClass(_rig);
  Assert::AreEqual(std::wstring(_className.begin(), _className.end()), std::wstring(identified.begin(), identified.end()),
                   std::wstring(_label.begin(), _label.end()).c_str());
  _rig.Play("down u; wait 0.1; up u; wait 0.1");
}

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

  // The cursor flipped and a line of three characters redrawn after it, in the text layout and in a graphics one. ReadTextLine
  // calls ToggleInputCursor and RedrawInputLine as value routines since level 5 of the de-assembly (ADR-012 item 12), so only
  // calls like these compare them with the original.
  TEST_METHOD(InputLineRedrawAgreesInBothLayouts)
  {
    ComparisonRig rig("InputLineRedraw");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    constexpr std::string_view TYPED{"ELI"};
    std::uint16_t at = LINES;
    for (const char character : TYPED)
    {
      ram.Write8(data, at, static_cast<std::uint8_t>(character));
      ++at;
    }
    const std::initializer_list<std::uint8_t> layouts = {TEXT_LAYOUT, GRAPHICS_LAYOUT};
    for (const std::uint8_t layout : layouts)
    {
      ram.Write8(data, DS.screenLayout.offset, layout);
      rig.Call(TOGGLE_INPUT_CURSOR, {});
      rig.Call(REDRAW_INPUT_LINE, {.ax = 0x1234, .bx = static_cast<std::uint16_t>(TYPED.size()), .si = LINES, .di = 0x0100});
      rig.Call(PRINT_STRING_FOR_LAYOUT, {.bx = 0xFFFF, .si = DS.frontViewText.offset, .di = 0x0200});
    }
    rig.AssertAllAgreed(TOGGLE_INPUT_CURSOR, layouts.size());
    rig.AssertAllAgreed(REDRAW_INPUT_LINE, layouts.size());
    rig.AssertAllAgreed(PRINT_STRING_FOR_LAYOUT, layouts.size());
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

  // ShowShipIdentity's labels other than Debris (ADR-008 item 8, D20): one ship of each class it names, each made by the game's
  // own spawner, put ahead of the player launched from Lave, and identified with I. A trader; a Hermit, the trader that is a
  // rock; a Viper, Police whatever its class; a hunter; a Shuttle, Simple, of class 3 but no rock; a wolf; and the missile the
  // wolf launches at the player, Attack. Not None: class 0 is only the Cobra an escape capsule leaves, and while the capsule
  // flies ProcessFlightKeys reads no key; nor Station, which HandleIdentifyKey passes over.
  TEST_METHOD(ShipIdentityNamesEveryClass)
  {
    TwinRig rig("TwinShipIdentity");
    rig.Play("key space; wait 4\nkey F1; wait 1.5\ndigest launched");
    struct Named
    {
      Spawner spawner;
      std::uint8_t record;
      std::string_view label;
      std::string_view className;
    };
    const Named ships[] = {{Spawner::Trader, 0, "trader", "Trader "},
                           {Spawner::Trader, 4, "hermit", "Hermit "},
                           {Spawner::Trader, 5, "police", "Police "},
                           {Spawner::Hunter, 0, "hunter", "Hunter "},
                           {Spawner::Drifter, 6, "simple", "Simple "}};
    for (const Named& ship : ships)
    {
      const std::uint16_t slot = SpawnAhead(rig, ship.spawner, ship.record, IDENTIFY_AHEAD);
      Identify(rig, ship.label, ship.className);
      RemoveShip(rig, slot);
    }
    const std::uint16_t wolf = SpawnAhead(rig, Spawner::Wolf, 0, IDENTIFY_AHEAD);
    Identify(rig, "wolf", "Wolf   ");
    const std::uint16_t missile = LaunchMissileAtPlayer(rig, wolf);
    // A frame for the missile to be drawn, which FindShipInCrosshairs needs; I is read before the frame's drawing.
    rig.Play("wait 0.1");
    Identify(rig, "attack", "Attack ");
    RemoveShip(rig, missile);
    RemoveShip(rig, wolf);
    rig.Play("wait 0.1");
  }
};

} // namespace GameLogicTests
