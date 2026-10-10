// Machine/Pit.h
#pragma once

#include "PortBus.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Machine
{

class Pic;
class Speaker;

/// The 8253 programmable interval timer as the IBM PC wires it, at ports 0x40-0x43. It is clocked
/// at a twelfth of the crystal, one tick every CYCLES_PER_PIT_TICK CPU cycles. Tick k happens at
/// cycle k * CYCLES_PER_PIT_TICK, counted from power-on, so the number of ticks by a cycle is a
/// division of the machine's own clock: sub-tick remainders are carried exactly, however unevenly
/// the clock advances.
///
/// The wiring. Channel 0's gate is tied high and its output's rising edge is IRQ0. Channel 1's gate
/// is tied high and it paces DRAM refresh, which nothing here models; it is programmable and
/// readable all the same. Channel 2's gate is port 0x61 bit 0 (SetChannel2Gate, from the PPI), and
/// its output goes to the speaker, which logs every change of channel 2's state (Speaker).
///
/// Power-on state, as the PC BIOS leaves it (there is no BIOS ROM here to program it, ADR-005):
/// channel 0 in mode 3 with divisor 0, which is 65536, so IRQ0 at about 18.2 Hz; channel 1 in mode 2
/// with divisor 18, LSB only, the refresh rate; channel 2 in mode 3 with divisor 0x0533, the BIOS
/// beep's 896 Hz, gate low. All three counters were loaded at tick 0.
///
/// Modes. 0 (interrupt on terminal count), 2 (rate generator) and 3 (square wave) are modelled,
/// with the gate, and with LSB, MSB and LSB-then-MSB access. The write and read byte sequences are
/// independent, as on the part. A count is loaded into the counting element on the tick after it
/// is written. In modes 2 and 3 a count written while the counter runs takes effect at the end of
/// the current period (mode 2) or half-period (mode 3); a control word stops the counter until a
/// count is written. Mode 3 with an odd count N is high for (N+1)/2 ticks and low for (N-1)/2. A
/// counter-latch command freezes the count for reading until it has been read in full; without
/// one, a read returns the live count. Modes 1, 4 and 5 count down as mode 0 does but their output
/// stays high; BCD counting (control word bit 0) counts in binary; in mode 0, writing the first byte
/// of a two-byte count does not stop the counter; and the 8254's read-back command (SC = 11) is
/// ignored, as the 8253 has none. Nothing on this machine depends on any of these.
///
/// Time. The PIT reads the clock on every port access and in Advance(), which the integrator calls
/// after every CPU step and which raises IRQ0 if channel 0's output rose since the last call. A
/// step that spans several rising edges raises IRQ0 once, as the edges would have coalesced in the
/// PIC's request register anyway.
class Pit final : public PortBus
{
public:
  static constexpr std::uint16_t FIRST_PORT = 0x40;
  static constexpr std::uint16_t LAST_PORT = 0x43;
  static constexpr std::size_t CHANNEL_COUNT = 3;

  /// A tick that never comes.
  static constexpr std::int64_t NEVER = std::numeric_limits<std::int64_t>::max();

  /// One counter's programming and phase: what its output and count are at any tick from now on,
  /// computed directly rather than by simulating the ticks in between. A plain aggregate (R8): the
  /// PIT keeps one per channel, and the speaker logs copies of channel 2's.
  struct Channel
  {
    std::uint8_t mode = 3;
    std::uint8_t access = 3;         // the control word's RW field: 1 LSB, 2 MSB, 3 LSB then MSB
    bool loaded = false;             // a count has been written since the last control word
    bool gate = true;                // the gate input
    std::uint32_t reload = 0;        // the count the counting element runs with, 1-65536
    std::int64_t originTick = 0;     // the tick `reload` was loaded on: phase zero, output high (modes 2, 3)
    std::int64_t pausedTick = 0;     // with the gate low: the tick it went low on
    std::int64_t switchTick = NEVER; // modes 2, 3: when a count written while running takes over
    std::uint32_t nextReload = 0;
    std::int64_t nextOriginTick = 0;
    std::uint16_t heldCount = 0; // what a read returns before `reload` is loaded

    /// Whether the output is high during tick _tick, the interval from that tick to the next.
    [[nodiscard]] bool OutputHigh(std::int64_t _tick) const noexcept;

    /// The counting element's value during tick _tick, as a read would return it.
    [[nodiscard]] std::uint16_t Count(std::int64_t _tick) const noexcept;

    /// The first tick after _tick on which the output goes from low to high, or NEVER.
    [[nodiscard]] std::int64_t NextRisingEdge(std::int64_t _tick) const noexcept;

    /// The time-integral of the output (1 high, 0 low) over [_from, _to), in a time base of
    /// _unitsPerTick units to the tick, with tick t covering [t * _unitsPerTick, (t + 1) *
    /// _unitsPerTick). A mode 2 or 3 wave whose period is shorter than _averageBelowTicks ticks
    /// counts as its mean level instead of its shape; 0 means always exact.
    [[nodiscard]] double Integral(std::uint64_t _from, std::uint64_t _to, std::uint64_t _unitsPerTick,
                                  std::uint32_t _averageBelowTicks) const noexcept;

    /// The count register: the last complete count written, 1-65536, or 0 if none has been.
    [[nodiscard]] std::uint32_t CountRegister() const noexcept;
  };

  /// Not noexcept: it tells the speaker channel 2's state, which the speaker logs.
  Pit(const Cycles& _clock, Pic& _pic, Speaker& _speaker);

  [[nodiscard]] std::uint8_t In8(std::uint16_t _port) noexcept override;
  void Out8(std::uint16_t _port, std::uint8_t _value) override;

  /// Raises IRQ0 if channel 0's output rose since the last call.
  void Advance() noexcept;

  /// The cycle at which the next IRQ0 is due: channel 0's next rising edge after the last Advance(),
  /// or NO_EVENT if its output will not rise again as it is programmed.
  [[nodiscard]] Cycles NextInterruptAt() const noexcept;

  /// Port 0x61 bit 0. Not noexcept: the speaker logs the change.
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
  // The byte sequencing of one channel's port.
  struct Access
  {
    bool writeHigh = false; // the next write is the MSB of an LSB-then-MSB count
    std::uint8_t lowByte = 0;
    bool readHigh = false; // the next read is the MSB of an LSB-then-MSB count
    bool latched = false;
    std::uint16_t latch = 0;
  };

  [[nodiscard]] std::int64_t Now() const noexcept;
  void WriteControl(std::uint8_t _value);
  void WriteCount(std::size_t _channel, std::uint8_t _value);
  void LoadCount(std::size_t _channel, std::uint32_t _count);
  [[nodiscard]] std::uint8_t ReadCount(std::size_t _channel) noexcept;
  // Raises IRQ0 for a rising edge caused by a port write rather than by a tick.
  void CheckWriteEdge(std::size_t _channel, bool _wasHigh) noexcept;
  void Changed(std::size_t _channel);

  const Cycles& m_clock;
  Pic& m_pic;
  Speaker& m_speaker;
  std::array<Channel, CHANNEL_COUNT> m_channels{};
  std::array<Access, CHANNEL_COUNT> m_access{};
  std::int64_t m_nextEdgeTick = NEVER; // channel 0's next rising edge after the last Advance()
};

} // namespace Machine
