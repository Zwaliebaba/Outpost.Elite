// GameLogic/Keyboard.h
#pragma once

#include "Timing.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace Elite
{

class InterruptController;
class IntervalTimer;
class Speaker;

/// The IBM PC/XT's keyboard and the side of its 8255 that the game drives (ADR-006 item 2): the keyboard's data latch, port
/// 60h, and the system control port, 61h. It is the emulated PC's model (Machine::Keyboard until D33) without its ports;
/// port 62h and the 8255's mode register, which the game never touches, went with them.
///
/// The system control port is an output latch, and a read returns the last value written: bit 0 is the interval timer's
/// channel 2 gate, bit 1 the speaker's data, and bit 7 high clears the keyboard's latch and holds the keyboard off.
///
/// Delivery. The host injects make and break codes into a queue. Whenever the latch is empty, the keyboard is not held and a
/// code is waiting, the next code arrives DELIVERY_DELAY_CYCLES later: it is latched into port 60h and raises IRQ 1. The latch
/// then stays full, and nothing more arrives, until bit 7 of the system control port is pulsed: written high, which empties
/// the latch, and low again, which starts the next delivery. That is the XT's acknowledge, and the game's handler
/// (ReadScanCode, 0x7443, Reference-Map.md) does exactly that after reading port 60h. A read of port 60h with the latch empty
/// returns 0, as the XT's cleared shift register does.
///
/// DELIVERY_DELAY_CYCLES is 1 ms, a chosen figure, not a measured one: the XT keyboard sends each code over a serial line,
/// start bit and eight data bits, which takes on the order of a millisecond, and the same order as the game's 1 kHz timer
/// tick keeps IRQ 0 and IRQ 1 interleaving as they would on the machine. Bit 6, which holds the keyboard clock low, is not
/// modelled.
///
/// Power-on state. The system control port holds 4Ch, which is what the XT BIOS's keyboard reset writes last (keyboard
/// enabled, clock not held, speaker and timer gate off), and the latch is empty.
class Keyboard
{
public:
  static constexpr Cycles DELIVERY_DELAY_CYCLES = MicrosecondsToCycles(1'000);
  static constexpr std::uint8_t POWER_ON_SYSTEM_CONTROL = 0x4C;

  /// Not noexcept: the queue may allocate, and the speaker logs the system control port's power-on value.
  Keyboard(const Cycles& _clock, InterruptController& _controller, IntervalTimer& _timer, Speaker& _speaker);

  /// Queues a scan code from the keyboard: a make code, or a break code (make | 0x80).
  void Inject(std::uint8_t _scanCode);

  /// Delivers the next code if its time has come.
  void Advance();

  /// The cycle at which the next code is delivered, or NO_EVENT if none is on its way.
  [[nodiscard]] Cycles NextDeliveryAt() const noexcept
  {
    return m_scheduled ? m_deliverAt : NO_EVENT;
  }

  /// IN AL,60h: the latched scan code, or 0 with the latch empty.
  [[nodiscard]] std::uint8_t Data() const noexcept
  {
    return m_latchFull ? m_latch : 0;
  }

  /// IN AL,61h: the system control port, as last written.
  [[nodiscard]] std::uint8_t SystemControl() const noexcept
  {
    return m_systemControl;
  }

  /// OUT 61h,_value: the timer's gate and the speaker take bits 0 and 1, and bit 7 clears and holds the keyboard.
  void SetSystemControl(std::uint8_t _value);

  [[nodiscard]] std::size_t QueuedCodes() const noexcept
  {
    return m_queue.size();
  }

  [[nodiscard]] bool LatchFull() const noexcept
  {
    return m_latchFull;
  }

private:
  // Starts the next delivery's delay if a code can be delivered and none is under way.
  void Schedule() noexcept;

  const Cycles& m_clock;
  InterruptController& m_controller;
  IntervalTimer& m_timer;
  Speaker& m_speaker;
  std::deque<std::uint8_t> m_queue;
  std::uint8_t m_systemControl = POWER_ON_SYSTEM_CONTROL;
  std::uint8_t m_latch = 0;
  bool m_latchFull = false;
  bool m_scheduled = false;
  Cycles m_deliverAt = 0;
};

} // namespace Elite
