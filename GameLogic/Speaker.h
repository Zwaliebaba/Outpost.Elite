// GameLogic/Speaker.h
#pragma once

#include "Timing.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Elite
{

/// The game's sound (ADR-042, ADR-055): a tone and the two bits its sound routines set, logged as they change, and synthesised
/// into PCM on demand. The game's music and effects run from its own state at the tick (TickMusic, TickSoundEffects) and set the
/// tone and the bits; this is what turns those into sound, as the PC's speaker wired them.
///
/// The circuit. The level is the data bit ANDed with the tone, a square wave, whose gate is the gate bit: with the gate set the
/// level is the data bit AND the tone's wave, and with it clear the tone holds high and the level is the data bit alone. Music is
/// the tone at a note's pitch with both bits set; effects toggle the data bit by hand at the tick.
///
/// The tone. The game gives its pitch as the divisor of the PC's timer, 1,193,182 Hz over it (the note tables hold divisors, and a
/// divisor of 0 is 65,536); the tone is a square wave at that frequency, high for its first half period. It starts, high, where
/// it is set and where the gate rises, as the timer reloaded it.
///
/// The log. Every change of the two bits (SetBits) or of the tone (SetTone) appends an event stamped with the clock and carrying
/// the whole state, so the level at any moment is a function of the last event at or before it. Changes within one millisecond
/// collapse into one event. The log only grows; the host renders from it and then drops what it no longer needs (DiscardBefore).
///
/// Rendering. Sample k of a stream at R samples a second covers [k / R, (k + 1) / R) seconds, its bounds computed from whole
/// milliseconds and whole samples, so a stream rendered in pieces never drifts. Each sample is the mean level over its interval (a
/// box filter), the square wave's integral taken exactly, so a wave far above the output's Nyquist frequency averages out rather
/// than aliasing. A box filter does not average out a wave just above or below Nyquist, though: the game's rests, the tone at
/// divisor 32h (about 23.9 kHz), would come out of a 44.1 or 48 kHz stream as a loud tone near 20-24 kHz. So a tone above
/// AUDIBLE_LIMIT_HZ or above Nyquist, which no listener could hear from the real speaker, contributes its mean level instead of
/// its shape. A rest is therefore a steady level, and silent.
///
/// Level 0 (no current through the coil) is sample 0, and level 1 is FULL_LEVEL. There is no DC filter here: the steady half
/// level of a rest is a displaced cone, which makes no sound.
class Speaker
{
public:
  static constexpr std::int16_t FULL_LEVEL = 16'384;
  static constexpr std::uint32_t AUDIBLE_LIMIT_HZ = 20'000;

  /// The PC timer's input clock, which the game's divisors divide: 14.31818 MHz over 12.
  static constexpr double TIMER_HZ = 14'318'180.0 / 12.0;

  /// The state from one millisecond until the next event.
  struct Event
  {
    Milliseconds at = 0;
    bool gate = false;          ///< the tone runs
    bool data = false;          ///< the speaker's data bit
    double toneHz = 0;          ///< the tone's frequency
    Milliseconds toneStart = 0; ///< where the tone's wave starts, high
  };

  /// Starts with both bits clear and the tone at divisor 0533h, the BIOS beep's 896 Hz, as the PC is left. Not noexcept: the log
  /// allocates.
  explicit Speaker(const Milliseconds& _clock);

  /// The gate and data bits: a rising gate starts the tone again.
  void SetBits(bool _gate, bool _data);

  /// The tone at _divisor of the timer's clock (0 is 65,536), which starts again.
  void SetTone(std::uint16_t _divisor);

  /// The frequency of the tone at _divisor.
  [[nodiscard]] static double ToneHz(std::uint16_t _divisor) noexcept;

  /// The state now: the last event's.
  [[nodiscard]] const Event& Now() const noexcept
  {
    return m_events.back();
  }

  /// The number of samples RenderSamples produces for the same arguments: those whose interval ends in (_from, _to].
  [[nodiscard]] static std::size_t SampleCount(Milliseconds _from, Milliseconds _to, std::uint32_t _sampleRate) noexcept;

  /// The millisecond the first sample that RenderSamples(_from, ...) renders starts in. It can be before _from, since that sample
  /// is the one whose interval ends just after _from.
  [[nodiscard]] static Milliseconds FirstSampleStart(Milliseconds _from, std::uint32_t _sampleRate) noexcept;

  /// Renders the samples whose interval ends in (_from, _to] into _out, in order, and returns how many it wrote: SampleCount's
  /// answer, or _out.size() if that is smaller. Rendering (a, b] and then (b, c] gives exactly the samples of rendering (a, c].
  /// The log must still hold the event in force at FirstSampleStart(_from), and the samples are only final once the clock has
  /// reached _to.
  std::size_t RenderSamples(Milliseconds _from, Milliseconds _to, std::uint32_t _sampleRate, std::span<std::int16_t> _out) const;

  /// Drops the events nothing from _time on depends on, keeping the one in force at _time. A host that has rendered up to b
  /// calls DiscardBefore(FirstSampleStart(b, rate)).
  void DiscardBefore(Milliseconds _time);

  [[nodiscard]] std::span<const Event> Events() const noexcept
  {
    return m_events;
  }

private:
  void Append(const Event& _event);

  const Milliseconds& m_clock;
  std::vector<Event> m_events;
};

} // namespace Elite
