#include "pch.h"

#include "Galaxy.h"
#include "TwinRig.h"

#include <array>
#include <format>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

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

} // namespace

// Twins for a system data screen, which the replays reach only in part (ADR-016).
TEST_CLASS(GalaxyTests)
{
public:
  // The data screen of Leesti, of tech level 10, two digits, in flight: a click on Leesti on the galactic chart, which F1 selects
  // as it closes the chart and launches (ADR-065), then F7. Then its own key and a letter ignored, and F1 back to
  // the front view.
  TEST_METHOD(SystemDataScreenAgreesForTwoDigitTechLevels)
  {
    TwinRig rig("TwinSystemData");
    rig.Play(DOCKED);
    rig.Play(std::format("key F4; wait 0.3\nclick {} {}; wait 0.2\nkey F1; wait 6\ndown F7; wait 0.1; up F7; wait 0.5\ndigest data",
                         LEESTI.x, LEESTI.y / 2));
    rig.Both([](Elite::Host&, Elite::GameState& _state)
             { Assert::AreEqual(std::uint8_t{'1'}, std::uint8_t{_state.Data().galaxy.techLevelTensDigit}, L"two digits"); });
    rig.Play("key F7; wait 0.1; key a; wait 0.1; key F1; wait 0.3\ndigest closed");
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
