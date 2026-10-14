// GameLogic/AudioState.h
#pragma once

#include <cstdint>
#include <type_traits>

namespace Elite
{

/// The speaker's effects and the music, which Sound starts and the timer's handler steps. Its members are the 1987 data segment's, by their
/// names, in their order there; the GameState holds it in GameData.
struct AudioState
{
  /// the value last written to port 0x61; the timer handler toggles bit 1 for sound effects
  std::uint8_t speakerPortImage;
  /// counts down from 70 (StartBeep); speaker bit 1 = bit 0 of the count, so 500 Hz for 70 ms
  std::uint8_t beepTicks;
  /// counts down from 70 (StartLowBeep); speaker bit 1 = bit 1 of the count, 250 Hz
  std::uint8_t lowBeepTicks;
  /// sweep that toggles the speaker (ToggleSpeaker); started by StartLaserSound
  std::uint8_t toneSweepActive;
  /// sweep that plays noiseSource samples (EmitNoiseSample); started by StartNoiseSweep, for impacts, explosions and the player's death
  std::uint8_t noiseSweepActive;
  /// samples per step; falls by sweepStepShrink each step
  std::uint8_t sweepStepLength;
  /// ticks to the next sample
  std::uint8_t sweepTickCounter;
  /// samples left in this step
  std::uint8_t sweepStepCounter;
  /// ticks between samples; +1 a step, the sweep ends at 7
  std::uint8_t sweepPeriodTicks;
  /// one noise sample a tick while nonzero; 100
  std::uint8_t noiseBurstTicks;
  /// silent tail of the current note
  std::uint16_t noteGapTicks;
  std::uint16_t noteTicksLeft;
  /// next note; 0x47 restarts the tune
  std::uint16_t musicPointer;

  /// Each member, by its name, in its order (D76): what the state's digest and the commander file are written from.
  template <typename Self, typename Visitor> static void VisitMembers(Self& _self, Visitor& _visitor)
  {
    _visitor("speakerPortImage", _self.speakerPortImage);
    _visitor("beepTicks", _self.beepTicks);
    _visitor("lowBeepTicks", _self.lowBeepTicks);
    _visitor("toneSweepActive", _self.toneSweepActive);
    _visitor("noiseSweepActive", _self.noiseSweepActive);
    _visitor("sweepStepLength", _self.sweepStepLength);
    _visitor("sweepTickCounter", _self.sweepTickCounter);
    _visitor("sweepStepCounter", _self.sweepStepCounter);
    _visitor("sweepPeriodTicks", _self.sweepPeriodTicks);
    _visitor("noiseBurstTicks", _self.noiseBurstTicks);
    _visitor("noteGapTicks", _self.noteGapTicks);
    _visitor("noteTicksLeft", _self.noteTicksLeft);
    _visitor("musicPointer", _self.musicPointer);
  }

  template <typename Visitor> void Visit(Visitor& _visitor)
  {
    VisitMembers(*this, _visitor);
  }

  template <typename Visitor> void Visit(Visitor& _visitor) const
  {
    VisitMembers(*this, _visitor);
  }
};

static_assert(std::is_trivially_copyable_v<AudioState> && std::is_standard_layout_v<AudioState>);

} // namespace Elite
