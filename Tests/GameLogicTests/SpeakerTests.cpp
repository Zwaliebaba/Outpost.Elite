#include "pch.h"

#include "Speaker.h"
#include "Timing.h"

#include <algorithm>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint8_t SPEAKER_OFF = 0x4C;   // the system control port as the BIOS leaves it
constexpr std::uint8_t DATA_ONLY = 0x4E;     // bit 1: the cone driven directly
constexpr std::uint8_t GATE_AND_DATA = 0x4F; // bits 0 and 1: channel 2's wave
constexpr std::int32_t FULL = Elite::Speaker::FULL_LEVEL;
constexpr std::int32_t SAMPLE_44_LEVEL = 14856; // 90.7% of full

// The speaker as the game drives it: the tone, and the system control port's two bits as the platform passes them on, which
// PlatformTests checks.
class SpeakerRig
{
public:
  SpeakerRig()
    : m_speaker(m_clock)
  {
  }

  [[nodiscard]] Elite::Speaker& Sound() noexcept
  {
    return m_speaker;
  }

  void SetSystemControl(Elite::Milliseconds _time, std::uint8_t _value)
  {
    m_clock = _time;
    m_speaker.SetBits((_value & 0x01) != 0, (_value & 0x02) != 0);
  }

  // What the game's music does for a note: channel 2, both bytes, mode 3 (0x7372).
  void PlayTone(Elite::Milliseconds _time, std::uint16_t _divisor)
  {
    m_clock = _time;
    m_speaker.SetTone(_divisor);
  }

  [[nodiscard]] std::vector<std::int16_t> Render(Elite::Milliseconds _from, Elite::Milliseconds _to, std::uint32_t _sampleRate) const
  {
    std::vector<std::int16_t> samples(Elite::Speaker::SampleCount(_from, _to, _sampleRate));
    const std::size_t written = m_speaker.RenderSamples(_from, _to, _sampleRate, samples);
    Assert::AreEqual(samples.size(), written);
    return samples;
  }

private:
  Elite::Milliseconds m_clock = 0;
  Elite::Speaker m_speaker;
};

[[nodiscard]] std::int32_t Spread(const std::vector<std::int16_t>& _samples, std::size_t _skip)
{
  const auto [low, high] = std::minmax_element(_samples.begin() + static_cast<std::ptrdiff_t>(_skip), _samples.end());
  return std::int32_t{*high} - std::int32_t{*low};
}

} // namespace

// The PC speaker (Speaker, D33): what the host hears, rendered from the timer's channel 2 and the system control port, its
// changes stamped in milliseconds (D87).
TEST_CLASS(SpeakerTests)
{
public:
  // With the gate clear the level is bit 1 alone: full on, then nothing.
  TEST_METHOD(DataBitAloneDrivesTheCone)
  {
    SpeakerRig rig;
    rig.SetSystemControl(0, DATA_ONLY);
    rig.SetSystemControl(21, SPEAKER_OFF);

    const std::vector<std::int16_t> on = rig.Render(0, 21, 48'000);
    Assert::AreEqual(std::size_t{1007}, on.size(), L"21 ms at 48 kHz, the last sample ending past it");
    for (const std::int16_t sample : on)
    {
      Assert::AreEqual(FULL, std::int32_t{sample});
    }
    const std::vector<std::int16_t> off = rig.Render(21, 42, 48'000);
    Assert::IsTrue(off[0] > 0 && off[0] < FULL, L"the sample that straddles the change is partly on");
    for (std::size_t index = 1; index < off.size(); ++index)
    {
      Assert::AreEqual(0, std::int32_t{off[index]});
    }
  }

  // A sample is the mean level over its interval. At 44.1 kHz sample 44 spans 997.8 to 1020.4 us; switched on at 1 ms, it
  // is on for 90.7% of it.
  TEST_METHOD(SampleIsTheMeanLevelOverItsInterval)
  {
    SpeakerRig rig;
    rig.SetSystemControl(1, DATA_ONLY);
    const std::vector<std::int16_t> samples = rig.Render(0, 2, 44'100);
    Assert::AreEqual(std::size_t{88}, samples.size());
    for (std::size_t index = 0; index < 44; ++index)
    {
      Assert::AreEqual(0, std::int32_t{samples[index]});
    }
    Assert::AreEqual(SAMPLE_44_LEVEL, std::int32_t{samples[44]});
    for (std::size_t index = 45; index < samples.size(); ++index)
    {
      Assert::AreEqual(FULL, std::int32_t{samples[index]});
    }
  }

  // A note: divisor 1193, about 1 kHz, is a square wave between 0 and full, high 597 ticks of 1193. In 50 ms 49 periods
  // begin after the first, which starts high; the 50th begins too late in the last sample to lift it past half.
  TEST_METHOD(NoteIsASquareWaveAtItsPitch)
  {
    SpeakerRig rig;
    rig.SetSystemControl(0, GATE_AND_DATA);
    rig.PlayTone(0, 1193);
    const std::vector<std::int16_t> samples = rig.Render(0, 50, 48'000);

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
    Assert::AreEqual(std::size_t{49}, rises, L"one rise a period, the first period starting high");
    Assert::AreEqual(0, std::int32_t{*std::min_element(samples.begin(), samples.end())});
    Assert::AreEqual(FULL, std::int32_t{*std::max_element(samples.begin(), samples.end())});
    const std::int64_t mean = sum / static_cast<std::int64_t>(samples.size());
    const std::int64_t expected = FULL * 597 / 1193;
    Assert::IsTrue(mean > expected - FULL / 100 && mean < expected + FULL / 100, L"high 597 ticks in 1193");
  }

  // The game's rests are channel 2 at divisor 32h, about 23.9 kHz, and must not be heard. At 44.1 and 48 kHz a plain box
  // filter would beat against it; it comes out a steady half level.
  TEST_METHOD(RestAtDivisor32IsNearSilent)
  {
    for (const std::uint32_t rate : {44'100u, 48'000u})
    {
      SpeakerRig rig;
      rig.SetSystemControl(0, GATE_AND_DATA);
      rig.PlayTone(0, 0x32);
      const std::vector<std::int16_t> samples = rig.Render(0, 100, rate);
      Assert::IsTrue(Spread(samples, 1) <= 1, L"no audible ripple");
      Assert::AreEqual(FULL / 2, std::int32_t{samples.back()});
    }

    // SilenceSpeakerTimer's divisor 2 (0x7436), about 597 kHz, too.
    SpeakerRig rig;
    rig.SetSystemControl(0, GATE_AND_DATA);
    rig.PlayTone(0, 2);
    Assert::IsTrue(Spread(rig.Render(0, 100, 48'000), 1) <= 1);
  }

  // The averaging is for waves nobody could hear: divisor 100, about 11.9 kHz, is a wave at 48 kHz, but above a 22.05 kHz
  // stream's Nyquist frequency it is a steady level rather than an alias.
  TEST_METHOD(OnlyInaudibleWavesAreAveraged)
  {
    SpeakerRig rig;
    rig.SetSystemControl(0, GATE_AND_DATA);
    rig.PlayTone(0, 100);
    Assert::IsTrue(Spread(rig.Render(0, 100, 48'000), 1) > FULL / 4, L"11.9 kHz is rendered at 48 kHz");
    Assert::IsTrue(Spread(rig.Render(0, 100, 22'050), 1) <= 1, L"and averaged above 11.025 kHz Nyquist");
  }

  // Rendering (a, b] then (b, c] gives exactly the samples of (a, c].
  TEST_METHOD(RenderingInPiecesMatchesRenderingAtOnce)
  {
    SpeakerRig rig;
    rig.SetSystemControl(0, GATE_AND_DATA);
    rig.PlayTone(1, 1193);
    rig.PlayTone(7, 0x32);
    rig.SetSystemControl(11, DATA_ONLY);
    for (Elite::Milliseconds time = 13; time < 21; ++time)
    {
      rig.SetSystemControl(time, time % 2 == 0 ? DATA_ONLY : SPEAKER_OFF);
    }
    rig.PlayTone(23, 600);
    rig.SetSystemControl(23, GATE_AND_DATA);

    const std::vector<std::int16_t> whole = rig.Render(0, 31, 44'100);
    std::vector<std::int16_t> pieces;
    Elite::Milliseconds from = 0;
    for (const Elite::Milliseconds to : {3u, 4u, 14u, 23u, 31u})
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
    for (Elite::Milliseconds time = 0; time < 21; ++time)
    {
      rig.SetSystemControl(time, time % 2 == 0 ? DATA_ONLY : SPEAKER_OFF);
    }
    const std::vector<std::int16_t> before = rig.Render(10, 21, 48'000);
    const std::size_t events = rig.Sound().Events().size();

    rig.Sound().DiscardBefore(Elite::Speaker::FirstSampleStart(10, 48'000));
    Assert::IsTrue(rig.Sound().Events().size() < events);
    Assert::IsTrue(before == rig.Render(10, 21, 48'000));
  }

  // The tone is gated by the gate bit: low, its output is high; rising, it reloads on the next timer tick. Its every change
  // reaches the log, stamped with the millisecond it was made in.
  TEST_METHOD(GateRestartsTheToneAndTheLogHearsIt)
  {
    SpeakerRig rig;
    Assert::IsFalse(rig.Sound().Now().tone.gate, L"gate low at power-on");
    Assert::IsTrue(rig.Sound().ToneOutputAt(0));

    rig.PlayTone(0, 0x32);                       // the game's music: channel 2, both bytes, mode 3
    rig.SetSystemControl(2, SPEAKER_OFF | 0x01); // 2 ms is timer tick 2386: reloaded on tick 2387
    Assert::AreEqual(std::int64_t{2387}, rig.Sound().Now().tone.originTick);
    Assert::IsTrue(rig.Sound().Now().tone.gate);

    rig.SetSystemControl(3, SPEAKER_OFF);
    Assert::IsTrue(rig.Sound().ToneOutputAt(3), L"a low gate forces the output high");

    const Elite::Speaker::Event& last = rig.Sound().Events().back();
    Assert::AreEqual(Elite::Milliseconds{3}, last.at);
    Assert::IsFalse(last.tone.gate);
    Assert::AreEqual(0x32u, last.tone.reload);
  }
};

} // namespace GameLogicTests
