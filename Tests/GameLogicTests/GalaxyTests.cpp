#include "pch.h"

#include "Galaxy.h"
#include "Reference.h"
#include "ReferenceImage.h"
#include "ReferenceRig.h"
#include "Replay.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// From the title screen: the commander loaded, docked at Lave, on the status screen.
constexpr std::string_view DOCKED = "key space; wait 4";

// Where the ninth galaxy's seeds are in the reference's image: the six bytes after galaxySeeds' eight entries, gap29A6, DS:29A6 past
// the code segment.
constexpr std::size_t NINTH_GALAXY_SEEDS_IN_IMAGE = std::size_t{Elite::CODE_SEGMENT_BYTES} + 0x29A6;

// The little-endian word at byte _at of the reference's image.
[[nodiscard]] std::uint32_t ImageWord(std::size_t _at)
{
  return std::uint32_t{Elite::REFERENCE_IMAGE[_at]} | (std::uint32_t{Elite::REFERENCE_IMAGE[_at + 1]} << 8);
}

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

} // namespace

// The short-range chart within its buffers, and the ninth galaxy's seeds.
TEST_CLASS(GalaxyTests)
{
public:
  // The short-range charts whose items or labels the original ran past its buffers, which a change on purpose caps (D41,
  // ADR-027): each is shown from the dock and stays within its 32 items and its labels' buffer, and the game runs on.
  TEST_METHOD(CrowdedShortRangeChartsStayInTheirBuffers)
  {
    ReferenceRig rig("CrowdedShortRangeCharts");
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
      state.Data().commander.currentSystemIndex = chart.system;
      state.Data().commander.currentSystemX = std::uint8_t{state.Data().galaxy.systemSeed1Bytes.systemX};
      state.Data().commander.currentSystemChartY = static_cast<std::uint8_t>(state.Data().galaxy.systemSeed0Bytes.systemY >> 1);
      play("key F6; wait 0.5");
      Assert::IsTrue(std::size_t{state.Data().galaxy.chartItemEnd} <= std::size(state.Data().galaxy.chartItems), L"32 items");
      Assert::IsTrue(std::size_t{state.Data().galaxy.chartLabelCursor} <= sizeof(Elite::GalaxyState::pendingChartLabels),
                     L"the labels within their buffer");
      play("key F9; wait 0.5");
    }
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
