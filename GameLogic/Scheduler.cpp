#include "pch.h"

#include "Scheduler.h"

#include <algorithm>

namespace Elite
{

Scheduler::Scheduler(const Milliseconds& _clock)
  : m_clock(_clock)
{
}

// ── The tick ──

void Scheduler::SetTickRunning(bool _running) noexcept
{
  m_nextTickAt = _running ? m_clock + TICK_MILLISECONDS : NO_EVENT;
}

// ── The keys ──

void Scheduler::Inject(const KeyEvent& _event)
{
  m_queue.push_back(_event);
  ScheduleDelivery();
}

KeyEvent Scheduler::TakeKey() noexcept
{
  m_latchFull = false;
  ScheduleDelivery();
  return m_latch;
}

void Scheduler::ScheduleDelivery() noexcept
{
  if (m_scheduled || m_latchFull || m_queue.empty())
  {
    return;
  }
  m_deliverAt = m_clock + 1;
  m_scheduled = true;
}

// ── The requests ──

void Scheduler::Advance() noexcept
{
  if (m_clock >= m_nextTickAt)
  {
    Request(Irq::Timer);
    m_nextTickAt = m_clock + TICK_MILLISECONDS;
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

Milliseconds Scheduler::NextEventAt() const noexcept
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
