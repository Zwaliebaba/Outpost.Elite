#include "pch.h"

#include "Pic.h"
#include "Pit.h"
#include "Speaker.h"
#include "Timing.h"

#include <algorithm>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

constexpr std::uint8_t SPEAKER_OFF = 0x4C;   // port 0x61 as the BIOS leaves it
constexpr std::uint8_t DATA_ONLY = 0x4E;     // bit 1: the cone driven directly
constexpr std::uint8_t GATE_AND_DATA = 0x4F; // bits 0 and 1: channel 2's wave
constexpr std::int32_t FULL = Machine::Speaker::FULL_LEVEL;

// The speaker as it is wired: channel 2 through the PIT, and port 0x61 as the PPI passes it on
// (bit 0 to channel 2's gate, the byte to the speaker), which KeyboardTests checks.
class SpeakerRig
{
public:
  SpeakerRig()
    : m_speaker(m_clock),
      m_timer(m_clock, m_pic, m_speaker)
  {
  }

  [[nodiscard]] Machine::Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  void SetPortB(Machine::Cycles _cycle, std::uint8_t _value)
  {
    m_clock = _cycle;
    m_timer.SetChannel2Gate((_value & 0x01) != 0);
    m_speaker.SetPortB(_value);
  }

  // What the game's music does for a note: channel 2, LSB then MSB, mode 3 (0x7392).
  void PlayTone(Machine::Cycles _cycle, std::uint16_t _divisor)
  {
    m_clock = _cycle;
    m_timer.Out8(0x43, 0xB6);
    m_timer.Out8(0x42, static_cast<std::uint8_t>(_divisor & 0xFF));
    m_timer.Out8(0x42, static_cast<std::uint8_t>(_divisor >> 8));
  }

  [[nodiscard]] std::vector<std::int16_t> Render(Machine::Cycles _from, Machine::Cycles _to, std::uint32_t _sampleRate) const
  {
    std::vector<std::int16_t> samples(Machine::Speaker::SampleCount(_from, _to, _sampleRate));
    const std::size_t written = m_speaker.RenderSamples(_from, _to, _sampleRate, samples);
    Assert::AreEqual(samples.size(), written);
    return samples;
  }

private:
  Machine::Cycles m_clock = 0;
  Machine::Pic m_pic;
  Machine::Speaker m_speaker;
  Machine::Pit m_timer;
};

[[nodiscard]] std::int32_t Spread(const std::vector<std::int16_t>& _samples, std::size_t _skip)
{
  const auto [low, high] = std::minmax_element(_samples.begin() + static_cast<std::ptrdiff_t>(_skip), _samples.end());
  return std::int32_t{*high} - std::int32_t{*low};
}

} // namespace

TEST_CLASS(SpeakerTests)
{
public:
  // With the gate clear the level is bit 1 alone: full on, then nothing.
  TEST_METHOD(DataBitAloneDrivesTheCone)
  {
    SpeakerRig rig;
    rig.SetPortB(0, DATA_ONLY);
    rig.SetPortB(100'000, SPEAKER_OFF);

    const std::vector<std::int16_t> on = rig.Render(0, 100'000, 48'000);
    Assert::AreEqual(std::size_t{1005}, on.size());
    for (const std::int16_t sample : on)
    {
      Assert::AreEqual(FULL, std::int32_t{sample});
    }
    const std::vector<std::int16_t> off = rig.Render(100'000, 200'000, 48'000);
    Assert::IsTrue(off[0] > 0 && off[0] < FULL, L"the sample that straddles the change is partly on");
    for (std::size_t index = 1; index < off.size(); ++index)
    {
      Assert::AreEqual(0, std::int32_t{off[index]});
    }
  }

  // A sample is the mean level over its interval. At 44.1 kHz sample 9 spans cycles 974.0 to 1082.2;
  // switched on at cycle 1000, it is on for 76.0% of it.
  TEST_METHOD(SampleIsTheMeanLevelOverItsInterval)
  {
    SpeakerRig rig;
    rig.SetPortB(1000, DATA_ONLY);
    const std::vector<std::int16_t> samples = rig.Render(0, 2000, 44'100);
    Assert::AreEqual(std::size_t{18}, samples.size());
    for (std::size_t index = 0; index < 9; ++index)
    {
      Assert::AreEqual(0, std::int32_t{samples[index]});
    }
    Assert::AreEqual(12452, std::int32_t{samples[9]});
    for (std::size_t index = 10; index < samples.size(); ++index)
    {
      Assert::AreEqual(FULL, std::int32_t{samples[index]});
    }
  }

  // A note: divisor 1193, about 1 kHz, is a square wave between 0 and full, high 597 ticks of 1193.
  TEST_METHOD(NoteIsASquareWaveAtItsPitch)
  {
    SpeakerRig rig;
    rig.SetPortB(0, GATE_AND_DATA);
    rig.PlayTone(0, 1193);
    const Machine::Cycles periodCycles = 1193 * Machine::CYCLES_PER_PIT_TICK;
    const std::vector<std::int16_t> samples = rig.Render(0, 100 * periodCycles, 48'000);

    std::int64_t sum = 0;
    std::size_t rises = 0;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
      sum += samples[index];
      if (index > 0 && samples[index - 1] < FULL / 2 && samples[index] >= FULL / 2)
      {
        ++rises;
      }
    }
    Assert::AreEqual(std::size_t{99}, rises, L"one rise a period, the first period starting high");
    Assert::AreEqual(0, std::int32_t{*std::min_element(samples.begin(), samples.end())});
    Assert::AreEqual(FULL, std::int32_t{*std::max_element(samples.begin(), samples.end())});
    const std::int64_t mean = sum / static_cast<std::int64_t>(samples.size());
    const std::int64_t expected = FULL * 597 / 1193;
    Assert::IsTrue(mean > expected - 40 && mean < expected + 40, L"high 597 ticks in 1193");
  }

  // The game's rests are channel 2 at divisor 0x32, about 23.9 kHz, and must not be heard. At
  // 44.1 and 48 kHz a plain box filter would beat against it; it comes out a steady half level.
  TEST_METHOD(RestAtDivisor32IsNearSilent)
  {
    for (const std::uint32_t rate : {44'100u, 48'000u})
    {
      SpeakerRig rig;
      rig.SetPortB(0, GATE_AND_DATA);
      rig.PlayTone(0, 0x32);
      const std::vector<std::int16_t> samples = rig.Render(0, 477'273, rate); // 0.1 s
      Assert::IsTrue(Spread(samples, 1) <= 1, L"no audible ripple");
      Assert::AreEqual(FULL / 2, std::int32_t{samples.back()});
    }

    // SilenceSpeakerTimer's divisor 2 (0x7456), about 597 kHz, too.
    SpeakerRig rig;
    rig.SetPortB(0, GATE_AND_DATA);
    rig.PlayTone(0, 2);
    Assert::IsTrue(Spread(rig.Render(0, 477'273, 48'000), 1) <= 1);
  }

  // The averaging is for waves nobody could hear: divisor 100, about 11.9 kHz, is a wave at 48 kHz,
  // but above a 22.05 kHz stream's Nyquist frequency it is a steady level rather than an alias.
  TEST_METHOD(OnlyInaudibleWavesAreAveraged)
  {
    SpeakerRig rig;
    rig.SetPortB(0, GATE_AND_DATA);
    rig.PlayTone(0, 100);
    Assert::IsTrue(Spread(rig.Render(0, 477'273, 48'000), 1) > FULL / 4, L"11.9 kHz is rendered at 48 kHz");
    Assert::IsTrue(Spread(rig.Render(0, 477'273, 22'050), 1) <= 1, L"and averaged above 11.025 kHz Nyquist");
  }

  // Rendering (a, b] then (b, c] gives exactly the samples of (a, c].
  TEST_METHOD(RenderingInPiecesMatchesRenderingAtOnce)
  {
    SpeakerRig rig;
    rig.SetPortB(0, GATE_AND_DATA);
    rig.PlayTone(1'000, 1193);
    rig.PlayTone(30'001, 0x32);
    rig.SetPortB(50'003, DATA_ONLY);
    for (Machine::Cycles cycle = 60'000; cycle < 100'000; cycle += 4'772)
    {
      rig.SetPortB(cycle, ((cycle / 4'772) % 2) == 0 ? DATA_ONLY : SPEAKER_OFF);
    }
    rig.PlayTone(110'000, 600);
    rig.SetPortB(110'000, GATE_AND_DATA);

    const std::vector<std::int16_t> whole = rig.Render(0, 150'000, 44'100);
    std::vector<std::int16_t> pieces;
    Machine::Cycles from = 0;
    for (const Machine::Cycles to : {12'345u, 12'346u, 67'890u, 110'000u, 150'000u})
    {
      const std::vector<std::int16_t> piece = rig.Render(from, to, 44'100);
      pieces.insert(pieces.end(), piece.begin(), piece.end());
      from = to;
    }
    Assert::IsTrue(whole == pieces);
  }

  // Discarding what is no longer needed leaves later samples as they were.
  TEST_METHOD(DiscardKeepsWhatLaterSamplesNeed)
  {
    SpeakerRig rig;
    for (Machine::Cycles cycle = 0; cycle < 100'000; cycle += 4'772)
    {
      rig.SetPortB(cycle, ((cycle / 4'772) % 2) == 0 ? DATA_ONLY : SPEAKER_OFF);
    }
    const std::vector<std::int16_t> before = rig.Render(50'000, 100'000, 48'000);
    const std::size_t events = rig.Sound().Events().size();

    rig.Sound().DiscardBefore(Machine::Speaker::FirstSampleStart(50'000, 48'000));
    Assert::IsTrue(rig.Sound().Events().size() < events);
    Assert::IsTrue(before == rig.Render(50'000, 100'000, 48'000));
  }
};

} // namespace MachineTests
