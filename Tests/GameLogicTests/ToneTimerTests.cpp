#include "pch.h"

#include "Speaker.h"
#include "Timing.h"
#include "ToneTimer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// One timer tick, so that tick arithmetic is done in Cycles.
constexpr Elite::Cycles TICK = Elite::CYCLES_PER_PIT_TICK;

// The tone timer on its own clock, with the speaker it drives.
class ToneRig
{
public:
  ToneRig()
    : m_speaker(m_clock),
      m_tone(m_clock, m_speaker)
  {
  }

  [[nodiscard]] Elite::ToneTimer& Tone() noexcept
  {
    return m_tone;
  }

  [[nodiscard]] Elite::Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  void SetClock(Elite::Cycles _cycle) noexcept
  {
    m_clock = _cycle;
  }

private:
  Elite::Cycles m_clock = 0;
  Elite::Speaker m_speaker;
  Elite::ToneTimer m_tone;
};

} // namespace

// The timer's channel 2 as the game programs it (ToneTimer, ADR-041): the square wave the speaker hears.
TEST_CLASS(ToneTimerTests)
{
public:
  // Channel 2 is gated by the system control port's bit 0: low, its output is high; rising, it reloads on the next tick. Its
  // every change reaches the speaker's log.
  TEST_METHOD(GateRestartsTheWaveAndTheSpeakerHearsIt)
  {
    ToneRig rig;
    Elite::ToneTimer& tone = rig.Tone();
    Assert::IsFalse(tone.State().gate, L"gate low at power-on");
    Assert::IsTrue(tone.OutputAt(0));

    tone.Program(0x32); // the game's music: channel 2, both bytes, mode 3
    rig.SetClock(400);
    tone.SetGate(true); // reloaded on tick 101: high 101-125, low 126-150
    Assert::IsTrue(tone.OutputAt(125 * TICK + 3));
    Assert::IsFalse(tone.OutputAt(126 * TICK));
    Assert::IsFalse(tone.OutputAt(150 * TICK + 3));
    Assert::IsTrue(tone.OutputAt(151 * TICK));

    rig.SetClock(130 * TICK);
    tone.SetGate(false);
    Assert::IsTrue(tone.OutputAt(130 * TICK), L"a low gate forces the output high");

    const Elite::Speaker::Event& last = rig.Sound().Events().back();
    Assert::AreEqual(130 * TICK, last.at);
    Assert::IsFalse(last.channel2.gate);
    Assert::AreEqual(0x32u, last.channel2.reload);
  }
};

} // namespace GameLogicTests
