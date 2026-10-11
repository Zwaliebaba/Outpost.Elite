#include "pch.h"

#include "Galaxy.h"
#include "Reference.h"
#include "ReferenceImage.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Commander = Elite::CommanderState;

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

// Where the ninth galaxy's seeds are in the reference's image: the six bytes after galaxySeeds' eight entries, gap29A6, DS:29A6 past
// the code segment.
constexpr std::size_t NINTH_GALAXY_SEEDS_IN_IMAGE = std::size_t{Elite::CODE_SEGMENT_BYTES} + 0x29A6;

// The little-endian word at byte _at of the reference's image.
[[nodiscard]] std::uint32_t ImageWord(std::size_t _at)
{
  return std::uint32_t{Elite::REFERENCE_IMAGE[_at]} | (std::uint32_t{Elite::REFERENCE_IMAGE[_at + 1]} << 8);
}

// _value into the byte _member names, on the twin.
template <typename Struct> void PokeBoth(TwinRig& _rig, DataByte<Struct> _member, std::uint8_t _value)
{
  _rig.Both([_member, _value](Elite::Platform&, Elite::GameState& _state) { DataByteIn(_state, _member) = _value; });
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
    PokeBoth(rig, &Commander::galacticCursorX, 3);
    PokeBoth(rig, &Commander::galacticCursorY, 2);
    rig.Play("key F5; wait 0.3\n"
             "down Left; down Up; wait 0.5; up Left; up Up; wait 0.1\n"
             "key KP_Begin; wait 0.1\n"
             "key d; wait 0.1\n"
             "key a; wait 0.1; key F5; wait 0.1; key Delete; wait 0.1\n"
             "down space; wait 0.1; up space; wait 0.1\n"
             "key f; wait 0.1; key l; key a; key v; key e; key Return; wait 0.1\n"
             "key f; wait 0.1; key z; key z; key Return; wait 0.1\n"
             "key f; wait 0.1; key Return; wait 0.1");
    PokeBoth(rig, &Commander::chartCursorX, 0xFC);
    PokeBoth(rig, &Commander::chartCursorY, 0x7C);
    rig.Play("down Right; down Down; wait 0.5; up Right; up Down; wait 0.1\nkey Escape; wait 0.3\ndigest closed");
  }

  // The short-range chart's keys: the cursor held against each edge, for five of the chart's frames of 84 to
  // 100 ms (D18), D, keypad 5, F with a system on the chart and one off it, and F6, its own key, ignored.
  TEST_METHOD(ShortRangeChartAgreesOnEveryKey)
  {
    TwinRig rig("TwinShortRangeChart");
    rig.Play(DOCKED);
    PokeBoth(rig, &Commander::shortRangeCursorX, 0xFC);
    PokeBoth(rig, &Commander::shortRangeCursorY, 0x7C);
    rig.Play("key F6; wait 0.3\n"
             "down Right; down Down; wait 0.5; up Right; up Down; wait 0.1");
    PokeBoth(rig, &Commander::chartCursorX, 3);
    PokeBoth(rig, &Commander::chartCursorY, 2);
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
    PokeBoth(rig, &Commander::chartIsShortRange, 0);
    PokeBoth(rig, &Commander::chartCursorX, LEESTI.x);
    PokeBoth(rig, &Commander::chartCursorY, static_cast<std::uint8_t>(LEESTI.y / 2));
    rig.Play("key F7; wait 0.5\ndigest data");
    rig.Both([](Elite::Platform&, Elite::GameState& _state)
             { Assert::AreEqual(std::uint8_t{'1'}, std::uint8_t{_state.Data().galaxy.data7D48}, L"two digits"); });
    rig.Play("key F7; wait 0.1; key a; wait 0.1; key Escape; wait 0.3\ndigest closed");
  }

  // The ninth galaxy, which GalacticJump makes now and then after the eighth: the original reads its seeds as a ninth entry of
  // galaxySeeds, which has eight, so they are the six bytes after the table, gap29A6's, as the reference's image holds them.
  TEST_METHOD(NinthGalaxySeedsAreTheBytesAfterTheTable)
  {
    ReferenceRig rig("NinthGalaxySeeds");
    Elite::GameState& state = rig.State();
    state.Data().commander.galaxyNumber = 8;
    Elite::LoadGalaxySeeds(state);
    Assert::AreEqual(ImageWord(NINTH_GALAXY_SEEDS_IN_IMAGE), std::uint32_t{state.Data().galaxy.systemSeed0}, L"the first seed");
    Assert::AreEqual(ImageWord(NINTH_GALAXY_SEEDS_IN_IMAGE + 2), std::uint32_t{state.Data().galaxy.systemSeed1}, L"the second");
    Assert::AreEqual(ImageWord(NINTH_GALAXY_SEEDS_IN_IMAGE + 4), std::uint32_t{state.Data().galaxy.systemSeed2}, L"the third");
  }
};

} // namespace GameLogicTests
