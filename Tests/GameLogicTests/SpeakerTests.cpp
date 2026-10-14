#include "pch.h"

#include "Speaker.h"
#include "Timing.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The speaker's two bits as the game sets them (Host::SetSpeakerBits): bit 0 the gate, bit 1 the data.
constexpr std::uint8_t SPEAKER_OFF = 0x00;
constexpr std::uint8_t DATA_ONLY = 0x02;     // the cone driven directly
constexpr std::uint8_t GATE_AND_DATA = 0x03; // the tone's wave
constexpr std::int32_t FULL = Elite::Speaker::FULL_LEVEL;
constexpr std::int32_t STRADDLE_LEVEL = 1638;   // 21 ms in sample 926 at 44.1 kHz: on for its first tenth
constexpr std::int32_t SAMPLE_44_LEVEL = 14746; // 1 ms in sample 44 at 44.1 kHz: on for its last nine tenths

// The speaker as the game drives it: the tone, and the two bits as the Host passes them on, which HostTests checks.
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

  void SetBits(Elite::Milliseconds _time, std::uint8_t _bits)
  {
    m_clock = _time;
    m_speaker.SetBits((_bits & 0x01) != 0, (_bits & 0x02) != 0);
  }

  // What the game's music does for a note (TickMusic).
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

// The speaker (Speaker, ADR-055): what the host hears, synthesised from the tone and the two bits, stamped in milliseconds.
TEST_CLASS(SpeakerTests)
{
public:
  // With the gate clear the level is the data bit alone: full on, then nothing.
  TEST_METHOD(DataBitAloneDrivesTheCone)
  {
    SpeakerRig rig;
    rig.SetBits(0, DATA_ONLY);
    rig.SetBits(21, SPEAKER_OFF);

    const std::vector<std::int16_t> on = rig.Render(0, 21, 44'100);
    Assert::AreEqual(std::size_t{926}, on.size(), L"21 ms at 44.1 kHz, the last sample ending past it");
    for (const std::int16_t sample : on)
    {
      Assert::AreEqual(FULL, std::int32_t{sample});
    }
    const std::vector<std::int16_t> off = rig.Render(21, 42, 44'100);
    Assert::AreEqual(STRADDLE_LEVEL, std::int32_t{off[0]}, L"the sample that straddles the change is partly on");
    for (std::size_t index = 1; index < off.size(); ++index)
    {
      Assert::AreEqual(0, std::int32_t{off[index]});
    }
  }

  // A sample is the mean level over its interval. At 44.1 kHz sample 44 spans 997.7 to 1020.4 us; switched on at 1 ms, it
  // is on for nine tenths of it.
  TEST_METHOD(SampleIsTheMeanLevelOverItsInterval)
  {
    SpeakerRig rig;
    rig.SetBits(1, DATA_ONLY);
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

  // A note: divisor 1193, 1000.15 Hz, is a square wave between 0 and full, high for half its period. In 50 ms 49 periods begin
  // after the first, which starts high; the 50th begins too late in the last sample to lift it past half.
  TEST_METHOD(NoteIsASquareWaveAtItsPitch)
  {
    SpeakerRig rig;
    rig.SetBits(0, GATE_AND_DATA);
    rig.PlayTone(0, 1193);
    Assert::IsTrue(std::abs(Elite::Speaker::ToneHz(1193) - 1000.15) < 0.01, L"the timer's clock over the divisor");
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
    Assert::IsTrue(mean > FULL / 2 - FULL / 100 && mean < FULL / 2 + FULL / 100, L"high half the time");
  }

  // The game's rests are the tone at divisor 32h, about 23.9 kHz, and must not be heard. At 44.1 and 48 kHz a plain box
  // filter would beat against it; it comes out a steady half level.
  TEST_METHOD(RestAtDivisor32IsNearSilent)
  {
    for (const std::uint32_t rate : {44'100u, 48'000u})
    {
      SpeakerRig rig;
      rig.SetBits(0, GATE_AND_DATA);
      rig.PlayTone(0, 0x32);
      const std::vector<std::int16_t> samples = rig.Render(0, 100, rate);
      Assert::IsTrue(Spread(samples, 0) == 0, L"no ripple at all");
      Assert::AreEqual(FULL / 2, std::int32_t{samples.back()});
    }

    // SilenceSpeakerTimer's divisor 2, about 597 kHz, too.
    SpeakerRig rig;
    rig.SetBits(0, GATE_AND_DATA);
    rig.PlayTone(0, 2);
    Assert::IsTrue(Spread(rig.Render(0, 100, 48'000), 0) == 0);
  }

  // The averaging is for waves nobody could hear: divisor 100, about 11.9 kHz, is a wave at 48 kHz, but above a 22.05 kHz
  // stream's Nyquist frequency it is a steady level rather than an alias.
  TEST_METHOD(OnlyInaudibleWavesAreAveraged)
  {
    SpeakerRig rig;
    rig.SetBits(0, GATE_AND_DATA);
    rig.PlayTone(0, 100);
    Assert::IsTrue(Spread(rig.Render(0, 100, 48'000), 1) > FULL / 4, L"11.9 kHz is rendered at 48 kHz");
    Assert::IsTrue(Spread(rig.Render(0, 100, 22'050), 0) == 0, L"and averaged above 11.025 kHz Nyquist");
  }

  // Rendering (a, b] then (b, c] gives exactly the samples of (a, c].
  TEST_METHOD(RenderingInPiecesMatchesRenderingAtOnce)
  {
    SpeakerRig rig;
    rig.SetBits(0, GATE_AND_DATA);
    rig.PlayTone(1, 1193);
    rig.PlayTone(7, 0x32);
    rig.SetBits(11, DATA_ONLY);
    for (Elite::Milliseconds time = 13; time < 21; ++time)
    {
      rig.SetBits(time, time % 2 == 0 ? DATA_ONLY : SPEAKER_OFF);
    }
    rig.PlayTone(23, 600);
    rig.SetBits(23, GATE_AND_DATA);

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
      rig.SetBits(time, time % 2 == 0 ? DATA_ONLY : SPEAKER_OFF);
    }
    const std::vector<std::int16_t> before = rig.Render(10, 21, 44'100);
    const std::size_t events = rig.Sound().Events().size();

    rig.Sound().DiscardBefore(Elite::Speaker::FirstSampleStart(10, 44'100));
    Assert::IsTrue(rig.Sound().Events().size() < events);
    Assert::IsTrue(before == rig.Render(10, 21, 44'100));
  }

  // A rising gate starts the tone again, high; a low gate holds it high, so the level is the data bit alone. Every change
  // reaches the log, stamped with the millisecond it was made in.
  TEST_METHOD(GateRestartsTheToneAndTheLogHearsIt)
  {
    SpeakerRig rig;
    Assert::IsFalse(rig.Sound().Now().gate, L"gate low at power-on");

    rig.PlayTone(0, 1193);
    rig.SetBits(2, GATE_AND_DATA);
    Assert::AreEqual(Elite::Milliseconds{2}, rig.Sound().Now().toneStart, L"the tone starts again at the rising gate");
    const std::vector<std::int16_t> started = rig.Render(2, 3, 48'000);
    Assert::AreEqual(FULL, std::int32_t{started.front()}, L"high as it starts");

    rig.SetBits(3, DATA_ONLY);
    const std::vector<std::int16_t> held = rig.Render(3, 5, 48'000);
    for (const std::int16_t sample : held)
    {
      Assert::AreEqual(FULL, std::int32_t{sample}, L"a low gate leaves the data bit alone");
    }

    const Elite::Speaker::Event& last = rig.Sound().Events().back();
    Assert::AreEqual(Elite::Milliseconds{3}, last.at);
    Assert::IsFalse(last.gate);
    Assert::IsTrue(std::abs(last.toneHz - Elite::Speaker::ToneHz(1193)) < 1e-9);
  }
};

} // namespace GameLogicTests
