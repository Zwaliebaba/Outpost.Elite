#include "pch.h"

#include "Keyboard.h"

#include "InterruptController.h"
#include "IntervalTimer.h"
#include "Speaker.h"

namespace Elite
{

namespace
{

constexpr std::uint8_t KEYBOARD_IRQ = 1;
constexpr std::uint8_t TIMER_GATE = 0x01;
constexpr std::uint8_t SPEAKER_BITS = 0x03;
constexpr std::uint8_t CLEAR_KEYBOARD = 0x80;

} // namespace

Keyboard::Keyboard(const Cycles& _clock, InterruptController& _controller, IntervalTimer& _timer, Speaker& _speaker)
  : m_clock(_clock),
    m_controller(_controller),
    m_timer(_timer),
    m_speaker(_speaker)
{
  // The system control port's power-on value drives the gate and the speaker like any other write.
  m_timer.SetChannel2Gate((m_systemControl & TIMER_GATE) != 0);
  m_speaker.SetSystemControl(m_systemControl);
}

void Keyboard::Inject(std::uint8_t _scanCode)
{
  m_queue.push_back(_scanCode);
  Schedule();
}

void Keyboard::Schedule() noexcept
{
  if (m_scheduled || m_latchFull || m_queue.empty() || (m_systemControl & CLEAR_KEYBOARD) != 0)
  {
    return;
  }
  m_deliverAt = m_clock + DELIVERY_DELAY_CYCLES;
  m_scheduled = true;
}

void Keyboard::Advance()
{
  if (!m_scheduled || m_clock < m_deliverAt)
  {
    return;
  }
  m_scheduled = false;
  m_latch = m_queue.front();
  m_queue.pop_front();
  m_latchFull = true;
  m_controller.RaiseIrq(KEYBOARD_IRQ);
}

void Keyboard::SetSystemControl(std::uint8_t _value)
{
  const auto changed = static_cast<std::uint8_t>(m_systemControl ^ _value);
  m_systemControl = _value;
  if ((changed & TIMER_GATE) != 0)
  {
    m_timer.SetChannel2Gate((_value & TIMER_GATE) != 0);
  }
  if ((changed & SPEAKER_BITS) != 0)
  {
    m_speaker.SetSystemControl(_value);
  }
  if ((_value & CLEAR_KEYBOARD) != 0)
  {
    // Held clear: the latch empties, and a delivery under way starts over once the hold is released.
    m_latchFull = false;
    m_latch = 0;
    m_scheduled = false;
  }
  else
  {
    Schedule();
  }
}

} // namespace Elite
