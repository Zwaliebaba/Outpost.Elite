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

constexpr std::uint16_t IS_MOUSE_DRIVER_INSTALLED = 0x02D4;
constexpr std::uint16_t TIMER_INTERRUPT = 0x0215;

// The BIOS's int 8 vector, which InstallTimerInterrupt keeps in the code segment (savedTimerVector).
constexpr std::uint16_t SAVED_TIMER_OFFSET = 0x025A;
constexpr std::uint16_t SAVED_TIMER_SEGMENT = 0x025C;
constexpr std::uint16_t VECTOR_TABLE_SEGMENT = 0x0000;
constexpr std::uint16_t TIMER_VECTOR_OFFSET = 0x0020; // 0000:0020, int 8
constexpr std::uint16_t TIMER_VECTOR_SEGMENT = 0x0022;
constexpr std::uint8_t TIMER_VECTOR = 0x08;
constexpr std::uint8_t TIME_OF_DAY_VECTOR = 0x1A;

constexpr std::uint8_t END_OF_INTERRUPT = 0x20;
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
std::uint8_t Decrement(GameState& _state, DataField<std::uint8_t> _field)
{
  const auto value = static_cast<std::uint8_t>(_state.Get(_field) - 1);
  _state.Set(_field, value);
  return value;
}

// _image to speakerPortImage and to port 61h.
void OutSpeaker(GameState& _state, Hardware& _hardware, std::uint8_t _image)
{
  _state.Set(DS.speakerPortImage, _image);
  _hardware.SetSystemControl(_image);
}

// A beep's square wave: bit 1 of _bit replaces the speaker's data bit.
void SetSpeakerData(GameState& _state, Hardware& _hardware, std::uint8_t _bit)
{
  OutSpeaker(_state, _hardware, static_cast<std::uint8_t>((_state.Get(DS.speakerPortImage) & ~SPEAKER_DATA) | _bit));
}

void ToggleSpeakerData(GameState& _state, Hardware& _hardware)
{
  OutSpeaker(_state, _hardware, static_cast<std::uint8_t>(_state.Get(DS.speakerPortImage) ^ SPEAKER_DATA));
}

// CheckProtectionAnswer (0x739A): decodes the expected answer to the pending question into DS:A5B7 and compares the
// typed one with it, by REPE CMPSB, backwards when _backward (the direction flag set).
void CheckProtectionAnswer(GameState& _state, bool _backward)
{
  const std::uint8_t typed = _state.Get(DS.data25CA);
  if (typed == 0)
  {
    return;
  }
  const std::uint16_t descriptor = _state.Get(DS.protectionDescriptor);
  std::uint16_t answer = _state.Word(static_cast<std::uint16_t>(descriptor + 0x0A));
  const std::uint8_t question = _state.Get(DS.protectionQuestion);
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
    _state.Set(DS.protectionAnswerCorrect, 1);
  }
}

// TimerTickMusic (0x72FF): titleTune's notes, each a byte (negative: no gap after it, 'F' a rest, 'G' the end) and a
// length in beats. A note change (0x7372) sets the PIT's channel 2 to its period.
void TickMusic(GameState& _state, Hardware& _hardware)
{
  const std::uint16_t ticksLeft = _state.Get(DS.noteTicksLeft);
  if (ticksLeft != 0)
  {
    _state.Set(DS.noteTicksLeft, static_cast<std::uint16_t>(ticksLeft - 1));
    if (ticksLeft != 1)
    {
      return;
    }
    // The note is over: its gap, if it has one, as a rest.
    const std::uint16_t gapTicks = _state.Get(DS.noteGapTicks);
    if (gapTicks == 0)
    {
      return;
    }
    _state.Set(DS.noteGapTicks, 0);
    _state.Set(DS.noteTicksLeft, gapTicks);
    _hardware.SetToneDivisor(REST_PERIOD);
    return;
  }

  std::uint16_t pointer = _state.Get(DS.musicPointer);
  const std::uint16_t entry = _state.Word(pointer);
  const std::uint8_t note = Low(entry);
  const std::uint8_t beats = High(entry);
  const auto ticks = static_cast<std::uint16_t>(NOTE_TICKS_PER_BEAT * beats);
  if (note == TUNE_RESTART)
  {
    _state.Set(DS.musicPointer, DS.titleTune.offset);
  }
  else
  {
    pointer = static_cast<std::uint16_t>(pointer + 2);
    _state.Set(DS.musicPointer, pointer);
    if (note != TUNE_REST)
    {
      std::uint8_t pitch = note;
      std::uint16_t sounding = ticks;
      if ((note & 0x80) != 0)
      {
        _state.Set(DS.noteGapTicks, 0);
        pitch = static_cast<std::uint8_t>(0u - note);
      }
      else
      {
        // An eighth of the note is a gap.
        const auto gap = static_cast<std::uint16_t>(ticks >> 3);
        _state.Set(DS.noteGapTicks, gap);
        sounding = static_cast<std::uint16_t>(ticks - gap);
      }
      _state.Set(DS.noteTicksLeft, sounding);
      const auto index = static_cast<std::uint8_t>(pitch - NOTE_BASE);
      _hardware.SetToneDivisor(_state.Word(DS.notePeriods.At(index)));
      return;
    }
  }
  _state.Set(DS.noteGapTicks, 0);
  _state.Set(DS.noteTicksLeft, ticks);
  _hardware.SetToneDivisor(REST_PERIOD);
}

// A sweep's sample: EmitNoiseSample for the noise sweep, ToggleSpeaker for the tone sweep. Each returns the port 61h
// value, which the sweep does not use.
using SweepSample = std::uint8_t (*)(GameState&, Hardware&);

// The noise and tone sweeps (0x7215, 0x7289): every sweepPeriodTicks ticks one sample, by _sample; every
// sweepStepLength samples a longer period and a shorter step, until the period reaches 7.
void TickSweep(GameState& _state, Hardware& _hardware, SweepSample _sample, DataField<std::uint8_t> _active)
{
  if (Decrement(_state, DS.sweepTickCounter) != 0)
  {
    return;
  }
  _state.Set(DS.sweepTickCounter, _state.Get(DS.sweepPeriodTicks));
  _sample(_state, _hardware);
  if (Decrement(_state, DS.sweepStepCounter) != 0)
  {
    return;
  }
  _state.Set(DS.sweepStepCounter, _state.Get(DS.sweepStepLength));
  const auto period = static_cast<std::uint8_t>(_state.Get(DS.sweepPeriodTicks) + 1);
  _state.Set(DS.sweepPeriodTicks, period);
  _state.Set(DS.sweepStepLength, static_cast<std::uint8_t>(_state.Get(DS.sweepStepLength) - _state.Get(DS.sweepStepShrink)));
  if (period == SWEEP_LAST_PERIOD)
  {
    _state.Set(_active, 0);
  }
}

// The siren (0x72C3): every other tick, a square wave of sirenCountdown's bit 0 or, in stage 1, of every tick.
void TickSiren(GameState& _state, Hardware& _hardware)
{
  const auto half = static_cast<std::uint8_t>(_state.Get(DS.sirenHalfTick) ^ 1);
  _state.Set(DS.sirenHalfTick, half);
  if (half != 0)
  {
    return;
  }
  const std::uint8_t countdown = Decrement(_state, DS.sirenCountdown);
  if (countdown == 0)
  {
    const std::uint8_t stage = Decrement(_state, DS.sirenStage);
    _state.Set(DS.sirenCountdown, SIREN_TICKS);
    if (stage == 0)
    {
      _state.Set(DS.sirenStage, SIREN_FIRST_STAGE);
      return;
    }
    _state.Set(DS.sirenCountdown, static_cast<std::uint8_t>(SIREN_TICKS << 1));
    return;
  }
  if (_state.Get(DS.sirenStage) == 1 || (countdown & 1) == 0)
  {
    ToggleSpeakerData(_state, _hardware);
  }
}

// The two-tone (0x725E): twoToneTicks of a square wave on every tick in stage 1, every other tick otherwise.
void TickTwoTone(GameState& _state, Hardware& _hardware)
{
  const auto ticks = static_cast<std::uint16_t>(_state.Get(DS.twoToneTicks) - 1);
  _state.Set(DS.twoToneTicks, ticks);
  if (ticks == 0)
  {
    Decrement(_state, DS.twoToneStage);
    _state.Set(DS.twoToneTicks, TWO_TONE_TICKS);
    return;
  }
  if (_state.Get(DS.twoToneStage) == 1 || (ticks & 1) == 0)
  {
    ToggleSpeakerData(_state, _hardware);
  }
}

// TimerTickSoundEffects (0x717F): the beeps, then the one effect of highest priority.
void TickSoundEffects(GameState& _state, Hardware& _hardware)
{
  if (_state.Get(DS.beepTicks) != 0)
  {
    SetSpeakerData(_state, _hardware, static_cast<std::uint8_t>((Decrement(_state, DS.beepTicks) << 1) & SPEAKER_DATA));
  }
  if (_state.Get(DS.lowBeepTicks) != 0)
  {
    SetSpeakerData(_state, _hardware, static_cast<std::uint8_t>(Decrement(_state, DS.lowBeepTicks) & SPEAKER_DATA));
  }
  if (_state.Get(DS.sirenEnabled) == 1)
  {
    TickSiren(_state, _hardware);
    return;
  }
  if (_state.Get(DS.noiseSweepActive) == 1)
  {
    TickSweep(_state, _hardware, &EmitNoiseSample, DS.noiseSweepActive);
    return;
  }
  if (_state.Get(DS.noiseBurstTicks) != 0)
  {
    Decrement(_state, DS.noiseBurstTicks);
    EmitNoiseSample(_state, _hardware);
    return;
  }
  if (_state.Get(DS.toneSweepActive) == 1)
  {
    TickSweep(_state, _hardware, &ToggleSpeaker, DS.toneSweepActive);
    return;
  }
  if (_state.Get(DS.twoToneStage) != 0)
  {
    TickTwoTone(_state, _hardware);
    return;
  }
  if (_state.Get(DS.slowNoiseCount) != 0)
  {
    if (Decrement(_state, DS.slowNoiseDivider) != 0)
    {
      return;
    }
    _state.Set(DS.slowNoiseDivider, SLOW_NOISE_TICKS);
    Decrement(_state, DS.slowNoiseCount);
    EmitNoiseSample(_state, _hardware);
    return;
  }
  if (_state.Get(DS.humEnabled) == 1)
  {
    if (Decrement(_state, DS.humCountdownTicks) != 0)
    {
      return;
    }
    _state.Set(DS.humCountdownTicks, HUM_TICKS);
    ToggleSpeakerData(_state, _hardware);
    return;
  }
  if (_state.Get(DS.continuousNoise) == 1)
  {
    EmitNoiseSample(_state, _hardware);
  }
}

// TimerInterrupt's chain to the BIOS (0x0255): a far jump to the saved int 8 vector with the interrupt's frame in
// place. Here the BIOS handler is called as int 8 instead, with the vector pointing at it for the call, so that its
// IRET comes back to this routine and this routine's IRET takes the frame.
void ChainToBiosTimer(Guest& _guest)
{
  const std::uint16_t offset = _guest.FarWord(0, TIMER_VECTOR_OFFSET);
  const std::uint16_t segment = _guest.FarWord(0, TIMER_VECTOR_SEGMENT);
  _guest.SetFarWord(0, TIMER_VECTOR_OFFSET, _guest.CodeWord(SAVED_TIMER_OFFSET));
  _guest.SetFarWord(0, TIMER_VECTOR_SEGMENT, _guest.CodeWord(SAVED_TIMER_SEGMENT));
  _guest.Interrupt(TIMER_VECTOR);
  _guest.SetFarWord(0, TIMER_VECTOR_OFFSET, offset);
  _guest.SetFarWord(0, TIMER_VECTOR_SEGMENT, segment);
}

} // namespace

void InstallTimerInterrupt(GameState& _state, Hardware& _hardware)
{
  _hardware.SetTickDivisor(PIT_CHANNEL0_SQUARE_WAVE, TICK_DIVISOR);
  const std::uint8_t speaker = _hardware.SystemControl();
  _state.Set(DS.savedSpeakerPort, speaker);
  const auto image = static_cast<std::uint8_t>(speaker & ~SPEAKER_BITS);
  _state.Set(DS.speakerPortImage, image);
  _hardware.SetSystemControl(image);
  _state.SetCodeWord(SAVED_TIMER_OFFSET, _state.FarWord(VECTOR_TABLE_SEGMENT, TIMER_VECTOR_OFFSET));
  _state.SetCodeWord(SAVED_TIMER_SEGMENT, _state.FarWord(VECTOR_TABLE_SEGMENT, TIMER_VECTOR_SEGMENT));
  _state.SetFarWord(VECTOR_TABLE_SEGMENT, TIMER_VECTOR_OFFSET, TIMER_INTERRUPT);
  _state.SetFarWord(VECTOR_TABLE_SEGMENT, TIMER_VECTOR_SEGMENT, _state.CodeSegment());
  _hardware.EnableInterrupts();
}

void RestoreTimerInterrupt(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = 0;
  _guest.SetFlag(Machine::FLAG_INTERRUPT, false);
  _guest.SetFarWord(regs.es, TIMER_VECTOR_SEGMENT, _guest.CodeWord(SAVED_TIMER_SEGMENT));
  regs.ax = _guest.CodeWord(SAVED_TIMER_OFFSET);
  _guest.SetFarWord(regs.es, TIMER_VECTOR_OFFSET, regs.ax);
  regs.ax = WithLow(regs.ax, _guest.Get(DS.savedSpeakerPort));
  _guest.Devices().SetSystemControl(Low(regs.ax));
  regs.bx = 0;
  regs.ax = WithLow(regs.ax, PIT_CHANNEL0_SQUARE_WAVE);
  if (_guest.Get(DS.amstradPresent) == 1)
  {
    _guest.Call(IS_MOUSE_DRIVER_INSTALLED);
    regs.ax = WithLow(regs.ax, PIT_CHANNEL0_RATE);
    if ((regs.flags & Machine::FLAG_ZERO) == 0)
    {
      regs.bx = AMSTRAD_MOUSE_DIVISOR;
    }
  }
  _guest.Devices().SetTickDivisor(Low(regs.ax), regs.bx);
  // The divisor's bytes go out through AL: AX ends as BH twice.
  regs.ax = WithLow(regs.bx, High(regs.bx));
  _guest.Devices().EnableInterrupts();

  // The BIOS clock past a day is set back by one: SUB DX / SBB CX, kept in the registers either way.
  regs.ax = WithHigh(regs.ax, 0);
  _guest.Interrupt(TIME_OF_DAY_VECTOR);
  const std::uint32_t ticks = (std::uint32_t{regs.cx} << 16) | regs.dx;
  const std::uint32_t lessADay = ticks - TICKS_PER_DAY;
  regs.dx = static_cast<std::uint16_t>(lessADay);
  regs.cx = static_cast<std::uint16_t>(lessADay >> 16);
  if (ticks >= TICKS_PER_DAY)
  {
    regs.ax = WithHigh(regs.ax, 1);
    _guest.Interrupt(TIME_OF_DAY_VECTOR);
  }
}

void TimerInterrupt(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t ax = regs.ax;
  const std::uint16_t ds = regs.ds;
  const std::uint16_t es = regs.es;
  regs.es = Guest::VIDEO_SEGMENT;
  regs.ax = _guest.DataSegment();
  regs.ds = _guest.DataSegment();
  TimerTick(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  // What TimerTick leaves in AX goes no further: AL is loaded here and AX popped below.
  regs.ax = WithLow(regs.ax, END_OF_INTERRUPT);
  _guest.Devices().EndOfInterrupt();
  bool chain = false;
  if (_guest.Get(DS.amstradPresent) == 1)
  {
    // The Amstrad's BIOS clock still needs its 18.2 Hz: every 18th tick, or every 55th with the mouse driver's rate.
    _guest.Call(IS_MOUSE_DRIVER_INSTALLED);
    const bool mouse = (regs.flags & Machine::FLAG_ZERO) == 0;
    if (Decrement(_guest.State(), DS.biosTimerChainCountdown) == 0)
    {
      _guest.Set(DS.biosTimerChainCountdown, mouse ? BIOS_CHAIN_TICKS_WITH_MOUSE : BIOS_CHAIN_TICKS);
      chain = true;
    }
  }
  regs.es = es;
  regs.ds = ds;
  regs.ax = ax;
  if (chain)
  {
    ChainToBiosTimer(_guest);
  }
}

void TimerTick(GameState& _state, Hardware& _hardware, bool _backward)
{
  _state.Set(DS.millisecondCounter, static_cast<std::uint16_t>(_state.Get(DS.millisecondCounter) + 1));
  _state.Set(DS.timerTicks, static_cast<std::uint16_t>(_state.Get(DS.timerTicks) + 1));
  _state.Set(DS.msSinceFrame, static_cast<std::uint8_t>(_state.Get(DS.msSinceFrame) + 1));
  if (_state.Get(DS.protectionQuestion) != NO_QUESTION)
  {
    CheckProtectionAnswer(_state, _backward);
    return;
  }
  if (_state.Get(DS.soundEnabled) == 0 || _state.Get(DS.gamePaused) == 1)
  {
    return;
  }
  if (_state.Get(DS.musicPlaying) == 1)
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
  const std::uint16_t ticks = _state.Get(DS.timerTicks);
  while (ticks == _state.Get(DS.timerTicks))
  {
    _hardware.LoopTurn(TIMER_TICK_COMPARE, {ticks});
  }
}

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DX;

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};

} // namespace

void InstallTimerInterruptEntry(Guest& _guest)
{
  InstallTimerInterrupt(_guest.State(), _guest.Devices());
  _guest.Regs().es = VECTOR_TABLE_SEGMENT;
  _guest.Regs().ax = _guest.CodeSegment();
  _guest.Clobber(CLOBBERS_AX);
}

void WaitForTimerTickEntry(Guest& _guest)
{
  WaitForTimerTick(_guest.State(), _guest.Devices());
  _guest.Clobber(PRESERVES_ALL);
}

void TimerTickEntry(Guest& _guest)
{
  TimerTick(_guest.State(), _guest.Devices(), _guest.Flag(Machine::FLAG_DIRECTION));
  _guest.Clobber(CLOBBERS_AX);
}

namespace
{

constexpr std::array ENTRIES = {
  NativeEntry{0x00C6, "InstallTimerInterrupt", &InstallTimerInterruptEntry, CLOBBERS_AX},
  NativeEntry{0x016B, "RestoreTimerInterrupt", &RestoreTimerInterrupt, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{TIMER_INTERRUPT, "TimerInterrupt", &TimerInterrupt, PRESERVES_ALL, Machine::NativeReturn::Interrupt},
  NativeEntry{0x7150, "TimerTick", &TimerTickEntry, CLOBBERS_AX},
  // WaitForTimerTick waits for the next tick as a rule.
  NativeEntry{0x7772, "WaitForTimerTick", &WaitForTimerTickEntry, PRESERVES_ALL, Machine::NativeReturn::Near, 0,
              Machine::NativeWait::Always},
};

} // namespace

std::span<const NativeEntry> TimerEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
