#include "pch.h"

#include "SidSynth.h"

namespace Outpost
{

  namespace
  {
    /// The control register's bits.
    constexpr std::uint8_t GATE = 0x01;
    constexpr std::uint8_t SYNC = 0x02;
    constexpr std::uint8_t RING = 0x04;
    constexpr std::uint8_t TEST = 0x08;
    constexpr std::uint8_t TRIANGLE = 0x10;
    constexpr std::uint8_t SAWTOOTH = 0x20;
    constexpr std::uint8_t PULSE = 0x40;
    constexpr std::uint8_t NOISE = 0x80;

    constexpr std::uint32_t ACCUMULATOR_MASK = 0xFFFFFF;
    constexpr std::uint32_t SHIFT_REGISTER_MASK = 0x7FFFFF;
    constexpr std::uint32_t SHIFT_REGISTER_RESET = 0x7FFFF8;

    /*
     * The envelope's rate table: how many cycles between steps for each of the sixteen settings.
     *
     * Attack climbs one level per period; decay and release fall one level per period MULTIPLIED by
     * the exponential divisor below, which is what makes the fall curve rather than slope. The
     * periods are the chip's own -- they are what the fifteen-bit rate counter's comparison values
     * work out to -- and they are documented in every SID description since the 1980s.
     */
    constexpr std::array<std::uint16_t, 16> RATE_PERIODS = {
      9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251,
    };

    /// The exponential divisor, by envelope level: the fall slows at 93, 54, 26, 14 and 6.
    [[nodiscard]] std::uint8_t ExponentialPeriod(std::uint8_t _level) noexcept
    {
      if (_level > 0x5D)
      {
        return 1;
      }
      if (_level > 0x36)
      {
        return 2;
      }
      if (_level > 0x1A)
      {
        return 4;
      }
      if (_level > 0x0E)
      {
        return 8;
      }
      if (_level > 0x06)
      {
        return 16;
      }
      if (_level > 0x00)
      {
        return 30;
      }
      return 1;
    }

    /// The noise waveform: eight taps of the shift register, spread over the top eight of twelve bits.
    [[nodiscard]] std::uint32_t NoiseOutput(std::uint32_t _shift) noexcept
    {
      return ((_shift >> 22) & 1u) << 11 | ((_shift >> 20) & 1u) << 10 | ((_shift >> 16) & 1u) << 9 | ((_shift >> 13) & 1u) << 8 |
             ((_shift >> 11) & 1u) << 7 | ((_shift >> 7) & 1u) << 6 | ((_shift >> 4) & 1u) << 5 | ((_shift >> 2) & 1u) << 4;
    }

    /// What the three voices can add up to before the volume: 2048 either way, times 255, times 3.
    /// Shifting by ten after the volume nibble puts full scale at about seventy per cent of 16 bits.
    constexpr int OUTPUT_SHIFT = 10;
  } // namespace

  SidSynth::SidSynth(std::uint32_t _clockHz, std::uint32_t _sampleRate) noexcept
    : m_clockHz(_clockHz),
      m_sampleRate(_sampleRate)
  {
  }

  void SidSynth::Reset() noexcept
  {
    m_voices = {};
    m_volume = 0;
    m_cycleRemainder = 0;
  }

  void SidSynth::Write(std::uint8_t _register, std::uint8_t _value) noexcept
  {
    if (_register >= REGISTER_COUNT)
    {
      return;
    }

    if (_register < 0x15)
    {
      Voice& voice = m_voices[_register / 7u];
      switch (_register % 7u)
      {
      case 0:
        voice.frequency = static_cast<std::uint16_t>((voice.frequency & 0xFF00u) | _value);
        break;
      case 1:
        voice.frequency = static_cast<std::uint16_t>((voice.frequency & 0x00FFu) | (_value << 8));
        break;
      case 2:
        voice.pulseWidth = static_cast<std::uint16_t>((voice.pulseWidth & 0x0F00u) | _value);
        break;
      case 3:
        voice.pulseWidth = static_cast<std::uint16_t>((voice.pulseWidth & 0x00FFu) | ((_value & 0x0Fu) << 8));
        break;
      case 4:
      {
        /*
         * The gate's edges are the envelope's only inputs: rising starts an attack from wherever the
         * level is, falling starts a release from wherever it is. The test bit holds the oscillator
         * at zero and resets the noise generator for as long as it is set.
         */
        const bool gate = (_value & GATE) != 0;
        if (gate && !voice.gate)
        {
          voice.phase = Phase::Attack;
          voice.holdAtZero = false;
        }
        else if (!gate && voice.gate)
        {
          voice.phase = Phase::Release;
        }
        voice.gate = gate;
        voice.control = _value;
        if ((_value & TEST) != 0)
        {
          voice.accumulator = 0;
          voice.shiftRegister = SHIFT_REGISTER_RESET;
        }
        break;
      }
      case 5:
        voice.attackDecay = _value;
        break;
      case 6:
        voice.sustainRelease = _value;
        break;
      default:
        break;
      }
      return;
    }

    // The filter registers are accepted and ignored; the volume nibble is what this model plays.
    if (_register == 0x18)
    {
      m_volume = static_cast<std::uint8_t>(_value & 0x0Fu);
    }
  }

  void SidSynth::ClockOscillator(Voice& _voice) noexcept
  {
    if ((_voice.control & TEST) != 0)
    {
      _voice.accumulator = 0;
      _voice.bit19 = false;
      _voice.msb = false;
      return;
    }

    _voice.accumulator = (_voice.accumulator + _voice.frequency) & ACCUMULATOR_MASK;

    // The noise generator shifts on bit 19's rising edge: taps 22 and 17 feed the new bit 0.
    const bool bit19 = (_voice.accumulator & 0x080000u) != 0;
    if (bit19 && !_voice.bit19)
    {
      const std::uint32_t feedback = ((_voice.shiftRegister >> 22) ^ (_voice.shiftRegister >> 17)) & 1u;
      _voice.shiftRegister = ((_voice.shiftRegister << 1) | feedback) & SHIFT_REGISTER_MASK;
    }
    _voice.bit19 = bit19;
    _voice.msb = (_voice.accumulator & 0x800000u) != 0;
  }

  void SidSynth::ClockEnvelope(Voice& _voice) noexcept
  {
    const std::uint8_t rate = (_voice.phase == Phase::Attack)         ? static_cast<std::uint8_t>(_voice.attackDecay >> 4)
                              : (_voice.phase == Phase::DecaySustain) ? static_cast<std::uint8_t>(_voice.attackDecay & 0x0Fu)
                                                                      : static_cast<std::uint8_t>(_voice.sustainRelease & 0x0Fu);

    ++_voice.rateCounter;
    if (_voice.rateCounter < RATE_PERIODS[rate])
    {
      return;
    }
    _voice.rateCounter = 0;

    if (_voice.phase == Phase::Attack)
    {
      // Linear, and the top is where decay begins.
      if (_voice.envelope < 0xFF)
      {
        ++_voice.envelope;
      }
      if (_voice.envelope == 0xFF)
      {
        _voice.phase = Phase::DecaySustain;
      }
      return;
    }

    // Decay and release fall through the exponential divisor, decay only as far as the sustain level.
    ++_voice.exponentialCounter;
    if (_voice.exponentialCounter < ExponentialPeriod(_voice.envelope))
    {
      return;
    }
    _voice.exponentialCounter = 0;

    if (_voice.holdAtZero)
    {
      return;
    }

    if (_voice.phase == Phase::DecaySustain)
    {
      const std::uint8_t sustain = static_cast<std::uint8_t>((_voice.sustainRelease >> 4) * 0x11u);
      if (_voice.envelope > sustain)
      {
        --_voice.envelope;
      }
      return;
    }

    if (_voice.envelope > 0)
    {
      --_voice.envelope;
    }
    if (_voice.envelope == 0)
    {
      _voice.holdAtZero = true;
    }
  }

  std::int32_t SidSynth::VoiceOutput(const Voice& _voice, const Voice& _ringSource) const noexcept
  {
    const std::uint8_t waveforms = static_cast<std::uint8_t>(_voice.control & 0xF0u);
    if (waveforms == 0 || _voice.envelope == 0)
    {
      return 0;
    }

    const std::uint32_t accumulator = _voice.accumulator;
    std::uint32_t output = 0xFFF;

    if ((waveforms & TRIANGLE) != 0)
    {
      // The top bit folds the ramp; with ring modulation the fold comes from the other voice too.
      bool fold = (accumulator & 0x800000u) != 0;
      if ((_voice.control & RING) != 0)
      {
        fold ^= _ringSource.msb;
      }
      const std::uint32_t ramp = fold ? (~accumulator & ACCUMULATOR_MASK) : accumulator;
      output &= (ramp >> 11) & 0xFFFu;
    }
    if ((waveforms & SAWTOOTH) != 0)
    {
      output &= accumulator >> 12;
    }
    if ((waveforms & PULSE) != 0)
    {
      const bool high = (_voice.control & TEST) != 0 || (accumulator >> 12) >= _voice.pulseWidth;
      output &= high ? 0xFFFu : 0u;
    }
    if ((waveforms & NOISE) != 0)
    {
      output &= NoiseOutput(_voice.shiftRegister);
    }

    return (static_cast<std::int32_t>(output) - 0x800) * static_cast<std::int32_t>(_voice.envelope);
  }

  void SidSynth::Clock() noexcept
  {
    // Every oscillator advances on the cycle, and THEN sync is applied from the top bits' edges --
    // voice 1 to voice 3, 2 to 1, 3 to 2 -- so the order they are clocked in cannot matter.
    std::array<bool, 3> rose{};
    for (std::size_t voice = 0; voice < 3u; ++voice)
    {
      const bool before = m_voices[voice].msb;
      ClockOscillator(m_voices[voice]);
      rose[voice] = !before && m_voices[voice].msb;
    }
    for (std::size_t voice = 0; voice < 3u; ++voice)
    {
      const std::size_t source = (voice + 2u) % 3u;
      if ((m_voices[voice].control & SYNC) != 0 && rose[source])
      {
        m_voices[voice].accumulator = 0;
      }
    }

    for (Voice& voice : m_voices)
    {
      ClockEnvelope(voice);
    }
  }

  void SidSynth::Render(std::int16_t* _out, std::size_t _count) noexcept
  {
    for (std::size_t sample = 0; sample < _count; ++sample)
    {
      // How many chip cycles this sample covers: the clock divided by the rate, with the fraction
      // carried so that the average is exact.
      m_cycleRemainder += m_clockHz;
      const std::uint32_t cycles = m_cycleRemainder / m_sampleRate;
      m_cycleRemainder %= m_sampleRate;

      std::int64_t sum = 0;
      for (std::uint32_t cycle = 0; cycle < cycles; ++cycle)
      {
        Clock();
        sum += VoiceOutput(m_voices[0], m_voices[2]) + VoiceOutput(m_voices[1], m_voices[0]) + VoiceOutput(m_voices[2], m_voices[1]);
      }

      const std::int64_t mixed = (cycles == 0) ? 0 : (sum / static_cast<std::int64_t>(cycles)) * m_volume;
      _out[sample] = static_cast<std::int16_t>(mixed >> OUTPUT_SHIFT);
    }
  }

} // namespace Outpost
