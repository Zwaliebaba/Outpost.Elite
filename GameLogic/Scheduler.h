// GameLogic/Scheduler.h
#pragma once

#include "SquareWave.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace Elite
{

/// The two hardware interrupts the game takes (ADR-041): the timer's, IRQ 0 on int 8, and the keyboard's, IRQ 1 on int 9.
enum class Irq : std::uint8_t
{
  Timer,
  Keyboard
};

/// What serves an interrupt: the BIOS's handler, the game's own, or a vector that names code no routine stands in for, as int 8
/// does once the game writes its segment again while the BIOS's handler is there; one taken there throws std::logic_error.
enum class IrqHandler : std::uint8_t
{
  Bios,
  Game,
  Stray
};

/// What paced time waits for and the two interrupts the game takes (ADR-041): the timer's tick, the keyboard's codes, and the
/// requests they raise. It stands where the PC's 8259, the 8253's channel 0 and the XT keyboard's latch stood, with their rules
/// as the game met them, so that every interrupt lands on the cycle it did.
///
/// The tick. Channel 0 of the interval timer in mode 3 (SquareWave), its gate tied high: its output's rising edge requests the
/// timer's interrupt. At power-on, as the BIOS leaves it, its divisor is 0, which is 65,536, so the tick is about 18.2 Hz. The
/// game sets its own divisor (SetTickDivisor) and puts 0 back. Programming it sets a low output high at once, and that is an
/// edge. A move of the clock that spans several rising edges requests the interrupt once, as the edges coalesced in the 8259's
/// request register.
///
/// The keys. The host injects make and break codes into a queue. Whenever the latch is empty, the keyboard is not held and a
/// code is waiting, the next code arrives DELIVERY_DELAY_CYCLES later: it is latched, and requests the keyboard's interrupt. The
/// latch then stays full, and nothing more arrives, until the keyboard is held, which empties the latch, and let go, which
/// starts the next delivery: the XT's acknowledge, bit 7 of the system control port pulsed, which the game's handler
/// (ReadScanCode, 0x7443) makes after reading the code. The latch read empty is 0, as the XT's cleared shift register was.
/// DELIVERY_DELAY_CYCLES is 1 ms, a chosen figure, not a measured one: the XT sends each code over a serial line in about that,
/// and the same order as the game's 1 kHz tick keeps the two interrupts interleaving as they would on the machine.
///
/// The requests. Each of the two is a latch: a second while one is pending is not queued. One is granted when nothing of its
/// priority or higher is in service, the timer's ranking above the keyboard's, as the 8259 nests them; taking it (Take) puts it
/// in service, and the end of interrupt, OUT 20h,20h, ends the highest in service.
class Scheduler
{
public:
  static constexpr Cycles DELIVERY_DELAY_CYCLES = MicrosecondsToCycles(1'000);

  explicit Scheduler(const Cycles& _clock) noexcept;

  // ── The tick ──

  /// OUT 43h,36h, then _divisor's low and high bytes: channel 0, both bytes of the count, mode 3. A low output set high by the
  /// control word requests the timer's interrupt.
  void SetTickDivisor(std::uint16_t _divisor) noexcept;

  /// The cycle at which the next tick requests the timer's interrupt, or NO_EVENT.
  [[nodiscard]] Cycles NextTickAt() const noexcept;

  /// Channel 0's output during the tick containing _cycle. Valid from its last change on.
  [[nodiscard]] bool TickOutputAt(Cycles _cycle) const noexcept
  {
    return m_tick.OutputHigh(SquareWave::TickAt(_cycle));
  }

  // ── The keys ──

  /// Queues a scan code from the keyboard: a make code, or a break code (make | 0x80).
  void Inject(std::uint8_t _scanCode);

  /// The cycle at which the next code is delivered, or NO_EVENT if none is on its way.
  [[nodiscard]] Cycles NextDeliveryAt() const noexcept
  {
    return m_scheduled ? m_deliverAt : NO_EVENT;
  }

  /// IN AL,60h: the latched scan code, or 0 with the latch empty.
  [[nodiscard]] std::uint8_t ScanCode() const noexcept
  {
    return m_latchFull ? m_latch : 0;
  }

  [[nodiscard]] bool LatchFull() const noexcept
  {
    return m_latchFull;
  }

  [[nodiscard]] std::size_t QueuedCodes() const noexcept
  {
    return m_queue.size();
  }

  /// The system control port's bit 7 as written: held, the latch empties and nothing is delivered; let go, the next delivery
  /// starts if one can.
  void HoldKeyboard(bool _held) noexcept;

  // ── The requests ──

  /// What falls due by the clock: the tick's request if its output rose since the last call, and the next code if its time has
  /// come.
  void Advance() noexcept;

  /// The next cycle at which something falls due, or NO_EVENT.
  [[nodiscard]] Cycles NextEventAt() const noexcept;

  /// Whether a request would be granted now.
  [[nodiscard]] bool InterruptPending() const noexcept;

  /// The request granted now, put in service. Only when one is pending.
  [[nodiscard]] Irq Take() noexcept;

  /// The non-specific end of interrupt: the highest in service ends.
  void EndOfInterrupt() noexcept;

  [[nodiscard]] bool Requested(Irq _irq) const noexcept
  {
    return m_requested[Index(_irq)];
  }

  [[nodiscard]] bool InService(Irq _irq) const noexcept
  {
    return m_inService[Index(_irq)];
  }

  /// A request, as a device's edge raises it. A test's way in; the tick and the keys raise their own.
  void Request(Irq _irq) noexcept
  {
    m_requested[Index(_irq)] = true;
  }

private:
  [[nodiscard]] static constexpr std::size_t Index(Irq _irq) noexcept
  {
    return static_cast<std::size_t>(_irq);
  }

  [[nodiscard]] std::int64_t Now() const noexcept
  {
    return SquareWave::TickAt(m_clock);
  }

  // Starts the next delivery's delay if a code can be delivered and none is under way.
  void ScheduleDelivery() noexcept;

  const Cycles& m_clock;

  SquareWave m_tick{};
  std::int64_t m_nextEdgeTick = SquareWave::NEVER; // the tick's next rising edge after the last Advance()

  std::deque<std::uint8_t> m_queue;
  std::uint8_t m_latch = 0;
  bool m_latchFull = false;
  bool m_held = false;
  bool m_scheduled = false;
  Cycles m_deliverAt = 0;

  std::array<bool, 2> m_requested{};
  std::array<bool, 2> m_inService{};
};

} // namespace Elite
