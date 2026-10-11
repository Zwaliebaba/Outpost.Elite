// GameLogic/IntervalTimer.h
#pragma once

#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Elite
{

class InterruptController;
class Speaker;

/// The IBM PC's 8253 interval timer as the game programs it (ADR-006 item 2), clocked at a twelfth of the crystal: one tick
/// every CYCLES_PER_PIT_TICK cycles. Tick k happens at cycle k * CYCLES_PER_PIT_TICK, counted from power-on, so the ticks by
/// a cycle are a division of the platform's own clock and nothing drifts. It is the emulated PC's model (Machine::Pit until
/// D33) without its ports: the game writes a control word and both bytes of a count, and reads nothing back, so the other
/// access modes, the latch and the count reads went with them.
///
/// The wiring. Channel 0's gate is tied high, and its output's rising edge is IRQ 0, the game's tick. Channel 1 paces DRAM
/// refresh, which nothing here models. Channel 2's gate is the system control port's bit 0 (SetChannel2Gate, from the
/// keyboard's 8255), and its output drives the speaker, which logs every change of channel 2's state (Speaker).
///
/// Power-on state, as the BIOS leaves it: channel 0 in mode 3 with divisor 0, which is 65,536, so IRQ 0 at about 18.2 Hz;
/// channel 1 in mode 2 with divisor 18; channel 2 in mode 3 with divisor 0533h, the BIOS beep's 896 Hz, gate low. All three
/// were loaded at tick 0.
///
/// Modes 0 (interrupt on terminal count), 2 (rate generator) and 3 (square wave) are modelled, with the gate. A count is
/// loaded into the counting element on the tick after it is written. In modes 2 and 3 a count written while the counter runs
/// takes effect at the end of the current period (mode 2) or half-period (mode 3); a control word stops the counter until a
/// count is written, which Program does at once. Mode 3 with an odd count N is high for (N+1)/2 ticks and low for (N-1)/2.
/// Modes 1, 4 and 5 count but their output stays high, and BCD counting counts in binary; nothing the game does uses them.
///
/// Time. The timer reads the clock when it is programmed and in Advance(), which the platform calls whenever its clock
/// moves, and which raises IRQ 0 if channel 0's output rose since the last call. A move that spans several rising edges
/// raises IRQ 0 once, as the edges would have coalesced in the controller's request register anyway.
class IntervalTimer
{
public:
  static constexpr std::size_t CHANNEL_COUNT = 3;

  /// A tick that never comes.
  static constexpr std::int64_t NEVER = std::numeric_limits<std::int64_t>::max();

  /// One counter's programming and phase: what its output is at any tick from now on, computed directly rather than by
  /// simulating the ticks in between. A plain aggregate (R8): the timer keeps one per channel, and the speaker logs copies of
  /// channel 2's.
  struct Channel
  {
    std::uint8_t mode = 3;
    bool loaded = false;             // a count has been written since the last control word
    bool gate = true;                // the gate input
    std::uint32_t reload = 0;        // the count the counting element runs with, 1-65536
    std::int64_t originTick = 0;     // the tick `reload` was loaded on: phase zero, output high (modes 2, 3)
    std::int64_t pausedTick = 0;     // with the gate low: the tick it went low on
    std::int64_t switchTick = NEVER; // modes 2, 3: when a count written while running takes over
    std::uint32_t nextReload = 0;
    std::int64_t nextOriginTick = 0;

    /// Whether the output is high during tick _tick, the interval from that tick to the next.
    [[nodiscard]] bool OutputHigh(std::int64_t _tick) const noexcept;

    /// The first tick after _tick on which the output goes from low to high, or NEVER.
    [[nodiscard]] std::int64_t NextRisingEdge(std::int64_t _tick) const noexcept;

    /// The time-integral of the output (1 high, 0 low) over [_from, _to), in a time base of _unitsPerTick units to the tick,
    /// with tick t covering [t * _unitsPerTick, (t + 1) * _unitsPerTick). A mode 2 or 3 wave whose period is shorter than
    /// _averageBelowTicks ticks counts as its mean level instead of its shape; 0 means always exact.
    [[nodiscard]] double Integral(std::uint64_t _from, std::uint64_t _to, std::uint64_t _unitsPerTick,
                                  std::uint32_t _averageBelowTicks) const noexcept;

    /// The count register: the last count written, 1-65536, or 0 if none has been.
    [[nodiscard]] std::uint32_t CountRegister() const noexcept;
  };

  /// Not noexcept: it tells the speaker channel 2's state, which the speaker logs.
  IntervalTimer(const Cycles& _clock, InterruptController& _controller, Speaker& _speaker);

  /// OUT 43h,_control, then _divisor's low and high bytes to the channel _control selects: a control word for both bytes of a
  /// count (bits 5-4 = 11), its mode in bits 3-1, then the count. A control word that sets channel 0's low output high raises
  /// IRQ 0 at once. Throws std::invalid_argument for any other access, or for the 8254's read-back, which the game never
  /// writes.
  void Program(std::uint8_t _control, std::uint16_t _divisor);

  /// Raises IRQ 0 if channel 0's output rose since the last call.
  void Advance() noexcept;

  /// The cycle at which the next IRQ 0 is due: channel 0's next rising edge after the last Advance(), or NO_EVENT if its
  /// output will not rise again as it is programmed.
  [[nodiscard]] Cycles NextInterruptAt() const noexcept;

  /// The system control port's bit 0. Not noexcept: the speaker logs the change.
  void SetChannel2Gate(bool _high);

  /// A channel's output during the tick containing _cycle. Valid from the channel's last change on.
  [[nodiscard]] bool OutputAt(std::size_t _channel, Cycles _cycle) const noexcept;

  /// A channel's programming and phase.
  [[nodiscard]] const Channel& ChannelState(std::size_t _channel) const noexcept
  {
    return m_channels[_channel];
  }

  /// The number of the tick in progress at _cycle.
  [[nodiscard]] static constexpr std::int64_t TickAt(Cycles _cycle) noexcept
  {
    return static_cast<std::int64_t>(_cycle / CYCLES_PER_PIT_TICK);
  }

private:
  [[nodiscard]] std::int64_t Now() const noexcept;
  void WriteControl(std::size_t _channel, std::uint8_t _mode);
  void LoadCount(std::size_t _channel, std::uint32_t _count);
  // Raises IRQ 0 for a rising edge caused by programming rather than by a tick.
  void CheckWriteEdge(std::size_t _channel, bool _wasHigh) noexcept;
  void Changed(std::size_t _channel);

  const Cycles& m_clock;
  InterruptController& m_controller;
  Speaker& m_speaker;
  std::array<Channel, CHANNEL_COUNT> m_channels{};
  std::int64_t m_nextEdgeTick = NEVER; // channel 0's next rising edge after the last Advance()
};

} // namespace Elite
