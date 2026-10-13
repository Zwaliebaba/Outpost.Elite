// GameLogic/ToneTimer.h
#pragma once

#include "SquareWave.h"
#include "Timing.h"

#include <cstdint>

namespace Elite
{

class Speaker;

/// The interval timer's channel 2 as the game programs it (ADR-041): the square wave that drives the speaker, whose gate is the
/// system control port's bit 0. Every change of its state reaches the speaker's log (Speaker). Channel 0, the tick, is the
/// scheduler's (Scheduler); channel 1 paces DRAM refresh, which nothing here models.
///
/// Power-on state, as the BIOS leaves it: divisor 0533h, the BIOS beep's 896 Hz, in mode 3, loaded at tick 0, gate low.
class ToneTimer
{
public:
  /// Not noexcept: it tells the speaker channel 2's state, which the speaker logs.
  ToneTimer(const Cycles& _clock, Speaker& _speaker);

  /// OUT 43h,B6h, then _divisor's low and high bytes: channel 2, both bytes of the count, mode 3, as the game's music and
  /// effects program it.
  void Program(std::uint16_t _divisor);

  /// The system control port's bit 0: a rising gate reloads the counter from the count register on the next tick. Not
  /// noexcept: the speaker logs the change.
  void SetGate(bool _high);

  /// The output during the tick containing _cycle. Valid from the channel's last change on.
  [[nodiscard]] bool OutputAt(Cycles _cycle) const noexcept
  {
    return m_wave.OutputHigh(SquareWave::TickAt(_cycle));
  }

  /// The channel's programming and phase.
  [[nodiscard]] const SquareWave& State() const noexcept
  {
    return m_wave;
  }

private:
  const Cycles& m_clock;
  Speaker& m_speaker;
  SquareWave m_wave{};
};

} // namespace Elite
