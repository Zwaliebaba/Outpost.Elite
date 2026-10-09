// Machine/Keyboard.h
#pragma once

#include "PortBus.h"
#include "Timing.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace Machine
{

class Pic;
class Pit;
class Speaker;

/// The IBM PC/XT's 8255 PPI as the keyboard and the speaker see it, at ports 0x60-0x63.
///
/// Port 0x60 (port A) is the keyboard's output latch. Port 0x61 (port B) is an output latch, and a
/// read returns the last value written: bit 0 is PIT channel 2's gate, bit 1 the speaker data, and
/// bit 7 high clears the keyboard latch and holds the keyboard off. Port 0x62 (port C) reads channel
/// 2's output in bit 5, as on the PC, and 0 elsewhere: no configuration switches are modelled, since
/// the BIOS is emulated at the call level and answers INT 11h itself. Port 0x63 is the 8255's mode
/// register: writes are ignored and reads return 0xFF.
///
/// Delivery. The host injects make and break codes into a queue. Whenever the latch is empty, the
/// keyboard is not held and a code is waiting, the next code arrives DELIVERY_DELAY_CYCLES later: it
/// is latched into port 0x60 and raises IRQ1. The latch then stays full, and nothing more arrives,
/// until port 0x61 bit 7 is pulsed: written high, which empties the latch, and low again, which
/// starts the next delivery. That is the XT's acknowledge, and the game's handler (ReadScanCode,
/// 0x7463, Reference-Map.md) does exactly that after reading port 0x60. A read of port 0x60 with
/// the latch empty returns 0, as the XT's cleared shift register does.
///
/// DELIVERY_DELAY_CYCLES is 1 ms, a chosen figure, not a measured one: the XT keyboard sends each
/// code over a serial line, start bit and eight data bits, which takes on the order of a millisecond,
/// and the same order as the game's 1 kHz timer tick keeps IRQ0 and IRQ1 interleaving as they would
/// on the machine. Port 0x61 bit 6, which holds the keyboard clock low, is not modelled.
///
/// Power-on state. Port 0x61 holds 0x4C, which is what the XT BIOS's keyboard reset writes last
/// (keyboard enabled, clock not held, speaker and timer gate off), and the latch is empty.
class Keyboard final : public PortBus
{
public:
  static constexpr std::uint16_t FIRST_PORT = 0x60;
  static constexpr std::uint16_t LAST_PORT = 0x63;
  static constexpr Cycles DELIVERY_DELAY_CYCLES = MicrosecondsToCycles(1'000);
  static constexpr std::uint8_t POWER_ON_PORT_B = 0x4C;

  /// Not noexcept: the queue may allocate, and the speaker logs port 0x61's power-on value.
  Keyboard(const Cycles& _clock, Pic& _pic, Pit& _pit, Speaker& _speaker);

  /// Queues a scan code from the keyboard: a make code, or a break code (make | 0x80).
  void Inject(std::uint8_t _scanCode);

  /// Delivers the next code if its time has come.
  void Advance();

  [[nodiscard]] std::uint8_t In8(std::uint16_t _port) override;
  void Out8(std::uint16_t _port, std::uint8_t _value) override;

  [[nodiscard]] std::size_t QueuedCodes() const noexcept
  {
    return m_queue.size();
  }

  [[nodiscard]] bool LatchFull() const noexcept
  {
    return m_latchFull;
  }

  [[nodiscard]] std::uint8_t PortB() const noexcept
  {
    return m_portB;
  }

private:
  // Starts the next delivery's delay if a code can be delivered and none is under way.
  void Schedule() noexcept;
  void WritePortB(std::uint8_t _value);

  const Cycles& m_clock;
  Pic& m_pic;
  Pit& m_pit;
  Speaker& m_speaker;
  std::deque<std::uint8_t> m_queue;
  std::uint8_t m_portB = POWER_ON_PORT_B;
  std::uint8_t m_latch = 0;
  bool m_latchFull = false;
  bool m_scheduled = false;
  Cycles m_deliverAt = 0;
};

} // namespace Machine
