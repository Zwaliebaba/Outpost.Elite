#include "pch.h"

#include "Sound.h"

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

// BeginSweep, the tail every sweep starter shares. Returns the step length it starts the step
// counter at.
[[nodiscard]] std::uint8_t BeginSweep(GameState& _state)
{
  _state.Data().audio.sweepTickCounter = 1;
  const std::uint8_t stepLength = _state.Data().audio.sweepStepLength;
  _state.Data().audio.sweepStepCounter = stepLength;
  return stepLength;
}

// StartImpactSound and StartExplosionSound: the step length and its shrink, then BeginNoiseSweep. Entered
// with interrupts off, which native code does not need: nothing interrupts it. Returns the step length, as BeginSweep does.
std::uint8_t StartNoiseSweep(GameState& _state, std::uint8_t _stepLength, std::uint8_t _stepShrink)
{
  _state.Data().audio.sweepStepLength = _stepLength;
  _state.Data().sound.sweepStepShrink = _stepShrink;
  _state.Data().audio.noiseSweepActive = 1;
  _state.Data().audio.toneSweepActive = 0;
  _state.Data().audio.sweepPeriodTicks = 1;
  return BeginSweep(_state);
}

} // namespace

void StartMusic(GameState& _state, Host& _host)
{
  SilenceSpeakerTimer(_host);
  // The gate and data on in the port, but not in speakerPortImage.
  _host.SetSpeakerBits(static_cast<std::uint8_t>(_state.Data().audio.speakerPortImage | SPEAKER_GATE_AND_DATA));
  _state.Data().audio.noteTicksLeft = 0;
  _state.Data().audio.noteGapTicks = 0;
  _state.Data().audio.musicPointer = 0;
  _state.Data().sound.musicPlaying = 1;
}

void StopAllSound(GameState& _state, Host& _host)
{
  StopSoundEffects(_state);
  _state.Data().sound.musicPlaying = 0;
  const auto port = static_cast<std::uint8_t>(_state.Data().audio.speakerPortImage & ~SPEAKER_GATE_AND_DATA);
  _state.Data().audio.speakerPortImage = port;
  _host.SetSpeakerBits(port);
}

void SilenceSpeakerTimer(Host& _host)
{
  _host.Sound().SetTone(INAUDIBLE_DIVISOR);
}

void StartBeep(GameState& _state)
{
  _state.Data().audio.beepTicks = BEEP_TICKS;
}

void StartLowBeep(GameState& _state)
{
  _state.Data().audio.lowBeepTicks = LOW_BEEP_TICKS;
}

void StopSoundEffects(GameState& _state)
{
  _state.Data().audio.toneSweepActive = 0;
  _state.Data().audio.noiseSweepActive = 0;
  _state.Data().audio.beepTicks = 0;
  _state.Data().audio.lowBeepTicks = 0;
}

std::uint8_t EmitNoiseSample(GameState& _state, Host& _host)
{
  const auto index = static_cast<std::uint16_t>((_state.Data().sound.noiseSourceIndex + 1) & NOISE_SOURCE_MASK);
  const std::uint8_t sample = NOISE_SOURCE[index];
  _state.Data().sound.noiseSourceIndex = index;
  const auto image = static_cast<std::uint8_t>(_state.Data().audio.speakerPortImage & ~SPEAKER_DATA);
  _state.Data().audio.speakerPortImage = image;
  const auto port = static_cast<std::uint8_t>((sample & SPEAKER_DATA) | image);
  _host.SetSpeakerBits(port);
  _state.Data().audio.speakerPortImage = port;
  return port;
}

std::uint8_t ToggleSpeaker(GameState& _state, Host& _host)
{
  const auto port = static_cast<std::uint8_t>(_state.Data().audio.speakerPortImage ^ SPEAKER_DATA);
  _state.Data().audio.speakerPortImage = port;
  _host.SetSpeakerBits(port);
  return port;
}

void StartImpactSound(GameState& _state)
{
  StartNoiseSweep(_state, 30, 4);
}

void StartExplosionSound(GameState& _state)
{
  StartNoiseSweep(_state, 50, 8);
}

std::uint8_t StartPlayerDeathSound(GameState& _state)
{
  return StartNoiseSweep(_state, 60, 7);
}

void StartLaserSound(GameState& _state)
{
  if (_state.Data().audio.noiseSweepActive == 1)
  {
    return;
  }
  _state.Data().audio.noiseSweepActive = 0;
  _state.Data().audio.toneSweepActive = 1;
  _state.Data().audio.sweepStepLength = 20;
  _state.Data().sound.sweepStepShrink = 2;
  _state.Data().audio.sweepPeriodTicks = 2;
  static_cast<void>(BeginSweep(_state));
}

void StartPlayerHitSound(GameState& _state)
{
  _state.Data().audio.toneSweepActive = 0;
  _state.Data().audio.noiseBurstTicks = NOISE_BURST_TICKS;
}

} // namespace Elite
