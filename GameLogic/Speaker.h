// GameLogic/Speaker.h
#pragma once

#include "SquareWave.h"
#include "Timing.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Elite
{

/// The game's sound (ADR-042): the tone and the two bits its sound routines set, logged as they change, and synthesised into PCM
/// on demand. The game's music and effects run from its own state at the 1 kHz tick (TickMusic, TickSoundEffects) and set the
/// tone and the bits; this is what turns those into sound, the PC speaker as its circuit wired them.
///
/// The circuit. The data bit (port 61h bit 1) is ANDed with the tone, the interval timer's channel 2 in mode 3 (SquareWave),
/// whose gate is the gate bit (port 61h bit 0). So with the gate set the level is the data bit AND the tone's wave, and with it
/// clear the tone sits high and the level is the data bit alone. The game uses both (Reference-Map.md, "What the interrupt
/// handlers share"): music is the tone at a note's divisor with both bits set, and effects toggle the data bit by hand at the
/// 1 kHz tick. A rising gate reloads the tone on the next timer tick.
///
/// The log. Every change of the two bits (SetBits) or of the tone (SetTone) appends an event stamped with the platform's clock
/// and carrying the whole state, so the level at any moment is a function of the last event at or before it. Changes
/// within one millisecond collapse into one event. The log only grows; the host renders from it and then
/// drops what it no longer needs with DiscardBefore.
///
/// Time. The platform's clock is milliseconds (D87); the tone runs on the interval timer's own clock, the IBM PC's, which the
/// speaker keeps privately: a millisecond starts at the timer tick it falls in.
///
/// Rendering. Sample k of a stream at R samples a second covers [k / R, (k + 1) / R) seconds, on the 8088's cycle clock as
/// an exact fraction, so a stream rendered in pieces never
/// drifts. Each sample is the mean level over its interval (a box filter), not the level at one
/// instant, so a wave far above the output's Nyquist frequency averages out rather than aliasing.
/// A box filter does not average out a wave just above or below Nyquist, though: the game's rests,
/// the tone at divisor 0x32 (about 23.9 kHz), would come out of a 44.1 or 48 kHz stream as a loud
/// tone near 20-24 kHz. So a tone whose fundamental is above AUDIBLE_LIMIT_HZ or
/// above Nyquist, which no listener could hear from the real speaker, contributes its mean level
/// instead of its shape. A rest is therefore a steady level, and silent.
///
/// Level 0 (no current through the coil) is sample 0, and level 1 is FULL_LEVEL. There is no DC
/// filter here: the steady half level of a rest is a displaced cone, which makes no sound.
class Speaker
{
public:
  static constexpr std::int16_t FULL_LEVEL = 16'384;
  static constexpr std::uint32_t AUDIBLE_LIMIT_HZ = 20'000;

  /// The state from one millisecond until the next event.
  struct Event
  {
    Milliseconds at = 0;
    bool gate = false; // port 0x61 bit 0
    bool data = false; // port 0x61 bit 1
    SquareWave tone;   // the timer's channel 2
  };

  /// Starts as the BIOS leaves the PC: both bits clear, and the tone at divisor 0533h, the BIOS beep's 896 Hz, in mode 3,
  /// loaded at tick 0, its gate low. Not noexcept: the log allocates.
  explicit Speaker(const Milliseconds& _clock);

  /// The gate and data bits, port 61h's bits 0 and 1 as written: a rising gate reloads the tone from its count on the next
  /// timer tick.
  void SetBits(bool _gate, bool _data);

  /// OUT 43h,B6h, then _divisor's low and high bytes: the tone, the timer's channel 2 in mode 3, as the game's music and effects
  /// set it, loaded on the next timer tick.
  void SetTone(std::uint16_t _divisor);

  /// The tone's output during the timer tick _time starts in. Valid from the last change on.
  [[nodiscard]] bool ToneOutputAt(Milliseconds _time) const noexcept;

  /// The state now: the last event's.
  [[nodiscard]] const Event& Now() const noexcept
  {
    return m_events.back();
  }

  /// The number of samples RenderSamples produces for the same arguments: those whose interval
  /// ends in (_from, _to].
  [[nodiscard]] static std::size_t SampleCount(Milliseconds _from, Milliseconds _to, std::uint32_t _sampleRate) noexcept;

  /// The millisecond the first sample that RenderSamples(_from, ...) renders starts in. It can be before
  /// _from, since that sample is the one whose interval ends just after _from.
  [[nodiscard]] static Milliseconds FirstSampleStart(Milliseconds _from, std::uint32_t _sampleRate) noexcept;

  /// Renders the samples whose interval ends in (_from, _to] into _out, in order, and returns how
  /// many it wrote: SampleCount's answer, or _out.size() if that is smaller. Rendering (a, b] and
  /// then (b, c] gives exactly the samples of rendering (a, c]. The log must still hold the event in
  /// force at FirstSampleStart(_from), and the samples are only final once the clock has reached
  /// _to. Exact for runs shorter than 2^64 / (3 * _sampleRate) cycles, about 300 days at 48 kHz.
  std::size_t RenderSamples(Milliseconds _from, Milliseconds _to, std::uint32_t _sampleRate, std::span<std::int16_t> _out) const;

  /// Drops the events nothing from _time on depends on, keeping the one in force at _time. A
  /// host that has rendered up to b calls DiscardBefore(FirstSampleStart(b, rate)).
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
