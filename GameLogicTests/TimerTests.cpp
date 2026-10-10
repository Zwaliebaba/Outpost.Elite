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
using Bytes = std::initializer_list<std::uint8_t>;
using Words = std::initializer_list<std::uint16_t>;

constexpr std::uint16_t TIMER_INTERRUPT = 0x0215;
constexpr std::uint16_t TIMER_TICK = 0x7150;
constexpr std::uint16_t MOUSE_VECTOR_OFFSET = 0x00CC; // 0000:00CC, int 33h
constexpr std::uint16_t IRET_OFFSET = 0x0214;         // an IRET in the code segment: no driver
constexpr std::uint16_t TUNE = 0x20F5;                // commanderFileList, free for a made-up tune
constexpr std::uint16_t TYPED_ANSWER = 0x25CB;        // after protectionInput's capacity and count

void Poke(ComparisonRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Host().Ram().Write8(Elite::DataSegment(_rig.Program()), _field.offset, _value);
}

void Poke(ComparisonRig& _rig, Elite::DataField<std::uint16_t> _field, std::uint16_t _value)
{
  _rig.Host().Ram().Write16(Elite::DataSegment(_rig.Program()), _field.offset, _value);
}

[[nodiscard]] std::uint8_t Peek(ComparisonRig& _rig, std::uint16_t _offset)
{
  return _rig.Host().Ram().Read8(Elite::DataSegment(_rig.Program()), _offset);
}

// Every effect off, with sound on and nothing paused: TimerTick's quiet path.
void Quiet(ComparisonRig& _rig)
{
  Poke(_rig, DS.protectionQuestion, 0xFF);
  Poke(_rig, DS.soundEnabled, 1);
  Poke(_rig, DS.gamePaused, 0);
  Poke(_rig, DS.musicPlaying, 0);
  for (const Elite::DataField<std::uint8_t> effect :
       {DS.beepTicks, DS.lowBeepTicks, DS.sirenEnabled, DS.noiseSweepActive, DS.noiseBurstTicks, DS.toneSweepActive, DS.twoToneStage,
        DS.slowNoiseCount, DS.humEnabled, DS.continuousNoise})
    Poke(_rig, effect, 0);
}

// Calls an interrupt handler as the CPU enters one: the flags, with interrupts off, then CS and the return
// offset on the stack.
void CallHandler(ComparisonRig& _rig, std::uint16_t _entry)
{
  Machine::Pc& pc = _rig.Host();
  Machine::Registers& regs = pc.Processor().Regs();
  const Machine::Registers saved = regs;
  regs.flags = static_cast<std::uint16_t>(regs.flags & ~Machine::FLAG_INTERRUPT);
  regs.cs = _rig.Program().loadSegment;
  for (const std::uint16_t value : {regs.flags, regs.cs})
  {
    regs.sp = static_cast<std::uint16_t>(regs.sp - 2);
    pc.Ram().Write16(regs.ss, regs.sp, value);
  }
  pc.CallNear(_entry);
  regs = saved;
}

} // namespace

// Constructed inputs for the timer interrupt (plan §6.3): the sounds and the Amstrad's BIOS chain that no replay
// makes, and the protection's answer check, which the D5 byte leaves unreached.
TEST_CLASS(TimerTests)
{
public:
  TEST_METHOD(TimerTickAgreesOnEverySound)
  {
    ComparisonRig rig("TimerTick");
    std::uint64_t calls = 0;
    const auto tick = [&]()
    {
      rig.Call(TIMER_TICK, {});
      ++calls;
    };
    Quiet(rig);
    tick();
    Poke(rig, DS.soundEnabled, 0);
    tick();
    Poke(rig, DS.soundEnabled, 1);
    Poke(rig, DS.gamePaused, 1);
    tick();
    Poke(rig, DS.gamePaused, 0);

    // Both beeps at once.
    Poke(rig, DS.beepTicks, 3);
    Poke(rig, DS.lowBeepTicks, 4);
    tick();
    tick();
    Quiet(rig);

    // The siren: every other tick, its countdown, and its stages.
    Poke(rig, DS.sirenEnabled, 1);
    for (const std::uint8_t stage : Bytes{1, 2})
    {
      for (const std::uint8_t countdown : Bytes{5, 4, 1})
      {
        for (const std::uint8_t half : Bytes{0, 1})
        {
          Poke(rig, DS.sirenStage, stage);
          Poke(rig, DS.sirenCountdown, countdown);
          Poke(rig, DS.sirenHalfTick, half);
          tick();
        }
      }
    }
    Quiet(rig);

    // The noise and tone sweeps: a tick between samples, a sample within a step, and the last step.
    for (const Elite::DataField<std::uint8_t> sweep : {DS.noiseSweepActive, DS.toneSweepActive})
    {
      Poke(rig, sweep, 1);
      Poke(rig, DS.sweepTickCounter, 2);
      tick();
      Poke(rig, DS.sweepPeriodTicks, 6);
      Poke(rig, DS.sweepStepCounter, 2);
      tick();
      Poke(rig, DS.sweepTickCounter, 1);
      Poke(rig, DS.sweepStepLength, 10);
      Poke(rig, DS.sweepStepShrink, 3);
      tick();
      Quiet(rig);
    }

    Poke(rig, DS.noiseBurstTicks, 2);
    tick();
    Quiet(rig);

    // The two-tone: a stage's end, stage 2 on even ticks only, stage 1 on every tick.
    for (const std::uint8_t stage : Bytes{2, 1})
    {
      for (const std::uint16_t ticks : Words{1, 5, 4})
      {
        Poke(rig, DS.twoToneStage, stage);
        Poke(rig, DS.twoToneTicks, ticks);
        tick();
      }
    }
    Quiet(rig);

    Poke(rig, DS.slowNoiseCount, 2);
    for (const std::uint8_t divider : Bytes{2, 1})
    {
      Poke(rig, DS.slowNoiseDivider, divider);
      tick();
    }
    Quiet(rig);

    Poke(rig, DS.humEnabled, 1);
    for (const std::uint8_t countdown : Bytes{2, 1})
    {
      Poke(rig, DS.humCountdownTicks, countdown);
      tick();
    }
    Quiet(rig);

    Poke(rig, DS.continuousNoise, 1);
    tick();
    Quiet(rig);

    // A made-up tune: a note with its gap, one without, a rest, the end; then the ticks within a note.
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const Words tune = {0x0230, 0x01D0, 0x0346, 0x0247};
    std::uint16_t offset = TUNE;
    for (const std::uint16_t note : tune)
    {
      ram.Write16(data, offset, note);
      offset = static_cast<std::uint16_t>(offset + 2);
    }
    Poke(rig, DS.musicPlaying, 1);
    Poke(rig, DS.musicPointer, TUNE);
    for (std::size_t note = 0; note < tune.size(); ++note)
    {
      Poke(rig, DS.noteTicksLeft, 0);
      tick();
    }
    Poke(rig, DS.noteTicksLeft, 2);
    tick();
    for (const std::uint16_t gap : Words{0, 9})
    {
      Poke(rig, DS.noteTicksLeft, 1);
      Poke(rig, DS.noteGapTicks, gap);
      tick();
    }
    Quiet(rig);
    rig.AssertAllAgreed(TIMER_TICK, calls);
  }

  // The answer to question 3, decoded as CheckProtectionAnswer decodes it: nothing typed, the wrong length, the
  // answer, and the answer with its last letter wrong.
  TEST_METHOD(TimerTickAgreesOnTheProtectionAnswer)
  {
    ComparisonRig rig("ProtectionAnswer");
    Quiet(rig);
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::uint8_t question = 3;
    std::uint16_t answer = ram.Read16(data, static_cast<std::uint16_t>(ram.Read16(data, DS.protectionDescriptor.offset) + 0x0A));
    for (std::uint8_t skip = 1; skip < question; ++skip)
      answer = static_cast<std::uint16_t>(answer + Peek(rig, answer) + 1);
    const std::uint8_t length = Peek(rig, answer);
    for (std::uint8_t index = 0; index < length; ++index)
    {
      const auto plain =
        static_cast<std::uint8_t>(Peek(rig, static_cast<std::uint16_t>(answer + 1 + index)) ^ question ^ (index + 1) ^ 0x61);
      ram.Write8(data, static_cast<std::uint16_t>(TYPED_ANSWER + index), plain);
    }
    Poke(rig, DS.protectionQuestion, question);
    Poke(rig, DS.protectionAnswerCorrect, 0);
    for (const std::uint8_t typed : Bytes{0, static_cast<std::uint8_t>(length + 1), length})
    {
      Poke(rig, DS.data25CA, typed);
      rig.Call(TIMER_TICK, {});
    }
    Assert::IsTrue(Peek(rig, DS.protectionAnswerCorrect.offset) == 1, L"the right answer is accepted");
    Poke(rig, DS.protectionAnswerCorrect, 0);
    ram.Write8(data, static_cast<std::uint16_t>(TYPED_ANSWER + length - 1), '?');
    rig.Call(TIMER_TICK, {});
    Assert::IsTrue(Peek(rig, DS.protectionAnswerCorrect.offset) == 0, L"a wrong answer is not");
    rig.AssertAllAgreed(TIMER_TICK, 4);
  }

  // The Amstrad chains to the BIOS's int 8 every 18 ticks, or every 55 with a mouse driver.
  TEST_METHOD(TimerInterruptAgreesOnTheAmstradsBiosChain)
  {
    ComparisonRig rig("TimerInterrupt");
    Quiet(rig);
    Poke(rig, DS.amstradPresent, 1);
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t code = rig.Program().loadSegment;
    std::uint64_t calls = 0;
    // No driver (a null vector, then an IRET), and a driver (any other byte, here Start's first).
    for (const std::uint32_t vector : {std::uint32_t{0}, (std::uint32_t{code} << 16) | IRET_OFFSET, std::uint32_t{code} << 16})
    {
      ram.Write16(0, MOUSE_VECTOR_OFFSET, static_cast<std::uint16_t>(vector));
      ram.Write16(0, MOUSE_VECTOR_OFFSET + 2, static_cast<std::uint16_t>(vector >> 16));
      for (const std::uint8_t countdown : Bytes{2, 1})
      {
        Poke(rig, DS.biosTimerChainCountdown, countdown);
        CallHandler(rig, TIMER_INTERRUPT);
        ++calls;
      }
    }
    rig.AssertAllAgreed(TIMER_INTERRUPT, calls);
  }
};

} // namespace GameLogicTests
