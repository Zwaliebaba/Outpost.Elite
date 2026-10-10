#include "pch.h"

#include "DataOverlay.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Elite::DS;

// From the title screen: the commander loaded, docked at Lave, on the status screen.
constexpr std::string_view DOCKED = "key space; wait 4";

/// A system's coordinates.
struct Position
{
  std::uint8_t x;
  std::uint8_t y;
};

// Leesti, near Lave in the first galaxy: of tech level 10.
constexpr Position LEESTI = {13, 186};

// _value into the data segment's _field, on the twin.
void PokeBoth(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Both([_field, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _field.offset, _value); });
}

} // namespace

// Twins for the charts' edges and keys, and a system data screen, which the replays reach only in part (ADR-016).
TEST_CLASS(GalaxyTests)
{
public:
  // The galactic chart's every key from the dock: the cursor held against the top-left corner and the bottom-right
  // one, keypad 5 and fire recentring it, D, F with a name that is found, one that is not and none, keys it ignores,
  // and Esc. Each hold lasts four of the chart's 117 ms frames (D18), long enough for the steering to reach the edge.
  TEST_METHOD(GalacticChartAgreesOnEveryKey)
  {
    TwinRig rig("TwinGalacticChart");
    rig.Play(DOCKED);
    PokeBoth(rig, DS.galacticCursorX, 3);
    PokeBoth(rig, DS.galacticCursorY, 2);
    rig.Play("key F5; wait 0.3\n"
             "down Left; down Up; wait 0.5; up Left; up Up; wait 0.1\n"
             "key KP_Begin; wait 0.1\n"
             "key d; wait 0.1\n"
             "key a; wait 0.1; key F5; wait 0.1; key Delete; wait 0.1\n"
             "down space; wait 0.1; up space; wait 0.1\n"
             "key f; wait 0.1; key l; key a; key v; key e; key Return; wait 0.1\n"
             "key f; wait 0.1; key z; key z; key Return; wait 0.1\n"
             "key f; wait 0.1; key Return; wait 0.1");
    PokeBoth(rig, DS.chartCursorX, 0xFC);
    PokeBoth(rig, DS.chartCursorY, 0x7C);
    rig.Play("down Right; down Down; wait 0.5; up Right; up Down; wait 0.1\nkey Escape; wait 0.3\ndigest closed");
  }

  // The short-range chart's keys: the cursor held against each edge, for five of the chart's frames of 84 to
  // 100 ms (D18), D, keypad 5, F with a system on the chart and one off it, and F6, its own key, ignored.
  TEST_METHOD(ShortRangeChartAgreesOnEveryKey)
  {
    TwinRig rig("TwinShortRangeChart");
    rig.Play(DOCKED);
    PokeBoth(rig, DS.shortRangeCursorX, 0xFC);
    PokeBoth(rig, DS.shortRangeCursorY, 0x7C);
    rig.Play("key F6; wait 0.3\n"
             "down Right; down Down; wait 0.5; up Right; up Down; wait 0.1");
    PokeBoth(rig, DS.chartCursorX, 3);
    PokeBoth(rig, DS.chartCursorY, 2);
    rig.Play("down Left; down Up; wait 0.5; up Left; up Up; wait 0.1\n"
             "key d; wait 0.1; key KP_Begin; wait 0.1; key F6; wait 0.1\n"
             "key f; wait 0.1; key t; key i; key b; key e; key d; key i; key e; key d; key Return; wait 0.1\n"
             "key f; wait 0.1; key r; key i; key e; key d; key q; key u; key a; key t; key Return; wait 0.1\n"
             "key Escape; wait 0.3\ndigest closed");
  }

  // The data screen of Leesti, of tech level 10, two digits: the status screen selects the system at the galactic
  // chart's cursor as F7 leaves it. Then its own key and a letter ignored, and Esc.
  TEST_METHOD(SystemDataScreenAgreesForTwoDigitTechLevels)
  {
    TwinRig rig("TwinSystemData");
    rig.Play(DOCKED);
    PokeBoth(rig, DS.chartIsShortRange, 0);
    PokeBoth(rig, DS.chartCursorX, LEESTI.x);
    PokeBoth(rig, DS.chartCursorY, static_cast<std::uint8_t>(LEESTI.y / 2));
    rig.Play("key F7; wait 0.5\ndigest data");
    rig.Both([](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
             { Assert::AreEqual(std::uint8_t{'1'}, _pc.Ram().Read8(Elite::DataSegment(_program), DS.data7D48.offset), L"two digits"); });
    rig.Play("key F7; wait 0.1; key a; wait 0.1; key Escape; wait 0.3\ndigest closed");
  }
};

} // namespace GameLogicTests
