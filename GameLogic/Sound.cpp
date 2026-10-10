#include "pch.h"

#include "Sound.h"

#include "Arithmetic.h"
#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::uint8_t SPEAKER_GATE_AND_DATA = 0x03; // port 61h bits 0 and 1
constexpr std::uint8_t SPEAKER_DATA = 0x02;          // port 61h bit 1
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

// StartImpactSound and StartExplosionSound: the step length and its shrink, then BeginNoiseSweep (CS:7ACE). Entered
// under CLI, which native code does not need: nothing interrupts it. Returns the step length, as BeginSweep does.
std::uint8_t StartNoiseSweep(GameState& _state, std::uint8_t _stepLength, std::uint8_t _stepShrink)
{
  _state.Set(DS.sweepStepLength, _stepLength);
  _state.Set(DS.sweepStepShrink, _stepShrink);
  _state.Set(DS.noiseSweepActive, 1);
  _state.Set(DS.toneSweepActive, 0);
  _state.Set(DS.sweepPeriodTicks, 1);
  return BeginSweep(_state);
}

} // namespace

void StartMusic(GameState& _state, Hardware& _hardware)
{
  SilenceSpeakerTimer(_hardware);
  // The gate and data on in the port, but not in speakerPortImage.
  _hardware.SetSystemControl(static_cast<std::uint8_t>(_state.Get(DS.speakerPortImage) | SPEAKER_GATE_AND_DATA));
  _state.Set(DS.noteTicksLeft, 0);
  _state.Set(DS.noteGapTicks, 0);
  _state.Set(DS.musicPointer, DS.titleTune.offset);
  _state.Set(DS.musicPlaying, 1);
}

void StopAllSound(GameState& _state, Hardware& _hardware)
{
  // StopSoundEffects clears the effects under CLI and ends with STI.
  StopSoundEffects(_state);
  _hardware.EnableInterrupts();
  _state.Set(DS.musicPlaying, 0);
  const auto port = static_cast<std::uint8_t>(_state.Get(DS.speakerPortImage) & ~SPEAKER_GATE_AND_DATA);
  _state.Set(DS.speakerPortImage, port);
  _hardware.SetSystemControl(port);
}

void SilenceSpeakerTimer(Hardware& _hardware)
{
  _hardware.SetToneDivisor(INAUDIBLE_DIVISOR);
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

std::uint8_t EmitNoiseSample(GameState& _state, Hardware& _hardware)
{
  const auto index = static_cast<std::uint16_t>((_state.Get(DS.noiseSourceIndex) + 1) & NOISE_SOURCE_MASK);
  const std::uint8_t sample = _state.CodeByte(static_cast<std::uint16_t>(NOISE_SOURCE_OFFSET + index));
  _state.Set(DS.noiseSourceIndex, index);
  const auto image = static_cast<std::uint8_t>(_state.Get(DS.speakerPortImage) & ~SPEAKER_DATA);
  _state.Set(DS.speakerPortImage, image);
  const auto port = static_cast<std::uint8_t>((sample & SPEAKER_DATA) | image);
  _hardware.SetSystemControl(port);
  _state.Set(DS.speakerPortImage, port);
  return port;
}

std::uint8_t ToggleSpeaker(GameState& _state, Hardware& _hardware)
{
  const auto port = static_cast<std::uint8_t>(_state.Get(DS.speakerPortImage) ^ SPEAKER_DATA);
  _state.Set(DS.speakerPortImage, port);
  _hardware.SetSystemControl(port);
  return port;
}

std::uint8_t StartImpactSound(GameState& _state)
{
  return StartNoiseSweep(_state, 30, 4);
}

std::uint8_t StartExplosionSound(GameState& _state)
{
  return StartNoiseSweep(_state, 50, 8);
}

std::uint8_t StartPlayerDeathSound(GameState& _state)
{
  return StartNoiseSweep(_state, 60, 7);
}

void StopContinuousNoise(GameState& _state)
{
  _state.Set(DS.continuousNoise, 0);
}

std::optional<std::uint8_t> StartLaserSound(GameState& _state)
{
  if (_state.Get(DS.noiseSweepActive) == 1)
  {
    return std::nullopt;
  }
  _state.Set(DS.noiseSweepActive, 0);
  _state.Set(DS.toneSweepActive, 1);
  _state.Set(DS.sweepStepLength, 20);
  _state.Set(DS.sweepStepShrink, 2);
  _state.Set(DS.sweepPeriodTicks, 2);
  return BeginSweep(_state);
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
// StartExplosionSound's and StartPlayerDeathSound's: their callers' callers read what they leave in AX, the step length
// in AL and AH as it was (ADR-012). DrawSunOrPlanet goes on with KillPlayer's, and UpdateMissileAi's contract compares
// both, through ExplodeObject and KillPlayer.
constexpr Machine::NativeContract LEAVES_STEP_LENGTH_ENABLES_INTERRUPTS{0, FLAG_INTERRUPT};

} // namespace

void StartMusicEntry(Guest& _guest)
{
  StartMusic(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_AX);
}

void StopAllSoundEntry(Guest& _guest)
{
  StopAllSound(_guest.State(), _guest.Devices());
  _guest.Clobber(CLOBBERS_AX);
}

void SilenceSpeakerTimerEntry(Guest& _guest)
{
  SilenceSpeakerTimer(_guest.Devices());
  _guest.Clobber(CLOBBERS_AX);
}

void EmitNoiseSampleEntry(Guest& _guest)
{
  SetLow(_guest.Regs().ax, EmitNoiseSample(_guest.State(), _guest.Devices()));
  _guest.Clobber(PRESERVES_ALL);
}

void ToggleSpeakerEntry(Guest& _guest)
{
  SetLow(_guest.Regs().ax, ToggleSpeaker(_guest.State(), _guest.Devices()));
  _guest.Clobber(PRESERVES_ALL);
}

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
  _guest.Devices().EnableInterrupts();
  _guest.Clobber(ENABLES_INTERRUPTS);
}

void StartImpactSoundEntry(Guest& _guest)
{
  // CLI before the writes, which nothing interrupts in native code, and the STI that ends BeginSweep: AL the step
  // length, and interrupts on.
  SetLow(_guest.Regs().ax, StartImpactSound(_guest.State()));
  _guest.Devices().EnableInterrupts();
  _guest.Clobber(CLOBBERS_AX_ENABLES_INTERRUPTS);
}

void StartExplosionSoundEntry(Guest& _guest)
{
  // As StartImpactSoundEntry; the contract compares AX.
  SetLow(_guest.Regs().ax, StartExplosionSound(_guest.State()));
  _guest.Devices().EnableInterrupts();
  _guest.Clobber(LEAVES_STEP_LENGTH_ENABLES_INTERRUPTS);
}

void StartPlayerDeathSoundEntry(Guest& _guest)
{
  // As StartImpactSoundEntry; the contract compares AX.
  SetLow(_guest.Regs().ax, StartPlayerDeathSound(_guest.State()));
  _guest.Devices().EnableInterrupts();
  _guest.Clobber(LEAVES_STEP_LENGTH_ENABLES_INTERRUPTS);
}

void StopContinuousNoiseEntry(Guest& _guest)
{
  StopContinuousNoise(_guest.State());
  _guest.Clobber(PRESERVES_ALL);
}

void StartLaserSoundEntry(Guest& _guest)
{
  // CLI before the writes, which nothing interrupts in native code, and the STI that ends BeginSweep: a sweep started
  // leaves AL its step length and interrupts on.
  if (const std::optional<std::uint8_t> stepLength = StartLaserSound(_guest.State()))
  {
    SetLow(_guest.Regs().ax, *stepLength);
    _guest.Devices().EnableInterrupts();
  }
  _guest.Clobber(CLOBBERS_AX_ENABLES_INTERRUPTS);
}

void StartPlayerHitSoundEntry(Guest& _guest)
{
  // CLI round the writes, which nothing interrupts in native code, then STI.
  StartPlayerHitSound(_guest.State());
  _guest.Devices().EnableInterrupts();
  _guest.Clobber(ENABLES_INTERRUPTS);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x7401, "StartMusic", &StartMusicEntry, CLOBBERS_AX},
  NativeEntry{0x7423, "StopAllSound", &StopAllSoundEntry, CLOBBERS_AX},
  NativeEntry{0x7436, "SilenceSpeakerTimer", &SilenceSpeakerTimerEntry, CLOBBERS_AX},
  NativeEntry{0x7A57, "StartBeep", &StartBeepEntry, PRESERVES_ALL},
  NativeEntry{0x7A5D, "StartLowBeep", &StartLowBeepEntry, PRESERVES_ALL},
  NativeEntry{0x7A63, "StopSoundEffects", &StopSoundEffectsEntry, ENABLES_INTERRUPTS},
  NativeEntry{0x7A93, "EmitNoiseSample", &EmitNoiseSampleEntry, PRESERVES_ALL},
  NativeEntry{0x7AB8, "ToggleSpeaker", &ToggleSpeakerEntry, PRESERVES_ALL},
  NativeEntry{0x7AC3, "StartImpactSound", &StartImpactSoundEntry, CLOBBERS_AX_ENABLES_INTERRUPTS},
  NativeEntry{0x7AFC, "StartExplosionSound", &StartExplosionSoundEntry, LEAVES_STEP_LENGTH_ENABLES_INTERRUPTS},
  NativeEntry{0x7B09, "StartPlayerDeathSound", &StartPlayerDeathSoundEntry, LEAVES_STEP_LENGTH_ENABLES_INTERRUPTS},
  NativeEntry{0x7B6B, "StopContinuousNoise", &StopContinuousNoiseEntry, PRESERVES_ALL},
  NativeEntry{0x7B71, "StartLaserSound", &StartLaserSoundEntry, CLOBBERS_AX_ENABLES_INTERRUPTS},
  NativeEntry{0x7B96, "StartPlayerHitSound", &StartPlayerHitSoundEntry, ENABLES_INTERRUPTS},
};

} // namespace

std::span<const NativeEntry> SoundEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
