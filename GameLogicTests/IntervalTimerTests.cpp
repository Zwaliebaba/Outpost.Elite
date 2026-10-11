#include "pch.h"

#include "InterruptController.h"
#include "IntervalTimer.h"
#include "Speaker.h"
#include "Timing.h"

#include <array>
#include <stdexcept>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The game's timer: channel 0, both bytes, mode 3, divisor 04A9h (InstallTimerInterrupt, 0x00C6).
constexpr std::uint8_t GAME_CONTROL_WORD = 0x36;
constexpr std::uint16_t GAME_DIVISOR = 0x04A9;
constexpr Elite::Cycles GAME_PERIOD_CYCLES = GAME_DIVISOR * Elite::CYCLES_PER_PIT_TICK; // 4772

// One timer tick, so that tick arithmetic is done in Cycles.
constexpr Elite::Cycles TICK = Elite::CYCLES_PER_PIT_TICK;

// A timer on its own clock, with the interrupt controller and the speaker it is wired to.
class TimerRig
{
public:
  TimerRig()
    : m_speaker(m_clock),
      m_timer(m_clock, m_controller, m_speaker)
  {
  }

  [[nodiscard]] Elite::IntervalTimer& Timer() noexcept
  {
    return m_timer;
  }

  [[nodiscard]] Elite::Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  void SetClock(Elite::Cycles _cycle) noexcept
  {
    m_clock = _cycle;
  }

  // Moves the clock to _cycle, as the platform does in a wait, and advances the timer. Returns whether IRQ 0 was raised, and
  // if so acknowledges it and ends it, as the game's handler does.
  [[nodiscard]] bool StepTo(Elite::Cycles _cycle)
  {
    m_clock = _cycle;
    m_timer.Advance();
    if ((m_controller.RequestRegister() & 1u) == 0)
    {
      return false;
    }
    (void)m_controller.Acknowledge();
    m_controller.EndOfInterrupt();
    return true;
  }

private:
  Elite::Cycles m_clock = 0;
  Elite::InterruptController m_controller;
  Elite::Speaker m_speaker;
  Elite::IntervalTimer m_timer;
};

} // namespace

// The 8253 as the game programs it (IntervalTimer, D33): the emulated PC's timer without its ports.
TEST_CLASS(IntervalTimerTests)
{
public:
  // As the BIOS leaves it: mode 3, divisor 65536, so the first IRQ 0 is 65536 ticks after power-on.
  TEST_METHOD(PowerOnChannel0RunsAtEighteenHertz)
  {
    TimerRig rig;
    const Elite::Cycles first = 65536 * Elite::CYCLES_PER_PIT_TICK;
    Assert::AreEqual(first, rig.Timer().NextInterruptAt(), L"the first IRQ 0 is due there");
    Assert::IsFalse(rig.StepTo(first - 1));
    Assert::IsTrue(rig.StepTo(first));
    Assert::IsFalse(rig.StepTo(2 * first - 1));
    Assert::IsTrue(rig.StepTo(2 * first));
  }

  // The game's divisor: one IRQ 0 every 1193 ticks, 4772 cycles, counted from the tick after the count is written.
  TEST_METHOD(GameDivisorRaisesIrq0Every4772Cycles)
  {
    TimerRig rig;
    rig.SetClock(1000);
    rig.Timer().Program(GAME_CONTROL_WORD, GAME_DIVISOR);

    std::vector<Elite::Cycles> raised;
    for (Elite::Cycles cycle = 1001; cycle <= 1000 + 20 * GAME_PERIOD_CYCLES + 10; ++cycle)
    {
      if (rig.StepTo(cycle))
      {
        raised.push_back(cycle);
      }
    }
    Assert::AreEqual(std::size_t{20}, raised.size());
    for (std::size_t index = 0; index < raised.size(); ++index)
    {
      // Loaded on tick 251 (cycle 1004), so the edges are on ticks 251 + 1193k.
      Assert::AreEqual(1004 + (index + 1) * GAME_PERIOD_CYCLES, raised[index]);
    }
  }

  // The clock can stop on any cycle, few of them a multiple of a tick. Advancing by such steps must neither lose nor gain
  // time: each IRQ 0 lands in the step that crosses its edge.
  TEST_METHOD(SubTickRemaindersAreCarriedExactly)
  {
    TimerRig rig;
    rig.SetClock(1000);
    rig.Timer().Program(GAME_CONTROL_WORD, GAME_DIVISOR);

    constexpr std::array<Elite::Cycles, 6> STEPS = {7, 13, 3, 23, 2, 9};
    Elite::Cycles cycle = 1000;
    std::size_t raised = 0;
    for (std::size_t step = 0; cycle < 2'000'000; ++step)
    {
      const Elite::Cycles previous = cycle;
      cycle += STEPS[step % STEPS.size()];
      if (rig.StepTo(cycle))
      {
        ++raised;
        const Elite::Cycles edge = 1004 + raised * GAME_PERIOD_CYCLES;
        Assert::IsTrue(previous < edge && edge <= cycle, L"IRQ 0 in the step that crosses its edge");
      }
    }
    Assert::AreEqual(static_cast<std::size_t>((cycle - 1004) / GAME_PERIOD_CYCLES), raised, L"no edge lost or gained");
  }

  // The game writes both bytes of every count it programs, in mode 3. Any other access or mode, and the 8254's read-back, it
  // never writes, and the timer refuses them rather than guess (D33).
  TEST_METHOD(OnlyBothBytesAreProgrammed)
  {
    TimerRig rig;
    Assert::ExpectException<std::invalid_argument>([&rig] { rig.Timer().Program(0x50, 10); }, L"channel 1, low byte only");
    Assert::ExpectException<std::invalid_argument>([&rig] { rig.Timer().Program(0x60, 1); }, L"channel 1, high byte only");
    Assert::ExpectException<std::invalid_argument>([&rig] { rig.Timer().Program(0x00, 0); }, L"a counter-latch command");
    Assert::ExpectException<std::invalid_argument>([&rig] { rig.Timer().Program(0xC2, 0); }, L"the 8254's read-back");
    Assert::ExpectException<std::invalid_argument>([&rig] { rig.Timer().Program(0x34, 0x5555); }, L"mode 2, the Amstrad's");
  }

  // A control word sets a mode 3 output high at once. When it was low, that is a rising edge on IRQ 0's line, and the
  // controller sees it.
  TEST_METHOD(ControlWordThatRaisesALowOutputIsAnEdge)
  {
    TimerRig rig;
    const Elite::Cycles lowHalf = 40'000 * Elite::CYCLES_PER_PIT_TICK; // phase 40000 of 65536: low
    Assert::IsFalse(rig.StepTo(lowHalf));
    Assert::IsFalse(rig.Timer().OutputAt(0, lowHalf));
    rig.Timer().Program(GAME_CONTROL_WORD, GAME_DIVISOR);
    Assert::IsTrue(rig.Timer().OutputAt(0, lowHalf));
    Assert::IsTrue(rig.StepTo(lowHalf));
  }

  // Channel 2 is gated by the system control port's bit 0: low, its output is high; rising, it reloads on the next tick. Its
  // every change reaches the speaker's log.
  TEST_METHOD(Channel2GateRestartsTheWaveAndTheSpeakerHearsIt)
  {
    TimerRig rig;
    Assert::IsFalse(rig.Timer().ChannelState(2).gate, L"gate low at power-on");
    Assert::IsTrue(rig.Timer().OutputAt(2, 0));

    rig.Timer().Program(0xB6, 0x32); // the game's music: channel 2, both bytes, mode 3
    rig.SetClock(400);
    rig.Timer().SetChannel2Gate(true); // reloaded on tick 101: high 101-125, low 126-150
    Assert::IsTrue(rig.Timer().OutputAt(2, 125 * TICK + 3));
    Assert::IsFalse(rig.Timer().OutputAt(2, 126 * TICK));
    Assert::IsFalse(rig.Timer().OutputAt(2, 150 * TICK + 3));
    Assert::IsTrue(rig.Timer().OutputAt(2, 151 * TICK));

    rig.SetClock(130 * TICK);
    rig.Timer().SetChannel2Gate(false);
    Assert::IsTrue(rig.Timer().OutputAt(2, 130 * TICK), L"a low gate forces the output high");

    const Elite::Speaker::Event& last = rig.Sound().Events().back();
    Assert::AreEqual(130 * TICK, last.at);
    Assert::IsFalse(last.channel2.gate);
    Assert::AreEqual(0x32u, last.channel2.reload);
  }
};

} // namespace GameLogicTests
