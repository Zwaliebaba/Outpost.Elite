#include "pch.h"

#include "Sound.h"

#include "Arithmetic.h"
#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t SPEAKER_PORT = 0x61;
constexpr std::uint16_t PIT_COMMAND_PORT = 0x43;
constexpr std::uint16_t PIT_CHANNEL_2_PORT = 0x42;
constexpr std::uint8_t SPEAKER_GATE_AND_DATA = 0x03; // port 61h bits 0 and 1
constexpr std::uint8_t SPEAKER_DATA = 0x02;          // port 61h bit 1
constexpr std::uint8_t PIT_CHANNEL_2_MODE_3 = 0xB6;  // channel 2, low then high byte, square wave
constexpr std::uint8_t INAUDIBLE_DIVISOR = 2;

// noiseSource: the code bytes at CS:07D0, read as data, 1024 of them.
constexpr std::uint16_t NOISE_SOURCE_OFFSET = 0x07D0;
constexpr std::uint16_t NOISE_SOURCE_MASK = 0x3FF;

constexpr std::uint8_t BEEP_TICKS = 70;
constexpr std::uint8_t LOW_BEEP_TICKS = 70;
constexpr std::uint8_t NOISE_BURST_TICKS = 100;

// BeginSweep (CS:7ADD), the tail every sweep starter shares, but its STI. Returns the step length it starts the step
// counter at, which the original leaves in AL.
[[nodiscard]] std::uint8_t BeginSweep(GameState& _state)
{
  _state.Set(DS.sweepTickCounter, 1);
  const std::uint8_t stepLength = _state.Get(DS.sweepStepLength);
  _state.Set(DS.sweepStepCounter, stepLength);
  _state.Set(DS.twoToneStage, 0);
  return stepLength;
}

// StartImpactSound and StartExplosionSound: the step length and its shrink, then BeginNoiseSweep
// (CS:7ACE). Entered under CLI, which native code does not need: nothing interrupts it.
void StartNoiseSweep(Guest& _guest, std::uint8_t _stepLength, std::uint8_t _stepShrink)
{
  _guest.Set(DS.sweepStepLength, _stepLength);
  _guest.Set(DS.sweepStepShrink, _stepShrink);
  _guest.Set(DS.noiseSweepActive, 1);
  _guest.Set(DS.toneSweepActive, 0);
  _guest.Set(DS.sweepPeriodTicks, 1);
  SetLow(_guest.Regs().ax, BeginSweep(_guest.State()));
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
}

} // namespace

void StartMusic(Guest& _guest)
{
  SilenceSpeakerTimer(_guest);
  Machine::Registers& regs = _guest.Regs();
  const auto port = static_cast<std::uint8_t>(_guest.Get(DS.speakerPortImage) | SPEAKER_GATE_AND_DATA);
  SetLow(regs.ax, port);
  _guest.Out8(SPEAKER_PORT, port);
  _guest.Set(DS.noteTicksLeft, 0);
  _guest.Set(DS.noteGapTicks, 0);
  regs.ax = DS.titleTune.offset;
  _guest.Set(DS.musicPointer, regs.ax);
  _guest.Set(DS.musicPlaying, 1);
}

void StopAllSound(Guest& _guest)
{
  StopSoundEffectsEntry(_guest);
  _guest.Set(DS.musicPlaying, 0);
  const auto port = static_cast<std::uint8_t>(_guest.Get(DS.speakerPortImage) & ~SPEAKER_GATE_AND_DATA);
  _guest.Set(DS.speakerPortImage, port);
  SetLow(_guest.Regs().ax, port);
  _guest.Out8(SPEAKER_PORT, port);
}

void SilenceSpeakerTimer(Guest& _guest)
{
  _guest.Out8(PIT_COMMAND_PORT, PIT_CHANNEL_2_MODE_3);
  _guest.Out8(PIT_CHANNEL_2_PORT, INAUDIBLE_DIVISOR);
  _guest.Out8(PIT_CHANNEL_2_PORT, 0);
  SetLow(_guest.Regs().ax, 0);
}

void StartBeep(GameState& _state)
{
  _state.Set(DS.beepTicks, BEEP_TICKS);
}

void StartLowBeep(GameState& _state)
{
  _state.Set(DS.lowBeepTicks, LOW_BEEP_TICKS);
}

void StopSoundEffects(GameState& _state)
{
  _state.Set(DS.toneSweepActive, 0);
  _state.Set(DS.noiseSweepActive, 0);
  _state.Set(DS.twoToneStage, 0);
  _state.Set(DS.slowNoiseCount, 0);
  _state.Set(DS.humEnabled, 0);
  _state.Set(DS.continuousNoise, 0);
  _state.Set(DS.sirenEnabled, 0);
  _state.Set(DS.beepTicks, 0);
  _state.Set(DS.lowBeepTicks, 0);
}

void EmitNoiseSample(Guest& _guest)
{
  const auto index = static_cast<std::uint16_t>((_guest.Get(DS.noiseSourceIndex) + 1) & NOISE_SOURCE_MASK);
  const std::uint8_t sample = _guest.CodeByte(static_cast<std::uint16_t>(NOISE_SOURCE_OFFSET + index));
  _guest.Set(DS.noiseSourceIndex, index);
  const auto image = static_cast<std::uint8_t>(_guest.Get(DS.speakerPortImage) & ~SPEAKER_DATA);
  _guest.Set(DS.speakerPortImage, image);
  const auto port = static_cast<std::uint8_t>((sample & SPEAKER_DATA) | image);
  SetLow(_guest.Regs().ax, port);
  _guest.Out8(SPEAKER_PORT, port);
  _guest.Set(DS.speakerPortImage, port);
}

void ToggleSpeaker(Guest& _guest)
{
  const auto port = static_cast<std::uint8_t>(_guest.Get(DS.speakerPortImage) ^ SPEAKER_DATA);
  _guest.Set(DS.speakerPortImage, port);
  SetLow(_guest.Regs().ax, port);
  _guest.Out8(SPEAKER_PORT, port);
}

void StartImpactSound(Guest& _guest)
{
  StartNoiseSweep(_guest, 30, 4);
}

void StartExplosionSound(Guest& _guest)
{
  StartNoiseSweep(_guest, 50, 8);
}

void StartPlayerDeathSound(Guest& _guest)
{
  StartNoiseSweep(_guest, 60, 7);
}

void StopContinuousNoise(GameState& _state)
{
  _state.Set(DS.continuousNoise, 0);
}

void StartLaserSound(Guest& _guest)
{
  if (_guest.Get(DS.noiseSweepActive) == 1)
  {
    return;
  }
  _guest.Set(DS.noiseSweepActive, 0);
  _guest.Set(DS.toneSweepActive, 1);
  _guest.Set(DS.sweepStepLength, 20);
  _guest.Set(DS.sweepStepShrink, 2);
  _guest.Set(DS.sweepPeriodTicks, 2);
  SetLow(_guest.Regs().ax, BeginSweep(_guest.State()));
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
}

void StartPlayerHitSound(GameState& _state)
{
  _state.Set(DS.toneSweepActive, 0);
  _state.Set(DS.twoToneStage, 0);
  _state.Set(DS.noiseBurstTicks, NOISE_BURST_TICKS);
}

// ── The entries of the de-assembled routines ──

namespace
{

using Machine::FLAG_INTERRUPT;
using Machine::REGISTER_AX;

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract ENABLES_INTERRUPTS{0, FLAG_INTERRUPT};
constexpr Machine::NativeContract CLOBBERS_AX_ENABLES_INTERRUPTS{REGISTER_AX, FLAG_INTERRUPT};

} // namespace

void StartBeepEntry(Guest& _guest)
{
  StartBeep(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void StartLowBeepEntry(Guest& _guest)
{
  StartLowBeep(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void StopSoundEffectsEntry(Guest& _guest)
{
  // CLI round the writes, which nothing interrupts in native code, then STI.
  StopSoundEffects(_guest.State());
  _guest.SetFlag(FLAG_INTERRUPT, true);
  _guest.Clobber(ENABLES_INTERRUPTS);
}

void StopContinuousNoiseEntry(Guest& _guest)
{
  StopContinuousNoise(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void StartPlayerHitSoundEntry(Guest& _guest)
{
  // CLI round the writes, which nothing interrupts in native code, then STI.
  StartPlayerHitSound(_guest.State());
  _guest.SetFlag(FLAG_INTERRUPT, true);
  _guest.Clobber(ENABLES_INTERRUPTS);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x7401, "StartMusic", &StartMusic, CLOBBERS_AX},
  NativeEntry{0x7423, "StopAllSound", &StopAllSound, CLOBBERS_AX},
  NativeEntry{0x7436, "SilenceSpeakerTimer", &SilenceSpeakerTimer, CLOBBERS_AX},
  NativeEntry{0x7A57, "StartBeep", &StartBeepEntry, PRESERVES_ALL},
  NativeEntry{0x7A5D, "StartLowBeep", &StartLowBeepEntry, PRESERVES_ALL},
  NativeEntry{0x7A63, "StopSoundEffects", &StopSoundEffectsEntry, ENABLES_INTERRUPTS},
  NativeEntry{0x7A93, "EmitNoiseSample", &EmitNoiseSample, PRESERVES_ALL},
  NativeEntry{0x7AB8, "ToggleSpeaker", &ToggleSpeaker, PRESERVES_ALL},
  NativeEntry{0x7AC3, "StartImpactSound", &StartImpactSound, CLOBBERS_AX_ENABLES_INTERRUPTS},
  NativeEntry{0x7AFC, "StartExplosionSound", &StartExplosionSound, CLOBBERS_AX_ENABLES_INTERRUPTS},
  NativeEntry{0x7B09, "StartPlayerDeathSound", &StartPlayerDeathSound, CLOBBERS_AX_ENABLES_INTERRUPTS},
  NativeEntry{0x7B6B, "StopContinuousNoise", &StopContinuousNoiseEntry, PRESERVES_ALL},
  NativeEntry{0x7B71, "StartLaserSound", &StartLaserSound, CLOBBERS_AX_ENABLES_INTERRUPTS},
  NativeEntry{0x7B96, "StartPlayerHitSound", &StartPlayerHitSoundEntry, ENABLES_INTERRUPTS},
};

} // namespace

std::span<const NativeEntry> SoundEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
