#include "pch.h"

#include "Timer.h"

#include "Arithmetic.h"
#include "Sound.h"
#include "TimerTables.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t TICK_DIVISOR = 0x04A9; // channel 0 in mode 3: about 1000.15 Hz
constexpr std::uint32_t TICKS_PER_DAY = 0x1800B0;
constexpr std::uint8_t SPEAKER_BITS = 0x03; // gate and data, cleared at start-up
constexpr std::uint8_t SPEAKER_DATA = 0x02;

constexpr std::uint8_t SWEEP_LAST_PERIOD = 7;

constexpr std::uint8_t TUNE_RESTART = 'G';
constexpr std::uint8_t TUNE_REST = 'F';
constexpr std::uint8_t NOTE_TICKS_PER_BEAT = 0x24;
constexpr std::uint8_t NOTE_BASE = 0x0C;    // notePeriods starts an octave up
constexpr std::uint16_t REST_PERIOD = 0x32; // inaudible

// The byte at _field less one, stored back: DEC BYTE PTR. Returns what it leaves.
std::uint8_t Decrement(Field<std::uint8_t>& _field)
{
  const auto value = static_cast<std::uint8_t>(_field - 1);
  _field = value;
  return value;
}

// _image to speakerPortImage and to port 61h.
void OutSpeaker(GameState& _state, Hardware& _hardware, std::uint8_t _image)
{
  _state.Data().audio.speakerPortImage = _image;
  _hardware.SetSystemControl(_image);
}

// A beep's square wave: bit 1 of _bit replaces the speaker's data bit.
void SetSpeakerData(GameState& _state, Hardware& _hardware, std::uint8_t _bit)
{
  OutSpeaker(_state, _hardware, static_cast<std::uint8_t>((_state.Data().audio.speakerPortImage & ~SPEAKER_DATA) | _bit));
}

// TimerTickMusic (0x72FF): titleTune's notes, each a byte (negative: no gap after it, 'F' a rest, 'G' the end) and a
// length in beats. A note change (0x7372) sets the PIT's channel 2 to its period.
void TickMusic(GameState& _state, Hardware& _hardware)
{
  const std::uint16_t ticksLeft = _state.Data().audio.noteTicksLeft;
  if (ticksLeft != 0)
  {
    _state.Data().audio.noteTicksLeft = static_cast<std::uint16_t>(ticksLeft - 1);
    if (ticksLeft != 1)
    {
      return;
    }
    // The note is over: its gap, if it has one, as a rest.
    const std::uint16_t gapTicks = _state.Data().audio.noteGapTicks;
    if (gapTicks == 0)
    {
      return;
    }
    _state.Data().audio.noteGapTicks = 0;
    _state.Data().audio.noteTicksLeft = gapTicks;
    _hardware.SetToneDivisor(REST_PERIOD);
    return;
  }

  // musicPointer is the index of the tune's next entry, two bytes each, where the original holds its address. The tune's last
  // entry restarts it.
  const std::uint16_t next = _state.Data().audio.musicPointer;
  const std::size_t entry = std::size_t{next} * 2;
  const std::uint8_t note = TITLE_TUNE[entry];
  const std::uint8_t beats = TITLE_TUNE[entry + 1];
  const auto ticks = static_cast<std::uint16_t>(NOTE_TICKS_PER_BEAT * beats);
  if (note == TUNE_RESTART)
  {
    _state.Data().audio.musicPointer = 0;
  }
  else
  {
    _state.Data().audio.musicPointer = static_cast<std::uint16_t>(next + 1);
    if (note != TUNE_REST)
    {
      std::uint8_t pitch = note;
      std::uint16_t sounding = ticks;
      if ((note & 0x80) != 0)
      {
        _state.Data().audio.noteGapTicks = 0;
        pitch = static_cast<std::uint8_t>(0u - note);
      }
      else
      {
        // An eighth of the note is a gap.
        const auto gap = static_cast<std::uint16_t>(ticks >> 3);
        _state.Data().audio.noteGapTicks = gap;
        sounding = static_cast<std::uint16_t>(ticks - gap);
      }
      _state.Data().audio.noteTicksLeft = sounding;
      const auto index = static_cast<std::uint8_t>(pitch - NOTE_BASE);
      _hardware.SetToneDivisor(NOTE_PERIODS[index]);
      return;
    }
  }
  _state.Data().audio.noteGapTicks = 0;
  _state.Data().audio.noteTicksLeft = ticks;
  _hardware.SetToneDivisor(REST_PERIOD);
}

// A sweep's sample: EmitNoiseSample for the noise sweep, ToggleSpeaker for the tone sweep. Each returns the port 61h
// value, which the sweep does not use.
using SweepSample = std::uint8_t (*)(GameState&, Hardware&);

// The noise and tone sweeps (0x7215, 0x7289): every sweepPeriodTicks ticks one sample, by _sample; every
// sweepStepLength samples a longer period and a shorter step, until the period reaches 7.
void TickSweep(GameState& _state, Hardware& _hardware, SweepSample _sample, Field<std::uint8_t>& _active)
{
  if (Decrement(_state.Data().audio.sweepTickCounter) != 0)
  {
    return;
  }
  _state.Data().audio.sweepTickCounter = std::uint8_t{_state.Data().audio.sweepPeriodTicks};
  _sample(_state, _hardware);
  if (Decrement(_state.Data().audio.sweepStepCounter) != 0)
  {
    return;
  }
  _state.Data().audio.sweepStepCounter = std::uint8_t{_state.Data().audio.sweepStepLength};
  const auto period = static_cast<std::uint8_t>(_state.Data().audio.sweepPeriodTicks + 1);
  _state.Data().audio.sweepPeriodTicks = period;
  _state.Data().audio.sweepStepLength =
    static_cast<std::uint8_t>(_state.Data().audio.sweepStepLength - _state.Data().sound.sweepStepShrink);
  if (period == SWEEP_LAST_PERIOD)
  {
    _active = 0;
  }
}

// TimerTickSoundEffects (0x717F): the beeps, then the one effect of highest priority.
void TickSoundEffects(GameState& _state, Hardware& _hardware)
{
  if (_state.Data().audio.beepTicks != 0)
  {
    SetSpeakerData(_state, _hardware, static_cast<std::uint8_t>((Decrement(_state.Data().audio.beepTicks) << 1) & SPEAKER_DATA));
  }
  if (_state.Data().audio.lowBeepTicks != 0)
  {
    SetSpeakerData(_state, _hardware, static_cast<std::uint8_t>(Decrement(_state.Data().audio.lowBeepTicks) & SPEAKER_DATA));
  }
  if (_state.Data().audio.noiseSweepActive == 1)
  {
    TickSweep(_state, _hardware, &EmitNoiseSample, _state.Data().audio.noiseSweepActive);
    return;
  }
  if (_state.Data().audio.noiseBurstTicks != 0)
  {
    Decrement(_state.Data().audio.noiseBurstTicks);
    EmitNoiseSample(_state, _hardware);
    return;
  }
  if (_state.Data().audio.toneSweepActive == 1)
  {
    TickSweep(_state, _hardware, &ToggleSpeaker, _state.Data().audio.toneSweepActive);
  }
}

} // namespace

void InstallTimerInterrupt(GameState& _state, Hardware& _hardware)
{
  _hardware.SetTickDivisor(TICK_DIVISOR);
  const std::uint8_t speaker = _hardware.SystemControl();
  _state.Data().timer.savedSpeakerPort = speaker;
  const auto image = static_cast<std::uint8_t>(speaker & ~SPEAKER_BITS);
  _state.Data().audio.speakerPortImage = image;
  _hardware.SetSystemControl(image);
  // The game's own handler, at CS:0215, installed in place of the BIOS's, which the original keeps in savedTimerVector and
  // native code puts back by name (RestoreTimerInterrupt).
  _hardware.SetHandler(Irq::Timer, IrqHandler::Game);
  _hardware.EnableInterrupts();
}

BiosClock RestoreTimerInterrupt(GameState& _state, Hardware& _hardware)
{
  _hardware.DisableInterrupts();
  // The BIOS's handler put back, as InstallTimerInterrupt found it.
  _hardware.SetHandler(Irq::Timer, IrqHandler::Bios);
  _hardware.SetSystemControl(_state.Data().timer.savedSpeakerPort);
  _hardware.SetTickDivisor(0);
  _hardware.EnableInterrupts();
  return _hardware.ReadBiosClock();
}

std::uint16_t ClockLessADayHigh(const BiosClock& _clock) noexcept
{
  return static_cast<std::uint16_t>((_clock.ticks - TICKS_PER_DAY) >> 16);
}

void TimerInterrupt(GameState& _state, Hardware& _hardware)
{
  // PUSH AX / PUSH DS / PUSH ES, and their POPs before the IRET or the chain: what it loads into them, the data segment, B800h
  // and the end of the interrupt's 20h, goes no further.
  TimerTick(_state, _hardware);
  _hardware.EndOfInterrupt();
}

void TimerTick(GameState& _state, Hardware& _hardware)
{
  _state.Data().timer.millisecondCounter = static_cast<std::uint16_t>(_state.Data().timer.millisecondCounter + 1);
  _state.Data().timer.timerTicks = static_cast<std::uint16_t>(_state.Data().timer.timerTicks + 1);
  _state.Data().flow.msSinceFrame = static_cast<std::uint8_t>(_state.Data().flow.msSinceFrame + 1);
  // The original checks the copy protection's answer here while a question is pending (CheckProtectionAnswer, CS:739A), and
  // none is: protectionQuestion stays FFh, since the D5 byte keeps the protection from asking one (ADR-001).
  if (_state.Data().flight.soundEnabled == 0 || _state.Data().flight.gamePaused == 1)
  {
    return;
  }
  if (_state.Data().sound.musicPlaying == 1)
  {
    TickMusic(_state, _hardware);
    return;
  }
  TickSoundEffects(_state, _hardware);
}

void WaitForTimerTick(GameState& _state, Hardware& _hardware)
{
  // PUSH AX, MOV AX,[timerTicks], then the compare at 7776 and JE back to it, and POP AX: AX holds the tick it started at. The
  // stack the PUSH used is not state the game reads, nor the digest (ADR-008 item 5).
  const std::uint16_t ticks = _state.Data().timer.timerTicks;
  while (ticks == _state.Data().timer.timerTicks)
  {
    _hardware.Wait();
  }
}

// ── The entry of the timer's interrupt handler ──

void TimerInterruptEntry(Guest& _guest)
{
  // AX, DS and ES pushed round what it loads into them, and popped: every register is kept, and the IRET takes the flags.
  TimerInterrupt(_guest.State(), _guest.Devices());
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{EntryKind::TimerHandler, &TimerInterruptEntry}, // CS:0215
};

} // namespace

std::span<const NativeEntry> TimerEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
