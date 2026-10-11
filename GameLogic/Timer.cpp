#include "pch.h"

#include "Timer.h"

#include "Arithmetic.h"
#include "DataOverlay.h"
#include "Input.h"
#include "Sound.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t TIMER_INTERRUPT = 0x0215;

// The BIOS's int 8 vector, which InstallTimerInterrupt keeps in the code segment (savedTimerVector).
constexpr std::uint16_t SAVED_TIMER_OFFSET = 0x025A;
constexpr std::uint16_t SAVED_TIMER_SEGMENT = 0x025C;
constexpr std::uint8_t TIMER_VECTOR = 0x08;

constexpr std::uint8_t PIT_CHANNEL0_SQUARE_WAVE = 0x36; // channel 0, both bytes, mode 3
constexpr std::uint8_t PIT_CHANNEL0_RATE = 0x34;        // channel 0, both bytes, mode 2: the Amstrad's mouse
constexpr std::uint16_t TICK_DIVISOR = 0x04A9;          // about 1000.15 Hz
constexpr std::uint16_t AMSTRAD_MOUSE_DIVISOR = 0x5555;
constexpr std::uint32_t TICKS_PER_DAY = 0x1800B0;
constexpr std::uint8_t SPEAKER_BITS = 0x03; // gate and data, cleared at start-up
constexpr std::uint8_t SPEAKER_DATA = 0x02;

constexpr std::uint8_t NO_QUESTION = 0xFF;
constexpr std::uint8_t HUM_TICKS = 0x0F;
constexpr std::uint8_t SWEEP_LAST_PERIOD = 7;
constexpr std::uint8_t SLOW_NOISE_TICKS = 5;
constexpr std::uint16_t TWO_TONE_TICKS = 0x12C;
constexpr std::uint8_t SIREN_TICKS = 0x4B;
constexpr std::uint8_t SIREN_FIRST_STAGE = 2;
constexpr std::uint8_t BIOS_CHAIN_TICKS_WITH_MOUSE = 55;
constexpr std::uint8_t BIOS_CHAIN_TICKS = 18;

constexpr std::uint8_t TUNE_RESTART = 'G';
constexpr std::uint8_t TUNE_REST = 'F';
constexpr std::uint8_t NOTE_TICKS_PER_BEAT = 0x24;
constexpr std::uint8_t NOTE_BASE = 0x0C;    // notePeriods starts an octave up
constexpr std::uint16_t REST_PERIOD = 0x32; // inaudible

constexpr std::uint16_t DECODED_ANSWER = 0xA5B7; // a length byte and the decoded answer, past dockingKeyReleased
constexpr std::uint8_t ANSWER_KEY = 0x61;

// Where WaitForTimerTick's loop jumps back to: the compare.
constexpr std::uint16_t TIMER_TICK_COMPARE = 0x7776;

// The byte at _field less one, stored back: DEC BYTE PTR. Returns what it leaves.
std::uint8_t Decrement(Counted<std::uint8_t>& _field)
{
  const auto value = static_cast<std::uint8_t>(_field - 1);
  _field = value;
  return value;
}

// _image to speakerPortImage and to port 61h.
void OutSpeaker(GameState& _state, Hardware& _hardware, std::uint8_t _image)
{
  _state.Data().speakerPortImage = _image;
  _hardware.SetSystemControl(_image);
}

// A beep's square wave: bit 1 of _bit replaces the speaker's data bit.
void SetSpeakerData(GameState& _state, Hardware& _hardware, std::uint8_t _bit)
{
  OutSpeaker(_state, _hardware, static_cast<std::uint8_t>((_state.Data().speakerPortImage & ~SPEAKER_DATA) | _bit));
}

void ToggleSpeakerData(GameState& _state, Hardware& _hardware)
{
  OutSpeaker(_state, _hardware, static_cast<std::uint8_t>(_state.Data().speakerPortImage ^ SPEAKER_DATA));
}

// CheckProtectionAnswer (0x739A): decodes the expected answer to the pending question into DS:A5B7 and compares the
// typed one with it, by REPE CMPSB, backwards when _backward (the direction flag set).
void CheckProtectionAnswer(GameState& _state, bool _backward)
{
  const std::uint8_t typed = _state.Data().protectionInput.data25CA;
  if (typed == 0)
  {
    return;
  }
  const std::uint16_t descriptor = _state.Get(DS.protectionDescriptor);
  std::uint16_t answer = _state.Word(static_cast<std::uint16_t>(descriptor + 0x0A));
  const std::uint8_t question = _state.Data().protectionQuestion;
  // The answers are length-prefixed, one after another: skip to the question's.
  for (auto left = static_cast<std::uint8_t>(question - 1); left != 0; --left)
  {
    answer = static_cast<std::uint16_t>(answer + static_cast<std::uint8_t>(_state.Byte(answer) + 1));
  }
  const std::uint8_t length = _state.Byte(answer);
  if (length != typed)
  {
    return;
  }
  ++answer;
  std::uint16_t decoded = DECODED_ANSWER;
  _state.SetByte(decoded++, length);
  std::uint8_t key = 1;
  for (std::uint8_t left = length; left != 0; --left)
  {
    const auto plain = static_cast<std::uint8_t>(_state.Byte(answer++) ^ question ^ key ^ ANSWER_KEY);
    ++key;
    _state.SetByte(decoded++, plain);
  }
  // REPE CMPSB of the decoded answer (DS:SI) with the typed one (ES:DI, ES=DS).
  std::uint16_t expected = DECODED_ANSWER + 1;
  auto given = static_cast<std::uint16_t>(DS.protectionInput.offset + 2);
  bool equal = false;
  for (std::uint8_t left = _state.Byte(DECODED_ANSWER); left != 0; --left)
  {
    equal = _state.Byte(expected) == _state.Byte(given);
    expected = static_cast<std::uint16_t>(_backward ? expected - 1 : expected + 1);
    given = static_cast<std::uint16_t>(_backward ? given - 1 : given + 1);
    if (!equal)
    {
      break;
    }
  }
  if (equal)
  {
    _state.Data().protectionAnswerCorrect = 1;
  }
}

// TimerTickMusic (0x72FF): titleTune's notes, each a byte (negative: no gap after it, 'F' a rest, 'G' the end) and a
// length in beats. A note change (0x7372) sets the PIT's channel 2 to its period.
void TickMusic(GameState& _state, Hardware& _hardware)
{
  const std::uint16_t ticksLeft = _state.Data().noteTicksLeft;
  if (ticksLeft != 0)
  {
    _state.Data().noteTicksLeft = static_cast<std::uint16_t>(ticksLeft - 1);
    if (ticksLeft != 1)
    {
      return;
    }
    // The note is over: its gap, if it has one, as a rest.
    const std::uint16_t gapTicks = _state.Data().noteGapTicks;
    if (gapTicks == 0)
    {
      return;
    }
    _state.Data().noteGapTicks = 0;
    _state.Data().noteTicksLeft = gapTicks;
    _hardware.SetToneDivisor(REST_PERIOD);
    return;
  }

  // musicPointer is the index of the tune's next entry, two bytes each, where the original holds its address. The tune's last
  // entry restarts it.
  const std::uint16_t next = _state.Data().musicPointer;
  const std::size_t entry = std::size_t{next} * 2;
  const std::uint8_t note = _state.Data().titleTune[entry];
  const std::uint8_t beats = _state.Data().titleTune[entry + 1];
  const auto ticks = static_cast<std::uint16_t>(NOTE_TICKS_PER_BEAT * beats);
  if (note == TUNE_RESTART)
  {
    _state.Data().musicPointer = 0;
  }
  else
  {
    _state.Data().musicPointer = static_cast<std::uint16_t>(next + 1);
    if (note != TUNE_REST)
    {
      std::uint8_t pitch = note;
      std::uint16_t sounding = ticks;
      if ((note & 0x80) != 0)
      {
        _state.Data().noteGapTicks = 0;
        pitch = static_cast<std::uint8_t>(0u - note);
      }
      else
      {
        // An eighth of the note is a gap.
        const auto gap = static_cast<std::uint16_t>(ticks >> 3);
        _state.Data().noteGapTicks = gap;
        sounding = static_cast<std::uint16_t>(ticks - gap);
      }
      _state.Data().noteTicksLeft = sounding;
      const auto index = static_cast<std::uint8_t>(pitch - NOTE_BASE);
      _hardware.SetToneDivisor(_state.Data().notePeriods[index]);
      return;
    }
  }
  _state.Data().noteGapTicks = 0;
  _state.Data().noteTicksLeft = ticks;
  _hardware.SetToneDivisor(REST_PERIOD);
}

// A sweep's sample: EmitNoiseSample for the noise sweep, ToggleSpeaker for the tone sweep. Each returns the port 61h
// value, which the sweep does not use.
using SweepSample = std::uint8_t (*)(GameState&, Hardware&);

// The noise and tone sweeps (0x7215, 0x7289): every sweepPeriodTicks ticks one sample, by _sample; every
// sweepStepLength samples a longer period and a shorter step, until the period reaches 7.
void TickSweep(GameState& _state, Hardware& _hardware, SweepSample _sample, Counted<std::uint8_t>& _active)
{
  if (Decrement(_state.Data().sweepTickCounter) != 0)
  {
    return;
  }
  _state.Data().sweepTickCounter = std::uint8_t{_state.Data().sweepPeriodTicks};
  _sample(_state, _hardware);
  if (Decrement(_state.Data().sweepStepCounter) != 0)
  {
    return;
  }
  _state.Data().sweepStepCounter = std::uint8_t{_state.Data().sweepStepLength};
  const auto period = static_cast<std::uint8_t>(_state.Data().sweepPeriodTicks + 1);
  _state.Data().sweepPeriodTicks = period;
  _state.Data().sweepStepLength = static_cast<std::uint8_t>(_state.Data().sweepStepLength - _state.Data().sweepStepShrink);
  if (period == SWEEP_LAST_PERIOD)
  {
    _active = 0;
  }
}

// The siren (0x72C3): every other tick, a square wave of sirenCountdown's bit 0 or, in stage 1, of every tick.
void TickSiren(GameState& _state, Hardware& _hardware)
{
  const auto half = static_cast<std::uint8_t>(_state.Data().sirenHalfTick ^ 1);
  _state.Data().sirenHalfTick = half;
  if (half != 0)
  {
    return;
  }
  const std::uint8_t countdown = Decrement(_state.Data().sirenCountdown);
  if (countdown == 0)
  {
    const std::uint8_t stage = Decrement(_state.Data().sirenStage);
    _state.Data().sirenCountdown = SIREN_TICKS;
    if (stage == 0)
    {
      _state.Data().sirenStage = SIREN_FIRST_STAGE;
      return;
    }
    _state.Data().sirenCountdown = static_cast<std::uint8_t>(SIREN_TICKS << 1);
    return;
  }
  if (_state.Data().sirenStage == 1 || (countdown & 1) == 0)
  {
    ToggleSpeakerData(_state, _hardware);
  }
}

// The two-tone (0x725E): twoToneTicks of a square wave on every tick in stage 1, every other tick otherwise.
void TickTwoTone(GameState& _state, Hardware& _hardware)
{
  const auto ticks = static_cast<std::uint16_t>(_state.Data().twoToneTicks - 1);
  _state.Data().twoToneTicks = ticks;
  if (ticks == 0)
  {
    Decrement(_state.Data().twoToneStage);
    _state.Data().twoToneTicks = TWO_TONE_TICKS;
    return;
  }
  if (_state.Data().twoToneStage == 1 || (ticks & 1) == 0)
  {
    ToggleSpeakerData(_state, _hardware);
  }
}

// TimerTickSoundEffects (0x717F): the beeps, then the one effect of highest priority.
void TickSoundEffects(GameState& _state, Hardware& _hardware)
{
  if (_state.Data().beepTicks != 0)
  {
    SetSpeakerData(_state, _hardware, static_cast<std::uint8_t>((Decrement(_state.Data().beepTicks) << 1) & SPEAKER_DATA));
  }
  if (_state.Data().lowBeepTicks != 0)
  {
    SetSpeakerData(_state, _hardware, static_cast<std::uint8_t>(Decrement(_state.Data().lowBeepTicks) & SPEAKER_DATA));
  }
  if (_state.Data().sirenEnabled == 1)
  {
    TickSiren(_state, _hardware);
    return;
  }
  if (_state.Data().noiseSweepActive == 1)
  {
    TickSweep(_state, _hardware, &EmitNoiseSample, _state.Data().noiseSweepActive);
    return;
  }
  if (_state.Data().noiseBurstTicks != 0)
  {
    Decrement(_state.Data().noiseBurstTicks);
    EmitNoiseSample(_state, _hardware);
    return;
  }
  if (_state.Data().toneSweepActive == 1)
  {
    TickSweep(_state, _hardware, &ToggleSpeaker, _state.Data().toneSweepActive);
    return;
  }
  if (_state.Data().twoToneStage != 0)
  {
    TickTwoTone(_state, _hardware);
    return;
  }
  if (_state.Data().slowNoiseCount != 0)
  {
    if (Decrement(_state.Data().slowNoiseDivider) != 0)
    {
      return;
    }
    _state.Data().slowNoiseDivider = SLOW_NOISE_TICKS;
    Decrement(_state.Data().slowNoiseCount);
    EmitNoiseSample(_state, _hardware);
    return;
  }
  if (_state.Data().humEnabled == 1)
  {
    if (Decrement(_state.Data().humCountdownTicks) != 0)
    {
      return;
    }
    _state.Data().humCountdownTicks = HUM_TICKS;
    ToggleSpeakerData(_state, _hardware);
    return;
  }
  if (_state.Data().continuousNoise == 1)
  {
    EmitNoiseSample(_state, _hardware);
  }
}

// TimerInterrupt's chain to the BIOS (0x0255): a far jump, through savedTimerVector, to the BIOS's handler for the tick, with
// the interrupt's frame in place, so that the BIOS's IRET ends the interrupt. Here the handler is run to its IRET, which comes
// back to this routine, and TimerInterrupt's own IRET takes the frame.
void ChainToBiosTimer(const GameState& _state, Hardware& _hardware)
{
  _hardware.RunBiosTimerTick(_state.CodeWord(SAVED_TIMER_SEGMENT), _state.CodeWord(SAVED_TIMER_OFFSET));
}

} // namespace

void InstallTimerInterrupt(GameState& _state, Hardware& _hardware)
{
  _hardware.SetTickDivisor(PIT_CHANNEL0_SQUARE_WAVE, TICK_DIVISOR);
  const std::uint8_t speaker = _hardware.SystemControl();
  _state.Data().savedSpeakerPort = speaker;
  const auto image = static_cast<std::uint8_t>(speaker & ~SPEAKER_BITS);
  _state.Data().speakerPortImage = image;
  _hardware.SetSystemControl(image);
  // The BIOS's handler kept, and the game's own installed in its place.
  const FarAddress bios = _hardware.InterruptVector(TIMER_VECTOR);
  _state.SetCodeWord(SAVED_TIMER_OFFSET, bios.offset);
  _state.SetCodeWord(SAVED_TIMER_SEGMENT, bios.segment);
  _hardware.SetInterruptVector(TIMER_VECTOR, FarAddress{_state.CodeSegment(), TIMER_INTERRUPT});
  _hardware.EnableInterrupts();
}

BiosClock RestoreTimerInterrupt(GameState& _state, Hardware& _hardware)
{
  _hardware.DisableInterrupts();
  // The BIOS's handler put back, from what InstallTimerInterrupt kept.
  _hardware.SetInterruptVector(TIMER_VECTOR, FarAddress{_state.CodeWord(SAVED_TIMER_SEGMENT), _state.CodeWord(SAVED_TIMER_OFFSET)});
  _hardware.SetSystemControl(_state.Data().savedSpeakerPort);
  std::uint8_t mode = PIT_CHANNEL0_SQUARE_WAVE;
  std::uint16_t divisor = 0;
  if (_state.Data().amstradPresent == 1)
  {
    // The Amstrad's mouse driver keeps its own rate.
    mode = PIT_CHANNEL0_RATE;
    if (IsMouseDriverInstalled(_hardware).installed)
    {
      divisor = AMSTRAD_MOUSE_DIVISOR;
    }
  }
  _hardware.SetTickDivisor(mode, divisor);
  _hardware.EnableInterrupts();

  // The BIOS clock past a day is set back by one: SUB DX / SBB CX.
  const BiosClock clock = _hardware.ReadBiosClock();
  if (clock.ticks >= TICKS_PER_DAY)
  {
    _hardware.SetBiosClock(clock.ticks - TICKS_PER_DAY);
  }
  return clock;
}

std::uint16_t ClockLessADayHigh(const BiosClock& _clock) noexcept
{
  return static_cast<std::uint16_t>((_clock.ticks - TICKS_PER_DAY) >> 16);
}

void TimerInterrupt(GameState& _state, Hardware& _hardware, bool _backward)
{
  // PUSH AX / PUSH DS / PUSH ES, and their POPs before the IRET or the chain: what it loads into them, the data segment, B800h
  // and the end of the interrupt's 20h, goes no further.
  TimerTick(_state, _hardware, _backward);
  _hardware.EndOfInterrupt();
  if (_state.Data().amstradPresent != 1)
  {
    return;
  }
  // The Amstrad's BIOS clock still needs its 18.2 Hz: every 18th tick, or every 55th with the mouse driver's rate.
  const bool mouse = IsMouseDriverInstalled(_hardware).installed;
  if (Decrement(_state.Data().biosTimerChainCountdown) != 0)
  {
    return;
  }
  _state.Data().biosTimerChainCountdown = mouse ? BIOS_CHAIN_TICKS_WITH_MOUSE : BIOS_CHAIN_TICKS;
  ChainToBiosTimer(_state, _hardware);
}

void TimerTick(GameState& _state, Hardware& _hardware, bool _backward)
{
  _state.Data().millisecondCounter = static_cast<std::uint16_t>(_state.Data().millisecondCounter + 1);
  _state.Data().timerTicks = static_cast<std::uint16_t>(_state.Data().timerTicks + 1);
  _state.Data().msSinceFrame = static_cast<std::uint8_t>(_state.Data().msSinceFrame + 1);
  if (_state.Data().protectionQuestion != NO_QUESTION)
  {
    CheckProtectionAnswer(_state, _backward);
    return;
  }
  if (_state.Data().soundEnabled == 0 || _state.Data().gamePaused == 1)
  {
    return;
  }
  if (_state.Data().musicPlaying == 1)
  {
    TickMusic(_state, _hardware);
    return;
  }
  TickSoundEffects(_state, _hardware);
}

void WaitForTimerTick(GameState& _state, Hardware& _hardware)
{
  // PUSH AX, MOV AX,[timerTicks], then the compare at 7776 and JE back to it, and POP AX: the loop carries in AX the
  // tick it started at. The stack the PUSH used is not state the game reads, nor the digest (ADR-008 item 5).
  const std::uint16_t ticks = _state.Data().timerTicks;
  while (ticks == _state.Data().timerTicks)
  {
    _hardware.LoopTurn(TIMER_TICK_COMPARE, {ticks});
  }
}

// ── The entry of the timer's interrupt handler ──

void TimerInterruptEntry(Guest& _guest)
{
  // AX, DS and ES pushed round what it loads into them, and popped: every register is kept, and the IRET takes the flags.
  TimerInterrupt(_guest.State(), _guest.Devices(), _guest.Backward());
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{TIMER_INTERRUPT, "TimerInterrupt", &TimerInterruptEntry, EntryReturn::Interrupt},
};

} // namespace

std::span<const NativeEntry> TimerEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
