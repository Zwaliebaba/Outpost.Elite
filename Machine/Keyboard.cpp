#include "pch.h"

#include "Keyboard.h"

#include "Pic.h"
#include "Pit.h"
#include "Speaker.h"

namespace Machine
{

namespace
{

constexpr std::uint8_t KEYBOARD_IRQ = 1;
constexpr std::uint8_t PORT_B_TIMER_GATE = 0x01;
constexpr std::uint8_t PORT_B_SPEAKER_BITS = 0x03;
constexpr std::uint8_t PORT_B_CLEAR_KEYBOARD = 0x80;
constexpr std::uint8_t PORT_C_TIMER_OUTPUT = 0x20;
constexpr std::uint8_t OPEN_BUS = 0xFF;

} // namespace

Keyboard::Keyboard(const Cycles& _clock, Pic& _pic, Pit& _pit, Speaker& _speaker)
  : m_clock(_clock),
    m_pic(_pic),
    m_pit(_pit),
    m_speaker(_speaker)
{
  // Port 0x61's power-on value drives the gate and the speaker like any other write.
  m_pit.SetChannel2Gate((m_portB & PORT_B_TIMER_GATE) != 0);
  m_speaker.SetPortB(m_portB);
}

void Keyboard::Inject(std::uint8_t _scanCode)
{
  m_queue.push_back(_scanCode);
  Schedule();
}

void Keyboard::Schedule() noexcept
{
  if (m_scheduled || m_latchFull || m_queue.empty() || (m_portB & PORT_B_CLEAR_KEYBOARD) != 0)
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
  m_pic.RaiseIrq(KEYBOARD_IRQ);
}

std::uint8_t Keyboard::In8(std::uint16_t _port)
{
  switch (_port & 3u)
  {
  case 0:
    return m_latchFull ? m_latch : 0;
  case 1:
    return m_portB;
  case 2:
    return m_pit.OutputAt(2, m_clock) ? PORT_C_TIMER_OUTPUT : 0;
  default:
    return OPEN_BUS;
  }
}

void Keyboard::Out8(std::uint16_t _port, std::uint8_t _value)
{
  if ((_port & 3u) == 1)
  {
    WritePortB(_value);
  }
  // Port A and port C are inputs, and the mode register is fixed.
}

void Keyboard::WritePortB(std::uint8_t _value)
{
  const auto changed = static_cast<std::uint8_t>(m_portB ^ _value);
  m_portB = _value;
  if ((changed & PORT_B_TIMER_GATE) != 0)
  {
    m_pit.SetChannel2Gate((_value & PORT_B_TIMER_GATE) != 0);
  }
  if ((changed & PORT_B_SPEAKER_BITS) != 0)
  {
    m_speaker.SetPortB(_value);
  }
  if ((_value & PORT_B_CLEAR_KEYBOARD) != 0)
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

} // namespace Machine
