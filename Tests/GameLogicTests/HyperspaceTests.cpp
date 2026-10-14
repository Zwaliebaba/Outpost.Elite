#include "pch.h"

#include "GameState.h"
#include "Hyperspace.h"
#include "TwinRig.h"

#include <array>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// A jump set up to happen on the next frame: galactic or not, from which galaxy, with the random
// numbers all 0 (randomState0-2 all 0 stay so, below the 200 of a mis-jump and the 300 of the ninth
// galaxy) or from a seed that draws as usual, a forced mis-jump, and the mission.
struct Jump
{
  std::uint8_t galactic;
  std::uint8_t galaxy;
  bool zeroRandom;
  std::uint8_t forceMisjump;
  std::uint8_t mission;
  std::uint8_t missionStage;
};

// randomState0-2, from which the next numbers draw as usual.
constexpr std::array<std::uint16_t, 3> RANDOM_SEED = {0x1234, 0x5678, 0x9ABC};

// Launches from Lave, and flies on for 6 seconds.
void Launch(TwinRig& _rig)
{
  _rig.Play("key space; wait 4\nkey F1; wait 6");
}

// _jump set up on the twin, to the system selected, latched as the target (LatchHyperspaceTarget), and played through the
// tunnel to the arrival.
void PlayJump(TwinRig& _rig, const Jump& _jump)
{
  Elite::GameState& state = _rig.State();
  Elite::LatchHyperspaceTarget(state);
  state.Data().jump.galacticJumpPending = _jump.galactic;
  state.Data().commander.galaxyNumber = _jump.galaxy;
  state.Data().commander.randomState0 = _jump.zeroRandom ? std::uint16_t{0} : RANDOM_SEED[0];
  state.Data().commander.randomState1 = _jump.zeroRandom ? std::uint16_t{0} : RANDOM_SEED[1];
  state.Data().commander.randomState2 = _jump.zeroRandom ? std::uint16_t{0} : RANDOM_SEED[2];
  state.Data().jump.forceMisjump = _jump.forceMisjump;
  state.Data().commander.missionNumber = _jump.mission;
  state.Data().commander.missionStage = _jump.missionStage;
  state.Data().jump.hyperspaceCountdown = 1;
  state.Data().jump.hyperspaceCountdownFrames = 1;
  _rig.Play("wait 3.5\ndigest arrived");
}

} // namespace

// A twin for the jumps the replays do not make (ADR-016): galactic ones, into the ninth galaxy and out, and mis-jumps.
TEST_CLASS(HyperspaceTests)
{
public:
  // The galactic drive's ready frames counted down; galactic jumps into the ninth galaxy and out of it,
  // and into it and straight out; a mis-jump into witch space at random and one forced; and arrivals
  // that arm missions 1 and 3's fuel leaks.
  TEST_METHOD(JumpsAgreeGalacticAndIntoWitchSpace)
  {
    TwinRig rig("TwinJumps");
    Launch(rig);
    rig.Both([](Elite::Host&, Elite::GameState& _state) { _state.Data().jump.galacticDriveReadyFrames = 3; });
    rig.Play("wait 0.5");
    PlayJump(rig, {1, 7, true, 0, 1, 0});
    PlayJump(rig, {1, 8, false, 0, 3, 1});
    PlayJump(rig, {1, 7, false, 0, 0, 0});
    PlayJump(rig, {0, 0, true, 0, 0, 0});
    PlayJump(rig, {0, 0, false, 1, 0, 0});
  }
};

} // namespace GameLogicTests
