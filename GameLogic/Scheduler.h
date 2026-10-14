// GameLogic/Scheduler.h
#pragma once

#include "KeyboardState.h"
#include "Timing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace Elite
{

/// The two interrupts the game takes (ADR-041): its tick's, and the keyboard's.
enum class Irq : std::uint8_t
{
  Timer,
  Keyboard
};

/// What serves an interrupt: the game's own handler, or none of the game's, when the host's default runs (Platform); a stray
/// one throws std::logic_error.
enum class IrqHandler : std::uint8_t
{
  Bios,
  Game,
  Stray
};

/// What time waits for and the two interrupts the game takes (D87, D88): the tick, the keys, and the requests they raise.
///
/// The tick. While it runs (SetTickRunning), the timer's interrupt is requested at every whole millisecond after it started.
///
/// The keys. The host queues key events (Inject). Whenever the latch is empty and an event is waiting, the next whole
/// millisecond delivers it: it is latched, and requests the keyboard's interrupt. The latch stays full until the handler takes
/// the event (TakeKey), so one event arrives a millisecond at most, in order (D88).
///
/// The requests. Each of the two is a latch: a second while one is pending is not queued. One is granted when nothing of its
/// priority or higher is in service, the timer's ranking above the keyboard's; taking it (Take) puts it in service, and the end
/// of interrupt ends the highest in service.
class Scheduler
{
public:
  /// Not noexcept: the keys' queue may allocate when it is made, as MSVC's debug deque does.
  explicit Scheduler(const Milliseconds& _clock);

  // ── The tick ──

  /// The tick started, from the next whole millisecond on, or stopped.
  void SetTickRunning(bool _running) noexcept;

  [[nodiscard]] bool TickRunning() const noexcept
  {
    return m_nextTickAt != NO_EVENT;
  }

  /// When the next tick requests the timer's interrupt, or NO_EVENT.
  [[nodiscard]] Milliseconds NextTickAt() const noexcept
  {
    return m_nextTickAt;
  }

  // ── The keys ──

  /// Queues a key event from the host.
  void Inject(const KeyEvent& _event);

  /// When the next event is delivered, or NO_EVENT if none is on its way.
  [[nodiscard]] Milliseconds NextDeliveryAt() const noexcept
  {
    return m_scheduled ? m_deliverAt : NO_EVENT;
  }

  [[nodiscard]] bool LatchFull() const noexcept
  {
    return m_latchFull;
  }

  /// The latched event, the latch emptied, and the next delivery started if one is waiting. Only with the latch full.
  [[nodiscard]] KeyEvent TakeKey() noexcept;

  [[nodiscard]] std::size_t QueuedEvents() const noexcept
  {
    return m_queue.size();
  }

  // ── The requests ──

  /// What falls due by the clock: the tick's request if its time has come, and the next event if its has.
  void Advance() noexcept;

  /// The next millisecond at which something falls due, or NO_EVENT.
  [[nodiscard]] Milliseconds NextEventAt() const noexcept;

  /// Whether a request would be granted now.
  [[nodiscard]] bool InterruptPending() const noexcept;

  /// The request granted now, put in service. Only when one is pending.
  [[nodiscard]] Irq Take() noexcept;

  /// The end of interrupt: the highest in service ends.
  void EndOfInterrupt() noexcept;

  [[nodiscard]] bool Requested(Irq _irq) const noexcept
  {
    return m_requested[Index(_irq)];
  }

  [[nodiscard]] bool InService(Irq _irq) const noexcept
  {
    return m_inService[Index(_irq)];
  }

  /// A request, as a device raises it. A test's way in; the tick and the keys raise their own.
  void Request(Irq _irq) noexcept
  {
    m_requested[Index(_irq)] = true;
  }

private:
  [[nodiscard]] static constexpr std::size_t Index(Irq _irq) noexcept
  {
    return static_cast<std::size_t>(_irq);
  }

  // Starts the next delivery if an event is waiting, the latch is empty and none is under way.
  void ScheduleDelivery() noexcept;

  const Milliseconds& m_clock;

  Milliseconds m_nextTickAt = NO_EVENT;

  std::deque<KeyEvent> m_queue;
  KeyEvent m_latch{};
  bool m_latchFull = false;
  bool m_scheduled = false;
  Milliseconds m_deliverAt = 0;

  std::array<bool, 2> m_requested{};
  std::array<bool, 2> m_inService{};
};

} // namespace Elite
