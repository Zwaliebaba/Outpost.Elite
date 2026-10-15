#include "pch.h"

#include "Galaxy.h"
#include "TwinRig.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

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

// The ninth galaxy's seeds: the six bytes after galaxySeeds' eight entries in the original data segment, read from its image while
// the tree held it (Phase 7, step 5).
constexpr std::array<std::uint32_t, 3> NINTH_GALAXY_SEEDS = {0x1234, 0x5678, 0x9ABC};

// Systems whose short-range chart overran the original's buffers (D41): a galaxy, numbered from 0, and a system.
struct CrowdedChart
{
  std::uint8_t galaxy;
  std::uint8_t system;
};

// More than 16 systems, so more than the 32 items: system 11 of the first galaxy, and ZACEAR, whose labels also overran
// pendingChartLabels. ISVEISAT's labels overran it too. System 81 of the first galaxy has a label that ran out of nudges below
// the chart.
constexpr std::array<CrowdedChart, 4> CROWDED_CHARTS = {{{0, 11}, {8, 64}, {8, 126}, {0, 81}}};

// _value into the byte _member names, on the twin.
template <typename Struct> void PokeBoth(TwinRig& _rig, DataByte<Struct> _member, std::uint8_t _value)
{
  _rig.Both([_member, _value](Elite::Host&, Elite::GameState& _state) { DataByteIn(_state, _member) = _value; });
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
    rig.Both([](Elite::Host&, Elite::GameState& _state)
             { Assert::AreEqual(std::uint8_t{'1'}, std::uint8_t{_state.Data().galaxy.techLevelTensDigit}, L"two digits"); });
    rig.Play("key F7; wait 0.1; key a; wait 0.1; key Escape; wait 0.3\ndigest closed");
  }

  // The short-range charts whose items or labels the original ran past its buffers, which a change on purpose caps (D41,
  // ADR-027): each is shown from the dock and stays within its 32 items and its labels' buffer, and the game runs on.
  TEST_METHOD(CrowdedShortRangeChartsStayInTheirBuffers)
  {
    GameRig rig("CrowdedShortRangeCharts");
    Elite::ReplayPlayer player(rig.Host(), rig.State());
    const auto play = [&player](std::string_view _steps)
    {
      std::vector<Elite::Step> steps;
      std::string error;
      Assert::IsTrue(Elite::ParseSteps(_steps, steps, error), L"parses");
      Elite::Checkpoint checkpoint;
      for (const Elite::Step& step : steps)
      {
        Assert::IsTrue(player.Play(step, checkpoint) == Elite::StopReason::Reached, L"runs");
      }
    };
    play("wait 3");
    play(DOCKED);
    for (const CrowdedChart chart : CROWDED_CHARTS)
    {
      Elite::GameState& state = rig.State();
      state.Data().commander.galaxyNumber = chart.galaxy;
      Elite::LoadSystemSeeds(state, chart.system);
      state.Data().commander.currentSystem.index = chart.system;
      state.Data().commander.currentSystemX = std::uint8_t{Elite::SystemX(state)};
      state.Data().commander.currentSystemChartY = static_cast<std::uint8_t>(Elite::SystemY(state) >> 1);
      play("key F6; wait 0.5");
      Assert::IsTrue(std::size_t{state.Data().galaxy.chartItemEnd} <= std::size(state.Data().galaxy.chartItems), L"32 items");
      Assert::IsTrue(std::size_t{state.Data().galaxy.chartLabelCursor} <= sizeof(Elite::GalaxyState::pendingChartLabels),
                     L"the labels within their buffer");
      play("key F9; wait 0.5");
    }
  }

  // The ninth galaxy, which GalacticJump makes now and then after the eighth: the original reads its seeds as a ninth entry of
  // galaxySeeds, which has eight, so they are the six bytes after the table, gap29A6's, as the reference's image held them.
  TEST_METHOD(NinthGalaxySeedsAreTheBytesAfterTheTable)
  {
    GameRig rig("NinthGalaxySeeds");
    Elite::GameState& state = rig.State();
    state.Data().commander.galaxyNumber = 8;
    Elite::LoadGalaxySeeds(state);
    Assert::AreEqual(NINTH_GALAXY_SEEDS[0], std::uint32_t{state.Data().galaxy.systemSeed0}, L"the first seed");
    Assert::AreEqual(NINTH_GALAXY_SEEDS[1], std::uint32_t{state.Data().galaxy.systemSeed1}, L"the second");
    Assert::AreEqual(NINTH_GALAXY_SEEDS[2], std::uint32_t{state.Data().galaxy.systemSeed2}, L"the third");
  }
};

} // namespace GameLogicTests
