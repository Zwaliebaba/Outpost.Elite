#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Outpost
{

  /*
   * A software 6581 (ADR-005 section 2).
   *
   * WRITTEN HERE RATHER THAN TAKEN, because the emulator cores are GPL and the house rules ban them
   * (AGENTS.md section 5). It is the chip as its data sheet and thirty years of documentation
   * describe it: three voices, each a 24-bit phase accumulator driving four waveforms -- triangle,
   * sawtooth, pulse with a 12-bit width, and noise from a 23-bit shift register clocked off the
   * accumulator's bit 19 -- through an envelope generator with the chip's rate table and its
   * piecewise-exponential decay. Hard sync and ring modulation are in because they cost nothing.
   * The filter is NOT, which the ADR allows: Elite's effects never enable it, and the music's one
   * filter command is honoured for the volume nibble it carries and nothing else.
   *
   * IT IS CLOCKED PER CYCLE AND AVERAGED PER SAMPLE. The chip runs at the 6510's clock, a little
   * over a million ticks a second, and rendering it at that rate and box-filtering down to the
   * output rate is both the simplest anti-aliasing there is and the one that costs the least to
   * reason about: no oscillator maths in the sample domain, no band-limiting tricks, just the chip
   * doing what it does twenty-three times per sample. Three voices at a million ticks is nothing to
   * a machine that can run this port at all.
   *
   * Everything is integer. Not because the rule that binds `GameLogic` reaches here -- it does not
   * -- but because the chip is integer, and a model that is integer can be read against the chip.
   */
  class SidSynth
  {
  public:
    /// The chip's registers: three voices of seven, then the filter and volume.
    static constexpr std::uint8_t REGISTER_COUNT = 0x19;

    /// `_clockHz` is the 6510's clock the chip shares; `_sampleRate` is what comes out.
    SidSynth(std::uint32_t _clockHz, std::uint32_t _sampleRate) noexcept;

    /// A register write, as the game's interrupt handler makes it. Registers beyond the chip's
    /// are ignored, as the chip ignores them.
    void Write(std::uint8_t _register, std::uint8_t _value) noexcept;

    /// Render `_count` samples of signed 16-bit mono into `_out`, advancing the chip by the cycles
    /// those samples cover.
    void Render(std::int16_t* _out, std::size_t _count) noexcept;

    /// Every register to zero and every oscillator and envelope to rest -- what the chip does at
    /// power-on, and what `stopat` does to it by hand.
    void Reset() noexcept;

  private:
    enum class Phase : std::uint8_t
    {
      Attack,
      DecaySustain,
      Release,
    };

    struct Voice
    {
      // ---- the registers ------------------------------------------------------------------------
      std::uint16_t frequency = 0;     ///< registers 0 and 1
      std::uint16_t pulseWidth = 0;    ///< registers 2 and 3, twelve bits
      std::uint8_t control = 0;        ///< register 4
      std::uint8_t attackDecay = 0;    ///< register 5
      std::uint8_t sustainRelease = 0; ///< register 6

      // ---- the oscillator -----------------------------------------------------------------------
      std::uint32_t accumulator = 0;          ///< 24 bits
      std::uint32_t shiftRegister = 0x7FFFF8; ///< the noise generator's 23 bits, at their reset value
      bool bit19 = false;                     ///< the accumulator's bit 19 last cycle, for the noise clock
      bool msb = false;                       ///< its bit 23 last cycle, for sync

      // ---- the envelope -------------------------------------------------------------------------
      Phase phase = Phase::Release;
      bool gate = false;
      std::uint8_t envelope = 0; ///< the 8-bit level
      std::uint16_t rateCounter = 0;
      std::uint8_t exponentialCounter = 0;
      bool holdAtZero = true; ///< the counter has hit zero in release and stays there until a gate
    };

    void Clock() noexcept;
    void ClockOscillator(Voice& _voice) noexcept;
    void ClockEnvelope(Voice& _voice) noexcept;
    [[nodiscard]] std::int32_t VoiceOutput(const Voice& _voice, const Voice& _ringSource) const noexcept;

    std::array<Voice, 3> m_voices{};
    std::uint8_t m_volume = 0; ///< register &18's low nibble

    std::uint32_t m_clockHz;
    std::uint32_t m_sampleRate;
    std::uint32_t m_cycleRemainder = 0; ///< the fraction of a cycle carried between samples
  };

} // namespace Outpost
