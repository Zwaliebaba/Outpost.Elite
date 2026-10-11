#include "pch.h"

#include "Sound.h"

#include "Arithmetic.h"
#include "SoundTables.h"

namespace Elite
{

namespace
{

constexpr std::uint8_t SPEAKER_GATE_AND_DATA = 0x03; // port 61h bits 0 and 1
constexpr std::uint8_t SPEAKER_DATA = 0x02;          // port 61h bit 1
constexpr std::uint8_t INAUDIBLE_DIVISOR = 2;

// noiseSourceIndex runs round NOISE_SOURCE's 1024 bytes (SoundTables.h).
constexpr std::uint16_t NOISE_SOURCE_MASK = 0x3FF;

constexpr std::uint8_t BEEP_TICKS = 70;
constexpr std::uint8_t LOW_BEEP_TICKS = 70;
constexpr std::uint8_t NOISE_BURST_TICKS = 100;

// BeginSweep (CS:7ADD), the tail every sweep starter shares, but its STI. Returns the step length it starts the step
// counter at, which the original leaves in AL.
[[nodiscard]] std::uint8_t BeginSweep(GameState& _state)
{
  _state.Data().sweepTickCounter = 1;
  const std::uint8_t stepLength = _state.Data().sweepStepLength;
  _state.Data().sweepStepCounter = stepLength;
  _state.Data().twoToneStage = 0;
  return stepLength;
}

// StartImpactSound and StartExplosionSound: the step length and its shrink, then BeginNoiseSweep (CS:7ACE). Entered
// under CLI, which native code does not need: nothing interrupts it. Returns the step length, as BeginSweep does.
std::uint8_t StartNoiseSweep(GameState& _state, std::uint8_t _stepLength, std::uint8_t _stepShrink)
{
  _state.Data().sweepStepLength = _stepLength;
  _state.Data().sweepStepShrink = _stepShrink;
  _state.Data().noiseSweepActive = 1;
  _state.Data().toneSweepActive = 0;
  _state.Data().sweepPeriodTicks = 1;
  return BeginSweep(_state);
}

} // namespace

void StartMusic(GameState& _state, Hardware& _hardware)
{
  SilenceSpeakerTimer(_hardware);
  // The gate and data on in the port, but not in speakerPortImage.
  _hardware.SetSystemControl(static_cast<std::uint8_t>(_state.Data().speakerPortImage | SPEAKER_GATE_AND_DATA));
  _state.Data().noteTicksLeft = 0;
  _state.Data().noteGapTicks = 0;
  _state.Data().musicPointer = 0;
  _state.Data().musicPlaying = 1;
}

void StopAllSound(GameState& _state, Hardware& _hardware)
{
  // StopSoundEffects clears the effects under CLI and ends with STI.
  StopSoundEffects(_state);
  _hardware.EnableInterrupts();
  _state.Data().musicPlaying = 0;
  const auto port = static_cast<std::uint8_t>(_state.Data().speakerPortImage & ~SPEAKER_GATE_AND_DATA);
  _state.Data().speakerPortImage = port;
  _hardware.SetSystemControl(port);
}

void SilenceSpeakerTimer(Hardware& _hardware)
{
  _hardware.SetToneDivisor(INAUDIBLE_DIVISOR);
}

void StartBeep(GameState& _state)
{
  _state.Data().beepTicks = BEEP_TICKS;
}

void StartLowBeep(GameState& _state)
{
  _state.Data().lowBeepTicks = LOW_BEEP_TICKS;
}

void StopSoundEffects(GameState& _state)
{
  _state.Data().toneSweepActive = 0;
  _state.Data().noiseSweepActive = 0;
  _state.Data().twoToneStage = 0;
  _state.Data().slowNoiseCount = 0;
  _state.Data().humEnabled = 0;
  _state.Data().continuousNoise = 0;
  _state.Data().sirenEnabled = 0;
  _state.Data().beepTicks = 0;
  _state.Data().lowBeepTicks = 0;
}

std::uint8_t EmitNoiseSample(GameState& _state, Hardware& _hardware)
{
  const auto index = static_cast<std::uint16_t>((_state.Data().noiseSourceIndex + 1) & NOISE_SOURCE_MASK);
  const std::uint8_t sample = NOISE_SOURCE[index];
  _state.Data().noiseSourceIndex = index;
  const auto image = static_cast<std::uint8_t>(_state.Data().speakerPortImage & ~SPEAKER_DATA);
  _state.Data().speakerPortImage = image;
  const auto port = static_cast<std::uint8_t>((sample & SPEAKER_DATA) | image);
  _hardware.SetSystemControl(port);
  _state.Data().speakerPortImage = port;
  return port;
}

std::uint8_t ToggleSpeaker(GameState& _state, Hardware& _hardware)
{
  const auto port = static_cast<std::uint8_t>(_state.Data().speakerPortImage ^ SPEAKER_DATA);
  _state.Data().speakerPortImage = port;
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
  _state.Data().continuousNoise = 0;
}

std::optional<std::uint8_t> StartLaserSound(GameState& _state)
{
  if (_state.Data().noiseSweepActive == 1)
  {
    return std::nullopt;
  }
  _state.Data().noiseSweepActive = 0;
  _state.Data().toneSweepActive = 1;
  _state.Data().sweepStepLength = 20;
  _state.Data().sweepStepShrink = 2;
  _state.Data().sweepPeriodTicks = 2;
  return BeginSweep(_state);
}

void StartPlayerHitSound(GameState& _state)
{
  _state.Data().toneSweepActive = 0;
  _state.Data().twoToneStage = 0;
  _state.Data().noiseBurstTicks = NOISE_BURST_TICKS;
}

} // namespace Elite
