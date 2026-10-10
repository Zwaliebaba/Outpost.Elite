#include "pch.h"

#include "Timer.h"

#include "Arithmetic.h"
#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::uint16_t IS_MOUSE_DRIVER_INSTALLED = 0x02D4;
constexpr std::uint16_t EMIT_NOISE_SAMPLE = 0x7A93;
constexpr std::uint16_t TOGGLE_SPEAKER = 0x7AB8;
constexpr std::uint16_t TIMER_INTERRUPT = 0x0215;

// The BIOS's int 8 vector, which InstallTimerInterrupt keeps in the code segment (savedTimerVector).
constexpr std::uint16_t SAVED_TIMER_OFFSET = 0x025A;
constexpr std::uint16_t SAVED_TIMER_SEGMENT = 0x025C;
constexpr std::uint16_t TIMER_VECTOR_OFFSET = 0x0020; // 0000:0020, int 8
constexpr std::uint16_t TIMER_VECTOR_SEGMENT = 0x0022;
constexpr std::uint8_t TIMER_VECTOR = 0x08;
constexpr std::uint8_t TIME_OF_DAY_VECTOR = 0x1A;

constexpr std::uint16_t PIC_COMMAND_PORT = 0x20;
constexpr std::uint8_t END_OF_INTERRUPT = 0x20;
constexpr std::uint16_t PIT_CHANNEL0_PORT = 0x40;
constexpr std::uint16_t PIT_CHANNEL2_PORT = 0x42;
constexpr std::uint16_t PIT_COMMAND_PORT = 0x43;
constexpr std::uint16_t SPEAKER_PORT = 0x61;
constexpr std::uint8_t PIT_CHANNEL0_SQUARE_WAVE = 0x36; // channel 0, both bytes, mode 3
constexpr std::uint8_t PIT_CHANNEL0_RATE = 0x34;        // channel 0, both bytes, mode 2: the Amstrad's mouse
constexpr std::uint8_t PIT_CHANNEL2_SQUARE_WAVE = 0xB6; // channel 2, both bytes, mode 3
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

// The byte at _field less one, stored back: DEC BYTE PTR. Returns what it leaves.
std::uint8_t Decrement(Guest& _guest, DataField<std::uint8_t> _field)
{
  const auto value = static_cast<std::uint8_t>(_guest.Get(_field) - 1);
  _guest.Set(_field, value);
  return value;
}

// AL and the speaker's data bit to port 61h, through speakerPortImage.
void OutSpeaker(Guest& _guest, std::uint8_t _image)
{
  _guest.Set(DS.speakerPortImage, _image);
  _guest.Regs().ax = WithLow(_guest.Regs().ax, _image);
  _guest.Out8(SPEAKER_PORT, _image);
}

// A beep's square wave: bit 1 of _bit replaces the speaker's data bit.
void SetSpeakerData(Guest& _guest, std::uint8_t _bit)
{
  OutSpeaker(_guest, static_cast<std::uint8_t>((_guest.Get(DS.speakerPortImage) & ~SPEAKER_DATA) | _bit));
}

void ToggleSpeakerData(Guest& _guest)
{
  OutSpeaker(_guest, static_cast<std::uint8_t>(_guest.Get(DS.speakerPortImage) ^ SPEAKER_DATA));
}

// CheckProtectionAnswer (0x739A): decodes the expected answer to the pending question into DS:A5B7 and compares the
// typed one with it.
void CheckProtectionAnswer(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint8_t typed = _guest.Get(DS.data25CA);
  if (typed == 0)
  {
    return;
  }
  const std::uint16_t descriptor = _guest.Get(DS.protectionDescriptor);
  std::uint16_t answer = _guest.Word(static_cast<std::uint16_t>(descriptor + 0x0A));
  const std::uint8_t question = _guest.Get(DS.protectionQuestion);
  // The answers are length-prefixed, one after another: skip to the question's.
  for (auto left = static_cast<std::uint8_t>(question - 1); left != 0; --left)
  {
    answer = static_cast<std::uint16_t>(answer + static_cast<std::uint8_t>(_guest.Byte(answer) + 1));
  }
  regs.ax = static_cast<std::uint16_t>(question << 8);
  const std::uint8_t length = _guest.Byte(answer);
  if (length != typed)
  {
    return;
  }
  ++answer;
  std::uint16_t decoded = DECODED_ANSWER;
  _guest.SetByte(decoded++, length);
  std::uint8_t key = 1;
  for (std::uint8_t left = length; left != 0; --left)
  {
    const auto plain = static_cast<std::uint8_t>(_guest.Byte(answer++) ^ question ^ key ^ ANSWER_KEY);
    regs.ax = WithLow(regs.ax, plain);
    ++key;
    _guest.SetByte(decoded++, plain);
  }
  // REPE CMPSB of the decoded answer (DS:SI) with the typed one (ES:DI, ES=DS), in DF's direction.
  const bool backward = (regs.flags & Machine::FLAG_DIRECTION) != 0;
  std::uint16_t expected = DECODED_ANSWER + 1;
  std::uint16_t given = static_cast<std::uint16_t>(DS.protectionInput.offset + 2);
  bool equal = false;
  for (std::uint8_t left = _guest.Byte(DECODED_ANSWER); left != 0; --left)
  {
    equal = _guest.Byte(expected) == _guest.Byte(given);
    expected = static_cast<std::uint16_t>(backward ? expected - 1 : expected + 1);
    given = static_cast<std::uint16_t>(backward ? given - 1 : given + 1);
    if (!equal)
    {
      break;
    }
  }
  if (equal)
  {
    _guest.Set(DS.protectionAnswerCorrect, 1);
  }
}

// TimerTickMusic's note change (0x7372): the PIT's channel 2 to _period. BX is the original's, put back.
void PlayPeriod(Guest& _guest, std::uint16_t _period)
{
  Machine::Registers& regs = _guest.Regs();
  _guest.Out8(PIT_COMMAND_PORT, PIT_CHANNEL2_SQUARE_WAVE);
  _guest.Out8(PIT_CHANNEL2_PORT, Low(_period));
  _guest.Out8(PIT_CHANNEL2_PORT, High(_period));
  regs.ax = static_cast<std::uint16_t>((_period & 0xFF00) | High(_period));
}

// TimerTickMusic (0x72FF): titleTune's notes, each a byte (negative: no gap after it, 'F' a rest, 'G' the end) and a
// length in beats.
void TickMusic(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  const std::uint16_t ticksLeft = _guest.Get(DS.noteTicksLeft);
  if (ticksLeft != 0)
  {
    _guest.Set(DS.noteTicksLeft, static_cast<std::uint16_t>(ticksLeft - 1));
    if (ticksLeft != 1)
    {
      return;
    }
    // The note is over: its gap, if it has one, as a rest.
    regs.ax = _guest.Get(DS.noteGapTicks);
    if (regs.ax == 0)
    {
      return;
    }
    _guest.Set(DS.noteGapTicks, 0);
    _guest.Set(DS.noteTicksLeft, regs.ax);
    PlayPeriod(_guest, REST_PERIOD);
    return;
  }

  std::uint16_t pointer = _guest.Get(DS.musicPointer);
  regs.ax = _guest.Word(pointer);
  const std::uint8_t note = Low(regs.ax);
  const std::uint8_t beats = High(regs.ax);
  const auto ticks = static_cast<std::uint16_t>(NOTE_TICKS_PER_BEAT * beats);
  if (note == TUNE_RESTART)
  {
    _guest.Set(DS.musicPointer, DS.titleTune.offset);
  }
  else
  {
    pointer = static_cast<std::uint16_t>(pointer + 2);
    _guest.Set(DS.musicPointer, pointer);
    if (note != TUNE_REST)
    {
      std::uint8_t pitch = note;
      std::uint16_t sounding = ticks;
      if ((note & 0x80) != 0)
      {
        _guest.Set(DS.noteGapTicks, 0);
        pitch = static_cast<std::uint8_t>(0u - note);
      }
      else
      {
        // An eighth of the note is a gap.
        const auto gap = static_cast<std::uint16_t>(ticks >> 3);
        _guest.Set(DS.noteGapTicks, gap);
        sounding = static_cast<std::uint16_t>(ticks - gap);
      }
      _guest.Set(DS.noteTicksLeft, sounding);
      const auto index = static_cast<std::uint8_t>(pitch - NOTE_BASE);
      PlayPeriod(_guest, _guest.Word(DS.notePeriods.At(index)));
      return;
    }
  }
  _guest.Set(DS.noteGapTicks, 0);
  _guest.Set(DS.noteTicksLeft, ticks);
  PlayPeriod(_guest, REST_PERIOD);
}

// The noise and tone sweeps (0x7215, 0x7289): every sweepPeriodTicks ticks one sample, by _sample; every
// sweepStepLength samples a longer period and a shorter step, until the period reaches 7.
void TickSweep(Guest& _guest, std::uint16_t _sample, DataField<std::uint8_t> _active)
{
  Machine::Registers& regs = _guest.Regs();
  if (Decrement(_guest, DS.sweepTickCounter) != 0)
  {
    return;
  }
  regs.ax = WithLow(regs.ax, _guest.Get(DS.sweepPeriodTicks));
  _guest.Set(DS.sweepTickCounter, Low(regs.ax));
  _guest.Call(_sample);
  if (Decrement(_guest, DS.sweepStepCounter) != 0)
  {
    return;
  }
  _guest.Set(DS.sweepStepCounter, _guest.Get(DS.sweepStepLength));
  const auto period = static_cast<std::uint8_t>(_guest.Get(DS.sweepPeriodTicks) + 1);
  _guest.Set(DS.sweepPeriodTicks, period);
  regs.ax = WithLow(regs.ax, _guest.Get(DS.sweepStepShrink));
  _guest.Set(DS.sweepStepLength, static_cast<std::uint8_t>(_guest.Get(DS.sweepStepLength) - Low(regs.ax)));
  if (period == SWEEP_LAST_PERIOD)
  {
    _guest.Set(_active, 0);
  }
}

// The siren (0x72C3): every other tick, a square wave of sirenCountdown's bit 0 or, in stage 1, of every tick.
void TickSiren(Guest& _guest)
{
  const auto half = static_cast<std::uint8_t>(_guest.Get(DS.sirenHalfTick) ^ 1);
  _guest.Set(DS.sirenHalfTick, half);
  if (half != 0)
  {
    return;
  }
  const std::uint8_t countdown = Decrement(_guest, DS.sirenCountdown);
  if (countdown == 0)
  {
    const std::uint8_t stage = Decrement(_guest, DS.sirenStage);
    _guest.Set(DS.sirenCountdown, SIREN_TICKS);
    if (stage == 0)
    {
      _guest.Set(DS.sirenStage, SIREN_FIRST_STAGE);
      return;
    }
    _guest.Set(DS.sirenCountdown, static_cast<std::uint8_t>(SIREN_TICKS << 1));
    return;
  }
  if (_guest.Get(DS.sirenStage) == 1 || (countdown & 1) == 0)
  {
    ToggleSpeakerData(_guest);
  }
}

// The two-tone (0x725E): twoToneTicks of a square wave on every tick in stage 1, every other tick otherwise.
void TickTwoTone(Guest& _guest)
{
  const auto ticks = static_cast<std::uint16_t>(_guest.Get(DS.twoToneTicks) - 1);
  _guest.Set(DS.twoToneTicks, ticks);
  if (ticks == 0)
  {
    Decrement(_guest, DS.twoToneStage);
    _guest.Set(DS.twoToneTicks, TWO_TONE_TICKS);
    return;
  }
  if (_guest.Get(DS.twoToneStage) == 1 || (ticks & 1) == 0)
  {
    ToggleSpeakerData(_guest);
  }
}

// TimerTickSoundEffects (0x717F): the beeps, then the one effect of highest priority.
void TickSoundEffects(Guest& _guest)
{
  if (_guest.Get(DS.beepTicks) != 0)
  {
    SetSpeakerData(_guest, static_cast<std::uint8_t>((Decrement(_guest, DS.beepTicks) << 1) & SPEAKER_DATA));
  }
  if (_guest.Get(DS.lowBeepTicks) != 0)
  {
    SetSpeakerData(_guest, static_cast<std::uint8_t>(Decrement(_guest, DS.lowBeepTicks) & SPEAKER_DATA));
  }
  if (_guest.Get(DS.sirenEnabled) == 1)
  {
    TickSiren(_guest);
    return;
  }
  if (_guest.Get(DS.noiseSweepActive) == 1)
  {
    TickSweep(_guest, EMIT_NOISE_SAMPLE, DS.noiseSweepActive);
    return;
  }
  if (_guest.Get(DS.noiseBurstTicks) != 0)
  {
    Decrement(_guest, DS.noiseBurstTicks);
    _guest.Call(EMIT_NOISE_SAMPLE);
    return;
  }
  if (_guest.Get(DS.toneSweepActive) == 1)
  {
    TickSweep(_guest, TOGGLE_SPEAKER, DS.toneSweepActive);
    return;
  }
  if (_guest.Get(DS.twoToneStage) != 0)
  {
    TickTwoTone(_guest);
    return;
  }
  if (_guest.Get(DS.slowNoiseCount) != 0)
  {
    if (Decrement(_guest, DS.slowNoiseDivider) != 0)
    {
      return;
    }
    _guest.Set(DS.slowNoiseDivider, SLOW_NOISE_TICKS);
    Decrement(_guest, DS.slowNoiseCount);
    _guest.Call(EMIT_NOISE_SAMPLE);
    return;
  }
  if (_guest.Get(DS.humEnabled) == 1)
  {
    if (Decrement(_guest, DS.humCountdownTicks) != 0)
    {
      return;
    }
    _guest.Set(DS.humCountdownTicks, HUM_TICKS);
    ToggleSpeakerData(_guest);
    return;
  }
  if (_guest.Get(DS.continuousNoise) == 1)
  {
    _guest.Call(EMIT_NOISE_SAMPLE);
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

void InstallTimerInterrupt(Guest& _guest)
{
  Machine::Registers& regs = _guest.Regs();
  regs.es = 0;
  _guest.Out8(PIT_COMMAND_PORT, PIT_CHANNEL0_SQUARE_WAVE);
  _guest.Out8(PIT_CHANNEL0_PORT, Low(TICK_DIVISOR));
  _guest.Out8(PIT_CHANNEL0_PORT, High(TICK_DIVISOR));
  const std::uint8_t speaker = _guest.In8(SPEAKER_PORT);
  _guest.Set(DS.savedSpeakerPort, speaker);
  const auto image = static_cast<std::uint8_t>(speaker & ~SPEAKER_BITS);
  _guest.Set(DS.speakerPortImage, image);
  _guest.Out8(SPEAKER_PORT, image);
  _guest.SetCodeWord(SAVED_TIMER_OFFSET, _guest.FarWord(regs.es, TIMER_VECTOR_OFFSET));
  _guest.SetCodeWord(SAVED_TIMER_SEGMENT, _guest.FarWord(regs.es, TIMER_VECTOR_SEGMENT));
  _guest.SetFarWord(regs.es, TIMER_VECTOR_OFFSET, TIMER_INTERRUPT);
  _guest.SetFarWord(regs.es, TIMER_VECTOR_SEGMENT, _guest.CodeSegment());
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);
  regs.ax = _guest.CodeSegment();
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
  _guest.Out8(SPEAKER_PORT, Low(regs.ax));
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
  _guest.Out8(PIT_COMMAND_PORT, Low(regs.ax));
  regs.ax = regs.bx;
  _guest.Out8(PIT_CHANNEL0_PORT, Low(regs.ax));
  regs.ax = WithLow(regs.ax, High(regs.ax));
  _guest.Out8(PIT_CHANNEL0_PORT, Low(regs.ax));
  _guest.SetFlag(Machine::FLAG_INTERRUPT, true);

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
  TimerTick(_guest);
  regs.ax = WithLow(regs.ax, END_OF_INTERRUPT);
  _guest.Out8(PIC_COMMAND_PORT, END_OF_INTERRUPT);
  bool chain = false;
  if (_guest.Get(DS.amstradPresent) == 1)
  {
    // The Amstrad's BIOS clock still needs its 18.2 Hz: every 18th tick, or every 55th with the mouse driver's rate.
    _guest.Call(IS_MOUSE_DRIVER_INSTALLED);
    const bool mouse = (regs.flags & Machine::FLAG_ZERO) == 0;
    if (Decrement(_guest, DS.biosTimerChainCountdown) == 0)
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

void TimerTick(Guest& _guest)
{
  _guest.Set(DS.millisecondCounter, static_cast<std::uint16_t>(_guest.Get(DS.millisecondCounter) + 1));
  _guest.Set(DS.timerTicks, static_cast<std::uint16_t>(_guest.Get(DS.timerTicks) + 1));
  _guest.Set(DS.msSinceFrame, static_cast<std::uint8_t>(_guest.Get(DS.msSinceFrame) + 1));
  if (_guest.Get(DS.protectionQuestion) != NO_QUESTION)
  {
    CheckProtectionAnswer(_guest);
    return;
  }
  if (_guest.Get(DS.soundEnabled) == 0 || _guest.Get(DS.gamePaused) == 1)
  {
    return;
  }
  if (_guest.Get(DS.musicPlaying) == 1)
  {
    TickMusic(_guest);
    return;
  }
  TickSoundEffects(_guest);
}

namespace
{

using Machine::REGISTER_AX;
using Machine::REGISTER_BX;
using Machine::REGISTER_CX;
using Machine::REGISTER_DX;

constexpr Machine::NativeContract CLOBBERS_AX{REGISTER_AX, 0};
constexpr Machine::NativeContract CLOBBERS_AX_BX_CX_DX{REGISTER_AX | REGISTER_BX | REGISTER_CX | REGISTER_DX, 0};

constexpr std::array ENTRIES = {
  NativeEntry{0x00C6, "InstallTimerInterrupt", &InstallTimerInterrupt, CLOBBERS_AX},
  NativeEntry{0x016B, "RestoreTimerInterrupt", &RestoreTimerInterrupt, CLOBBERS_AX_BX_CX_DX},
  NativeEntry{TIMER_INTERRUPT, "TimerInterrupt", &TimerInterrupt, PRESERVES_ALL, Machine::NativeReturn::Interrupt},
  NativeEntry{0x7150, "TimerTick", &TimerTick, CLOBBERS_AX},
};

} // namespace

std::span<const NativeEntry> TimerEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
