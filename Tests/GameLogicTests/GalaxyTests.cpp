#include "pch.h"

#include "Galaxy.h"
#include "TwinRig.h"

#include <array>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The ninth galaxy's seeds: the six bytes after galaxySeeds' eight entries in the original data segment, read from its image while
// the tree held it (Phase 7, step 5).
constexpr std::array<std::uint32_t, 3> NINTH_GALAXY_SEEDS = {0x1234, 0x5678, 0x9ABC};

} // namespace

// The galaxies' seeds where the original's table ends.
TEST_CLASS(GalaxyTests)
{
public:
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
