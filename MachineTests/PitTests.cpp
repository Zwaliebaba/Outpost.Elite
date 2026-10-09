#include "pch.h"

#include "Pic.h"
#include "Pit.h"
#include "Speaker.h"
#include "Timing.h"

#include <array>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint16_t CHANNEL_0 = 0x40;
constexpr std::uint16_t CHANNEL_1 = 0x41;
constexpr std::uint16_t CHANNEL_2 = 0x42;
constexpr std::uint16_t CONTROL = 0x43;

// The game's timer: channel 0, LSB then MSB, mode 3, divisor 0x04A9 (InstallTimerInterrupt, 0x00C6).
constexpr std::uint8_t GAME_CONTROL_WORD = 0x36;
constexpr std::uint32_t GAME_DIVISOR = 0x04A9;
constexpr Machine::Cycles GAME_PERIOD_CYCLES = GAME_DIVISOR * Machine::CYCLES_PER_PIT_TICK; // 4772

// One timer tick, so that tick arithmetic is done in Cycles.
constexpr Machine::Cycles TICK = Machine::CYCLES_PER_PIT_TICK;

// A PIT on its own clock, with the PIC and speaker it is wired to.
class PitRig
{
public:
  PitRig()
    : m_speaker(m_clock),
      m_timer(m_clock, m_pic, m_speaker)
  {
  }

  [[nodiscard]] Machine::Pit& Timer() noexcept
  {
    return m_timer;
  }

  [[nodiscard]] Machine::Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  void SetClock(Machine::Cycles _cycle) noexcept
  {
    m_clock = _cycle;
  }

  // Moves the clock to _cycle, as the integrator does after a step, and advances the PIT. Returns
  // whether IRQ0 was raised, and if so acknowledges it and sends EOI, as the game's handler does.
  [[nodiscard]] bool StepTo(Machine::Cycles _cycle)
  {
    m_clock = _cycle;
    m_timer.Advance();
    if ((m_pic.RequestRegister() & 1u) == 0)
    {
      return false;
    }
    (void)m_pic.AcknowledgeInterrupt();
    m_pic.Out8(0x20, 0x20);
    return true;
  }

  void ProgramChannel0(std::uint8_t _controlWord, std::uint32_t _divisor)
  {
    m_timer.Out8(CONTROL, _controlWord);
    m_timer.Out8(CHANNEL_0, static_cast<std::uint8_t>(_divisor & 0xFF));
    m_timer.Out8(CHANNEL_0, static_cast<std::uint8_t>(_divisor >> 8));
  }

  [[nodiscard]] std::uint32_t ReadWord(std::uint16_t _port)
  {
    const std::uint32_t low = m_timer.In8(_port);
    const std::uint32_t high = m_timer.In8(_port);
    return low | (high << 8);
  }

private:
  Machine::Cycles m_clock = 0;
  Machine::Pic m_pic;
  Machine::Speaker m_speaker;
  Machine::Pit m_timer;
};

} // namespace

TEST_CLASS(PitTests)
{
public:
  // As the BIOS leaves it: mode 3, divisor 65536, so the first IRQ0 is 65536 ticks after power-on.
  TEST_METHOD(PowerOnChannel0RunsAtEighteenHertz)
  {
    PitRig rig;
    const Machine::Cycles first = 65536 * Machine::CYCLES_PER_PIT_TICK;
    Assert::IsFalse(rig.StepTo(first - 1));
    Assert::IsTrue(rig.StepTo(first));
    Assert::IsFalse(rig.StepTo(2 * first - 1));
    Assert::IsTrue(rig.StepTo(2 * first));
  }

  // The game's divisor: one IRQ0 every 1193 ticks, 4772 cycles, counted from the tick after the
  // count's second byte is written.
  TEST_METHOD(GameDivisorRaisesIrq0Every4772Cycles)
  {
    PitRig rig;
    rig.SetClock(1000);
    rig.ProgramChannel0(GAME_CONTROL_WORD, GAME_DIVISOR);

    std::vector<Machine::Cycles> raised;
    for (Machine::Cycles cycle = 1001; cycle <= 1000 + 20 * GAME_PERIOD_CYCLES + 10; ++cycle)
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

  // Instructions take uneven numbers of cycles, few of them a multiple of a tick. Advancing by
  // such steps must neither lose nor gain time: each IRQ0 lands in the step that crosses its edge.
  TEST_METHOD(SubTickRemaindersAreCarriedExactly)
  {
    PitRig rig;
    rig.SetClock(1000);
    rig.ProgramChannel0(GAME_CONTROL_WORD, GAME_DIVISOR);

    constexpr std::array<Machine::Cycles, 6> STEPS = {7, 13, 3, 23, 2, 9};
    Machine::Cycles cycle = 1000;
    std::size_t raised = 0;
    for (std::size_t step = 0; cycle < 2'000'000; ++step)
    {
      const Machine::Cycles previous = cycle;
      cycle += STEPS[step % STEPS.size()];
      if (rig.StepTo(cycle))
      {
        ++raised;
        const Machine::Cycles edge = 1004 + raised * GAME_PERIOD_CYCLES;
        Assert::IsTrue(previous < edge && edge <= cycle, L"IRQ0 in the step that crosses its edge");
      }
    }
    Assert::AreEqual(static_cast<std::size_t>((cycle - 1004) / GAME_PERIOD_CYCLES), raised, L"no edge lost or gained");
  }

  // A counter-latch command freezes the count until both bytes are read; otherwise reads are live.
  // Mode 3 with an odd count runs N-1, N-3 ... in its high half.
  TEST_METHOD(CounterLatchHoldsTheCountUntilRead)
  {
    PitRig rig;
    rig.SetClock(1000);
    rig.ProgramChannel0(GAME_CONTROL_WORD, GAME_DIVISOR); // loaded on tick 251

    rig.SetClock(1044); // tick 261, ten ticks in
    rig.Timer().Out8(CONTROL, 0x00);
    rig.SetClock(2000);
    Assert::AreEqual(1192u - 2u * 10u, rig.ReadWord(CHANNEL_0), L"the latched count");
    Assert::AreEqual(1192u - 2u * 249u, rig.ReadWord(CHANNEL_0), L"the live count at tick 500");
  }

  // Mode 2 counts N ... 1 and drops its output for the tick on which the count is 1.
  TEST_METHOD(RateGeneratorCountsDownAndPulsesLow)
  {
    PitRig rig;
    rig.Timer().Out8(CONTROL, 0x74); // channel 1, LSB then MSB, mode 2
    rig.Timer().Out8(CHANNEL_1, 5);
    rig.Timer().Out8(CHANNEL_1, 0); // loaded on tick 1

    constexpr std::array<std::uint32_t, 7> COUNTS = {5, 4, 3, 2, 1, 5, 4};
    for (std::size_t index = 0; index < COUNTS.size(); ++index)
    {
      const Machine::Cycles cycle = (1 + index) * Machine::CYCLES_PER_PIT_TICK;
      rig.SetClock(cycle);
      Assert::AreEqual(COUNTS[index], rig.ReadWord(CHANNEL_1));
      Assert::AreEqual(COUNTS[index] != 1, rig.Timer().OutputAt(1, cycle));
    }
  }

  // Mode 0 with one-byte access: the output is low until the count runs out, then stays high.
  TEST_METHOD(InterruptOnTerminalCountWithSingleByteAccess)
  {
    PitRig rig;
    rig.Timer().Out8(CONTROL, 0x50); // channel 1, LSB only, mode 0
    Assert::IsFalse(rig.Timer().OutputAt(1, 0), L"low after the control word");
    rig.Timer().Out8(CHANNEL_1, 10); // loaded on tick 1, terminal on tick 11
    Assert::IsFalse(rig.Timer().OutputAt(1, 43));
    Assert::IsTrue(rig.Timer().OutputAt(1, 44));
    Assert::IsTrue(rig.Timer().OutputAt(1, 4000), L"and it stays high");
    rig.SetClock(12);
    Assert::AreEqual(8u, std::uint32_t{rig.Timer().In8(CHANNEL_1)}, L"LSB only: one read is the whole count");

    rig.SetClock(400);
    rig.Timer().Out8(CONTROL, 0x60);   // channel 1, MSB only, mode 0
    rig.Timer().Out8(CHANNEL_1, 0x01); // 256, loaded on tick 101
    rig.SetClock(404);
    Assert::AreEqual(0x01u, std::uint32_t{rig.Timer().In8(CHANNEL_1)});
    Assert::IsFalse(rig.Timer().OutputAt(1, (101 + 256) * TICK - 1));
    Assert::IsTrue(rig.Timer().OutputAt(1, (101 + 256) * TICK));
  }

  // In mode 3 a count written while the counter runs waits for the end of the half-period in
  // progress. Divisor 1000 from tick 1; at tick 101 write 400: the high half ends on tick 501, the
  // new count's 200-tick low half follows, so IRQ0 comes on ticks 701 and 1101, not 1001.
  TEST_METHOD(CountWrittenWhileRunningWaitsForTheHalfPeriod)
  {
    PitRig rig;
    rig.ProgramChannel0(GAME_CONTROL_WORD, 1000);
    rig.SetClock(404);
    rig.Timer().Out8(CHANNEL_0, 0x90);
    rig.Timer().Out8(CHANNEL_0, 0x01);

    std::vector<Machine::Cycles> raised;
    for (Machine::Cycles cycle = 405; cycle <= 1200 * TICK; ++cycle)
    {
      if (rig.StepTo(cycle))
      {
        raised.push_back(cycle);
      }
    }
    Assert::AreEqual(std::size_t{2}, raised.size());
    Assert::AreEqual(701 * TICK, raised[0]);
    Assert::AreEqual(1101 * TICK, raised[1]);
  }

  // A control word sets a mode 3 output high at once. When it was low, that is a rising edge on
  // IRQ0's line, and the PIC sees it.
  TEST_METHOD(ControlWordThatRaisesALowOutputIsAnEdge)
  {
    PitRig rig;
    const Machine::Cycles lowHalf = 40'000 * Machine::CYCLES_PER_PIT_TICK; // phase 40000 of 65536: low
    Assert::IsFalse(rig.StepTo(lowHalf));
    Assert::IsFalse(rig.Timer().OutputAt(0, lowHalf));
    rig.Timer().Out8(CONTROL, GAME_CONTROL_WORD);
    Assert::IsTrue(rig.Timer().OutputAt(0, lowHalf));
    Assert::IsTrue(rig.StepTo(lowHalf));
  }

  // Channel 2 is gated by port 0x61 bit 0: low, its output is high; rising, it reloads on the next
  // tick. Its every change reaches the speaker's log.
  TEST_METHOD(Channel2GateRestartsTheWaveAndTheSpeakerHearsIt)
  {
    PitRig rig;
    Assert::IsFalse(rig.Timer().ChannelState(2).gate, L"gate low at power-on");
    Assert::IsTrue(rig.Timer().OutputAt(2, 0));

    rig.Timer().Out8(CONTROL, 0xB6); // the game's music: channel 2, LSB then MSB, mode 3
    rig.Timer().Out8(CHANNEL_2, 0x32);
    rig.Timer().Out8(CHANNEL_2, 0x00);
    rig.SetClock(400);
    rig.Timer().SetChannel2Gate(true); // reloaded on tick 101: high 101-125, low 126-150
    Assert::IsTrue(rig.Timer().OutputAt(2, 125 * TICK + 3));
    Assert::IsFalse(rig.Timer().OutputAt(2, 126 * TICK));
    Assert::IsFalse(rig.Timer().OutputAt(2, 150 * TICK + 3));
    Assert::IsTrue(rig.Timer().OutputAt(2, 151 * TICK));

    rig.SetClock(130 * TICK);
    rig.Timer().SetChannel2Gate(false);
    Assert::IsTrue(rig.Timer().OutputAt(2, 130 * TICK), L"a low gate forces the output high");

    const Machine::Speaker::Event& last = rig.Sound().Events().back();
    Assert::AreEqual(130 * TICK, last.at);
    Assert::IsFalse(last.channel2.gate);
    Assert::AreEqual(0x32u, last.channel2.reload);
  }
};

} // namespace MachineTests
