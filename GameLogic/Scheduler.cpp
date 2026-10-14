#include "pch.h"

#include "Scheduler.h"

#include <algorithm>

namespace Elite
{

Scheduler::Scheduler(const Cycles& _clock) noexcept
  : m_clock(_clock)
{
  m_tick.loaded = true;
  m_tick.reload = SquareWave::FULL_COUNT;
  m_nextEdgeTick = m_tick.NextRisingEdge(Now());
}

// ── The tick ──

void Scheduler::SetTickDivisor(std::uint16_t _divisor) noexcept
{
  // The edges up to now count under the old programming first. Then the control word, which sets a low output high: an edge.
  // Then the count, loaded on the next tick, before which the output stays high.
  Advance();
  const std::int64_t now = Now();
  if (!m_tick.OutputHigh(now))
  {
    Request(Irq::Timer);
  }
  m_tick.reload = SquareWave::Reload(_divisor);
  m_tick.originTick = now + 1;
  m_nextEdgeTick = m_tick.NextRisingEdge(now);
}

Cycles Scheduler::NextTickAt() const noexcept
{
  if (m_nextEdgeTick == SquareWave::NEVER)
  {
    return NO_EVENT;
  }
  return static_cast<Cycles>(m_nextEdgeTick) * CYCLES_PER_PIT_TICK;
}

// ── The keys ──

void Scheduler::Inject(std::uint8_t _scanCode)
{
  m_queue.push_back(_scanCode);
  ScheduleDelivery();
}

void Scheduler::ScheduleDelivery() noexcept
{
  if (m_scheduled || m_latchFull || m_queue.empty() || m_held)
  {
    return;
  }
  m_deliverAt = m_clock + DELIVERY_DELAY_CYCLES;
  m_scheduled = true;
}

void Scheduler::HoldKeyboard(bool _held) noexcept
{
  m_held = _held;
  if (_held)
  {
    // Held clear: the latch empties, and a delivery under way starts over once the hold is released.
    m_latchFull = false;
    m_latch = 0;
    m_scheduled = false;
    return;
  }
  ScheduleDelivery();
}

// ── The requests ──

void Scheduler::Advance() noexcept
{
  const std::int64_t now = Now();
  if (now >= m_nextEdgeTick)
  {
    Request(Irq::Timer);
    m_nextEdgeTick = m_tick.NextRisingEdge(now);
  }
  if (m_scheduled && m_clock >= m_deliverAt)
  {
    m_scheduled = false;
    m_latch = m_queue.front();
    m_queue.pop_front();
    m_latchFull = true;
    Request(Irq::Keyboard);
  }
}

Cycles Scheduler::NextEventAt() const noexcept
{
  return std::min(NextTickAt(), NextDeliveryAt());
}

bool Scheduler::InterruptPending() const noexcept
{
  // Fully nested: a request gets in only past every level in service of its priority or higher.
  if (m_inService[Index(Irq::Timer)])
  {
    return false;
  }
  return m_requested[Index(Irq::Timer)] || (m_requested[Index(Irq::Keyboard)] && !m_inService[Index(Irq::Keyboard)]);
}

Irq Scheduler::Take() noexcept
{
  const Irq irq = m_requested[Index(Irq::Timer)] ? Irq::Timer : Irq::Keyboard;
  m_requested[Index(irq)] = false;
  m_inService[Index(irq)] = true;
  return irq;
}

void Scheduler::EndOfInterrupt() noexcept
{
  if (m_inService[Index(Irq::Timer)])
  {
    m_inService[Index(Irq::Timer)] = false;
    return;
  }
  m_inService[Index(Irq::Keyboard)] = false;
}

} // namespace Elite
