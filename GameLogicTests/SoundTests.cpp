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

constexpr std::uint16_t START_LOW_BEEP = 0x7A5D;
constexpr std::uint16_t START_PLAYER_DEATH_SOUND = 0x7B09;
constexpr std::uint16_t STOP_CONTINUOUS_NOISE = 0x7B6B;

} // namespace

// Constructed inputs for the sound routines (plan §6.3): the starters no replay calls, the docking computer's low
// beep, the player's death and GAME OVER's silence.
TEST_CLASS(SoundTests)
{
public:
  // Each from silence and over a sound already playing.
  TEST_METHOD(SoundStartersAgreeFromSilenceAndOverASound)
  {
    ComparisonRig rig("SoundStarters");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::initializer_list<std::uint8_t> states = {0, 1};
    for (const std::uint8_t state : states)
    {
      for (const Elite::DataField<std::uint8_t> effect : {DS.lowBeepTicks, DS.continuousNoise, DS.noiseSweepActive, DS.toneSweepActive})
      {
        ram.Write8(data, effect.offset, state);
      }
      ram.Write8(data, DS.twoToneStage.offset, static_cast<std::uint8_t>(state * 2));
      rig.Call(START_LOW_BEEP, {.ax = 0x1234});
      rig.Call(START_PLAYER_DEATH_SOUND, {.ax = 0x1234});
      rig.Call(STOP_CONTINUOUS_NOISE, {.ax = 0x1234});
    }
    rig.AssertAllAgreed(START_LOW_BEEP, states.size());
    rig.AssertAllAgreed(START_PLAYER_DEATH_SOUND, states.size());
    rig.AssertAllAgreed(STOP_CONTINUOUS_NOISE, states.size());
  }
};

} // namespace GameLogicTests
