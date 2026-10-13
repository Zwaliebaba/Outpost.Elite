#include "pch.h"

#include "ToneTimer.h"

#include "Speaker.h"

namespace Elite
{

namespace
{

// The BIOS's programming, which is this channel's power-on state (ToneTimer.h).
constexpr std::uint16_t BEEP_DIVISOR = 0x0533;

} // namespace

ToneTimer::ToneTimer(const Cycles& _clock, Speaker& _speaker)
  : m_clock(_clock),
    m_speaker(_speaker)
{
  m_wave.loaded = true;
  m_wave.reload = BEEP_DIVISOR;
  m_wave.gate = false;
  m_speaker.SetChannel2(m_wave);
}

void ToneTimer::Program(std::uint16_t _divisor)
{
  // The control word, which leaves the channel unloaded until its count; then the count, loaded on the next tick. Both reach the
  // speaker, which keeps the second, as they come at one cycle.
  m_wave.loaded = false;
  m_speaker.SetChannel2(m_wave);
  m_wave.reload = SquareWave::Reload(_divisor);
  m_wave.originTick = SquareWave::TickAt(m_clock) + 1;
  m_wave.loaded = true;
  m_speaker.SetChannel2(m_wave);
}

void ToneTimer::SetGate(bool _high)
{
  if (m_wave.gate == _high)
  {
    return;
  }
  m_wave.gate = _high;
  if (_high && m_wave.loaded)
  {
    m_wave.reload = m_wave.CountRegister();
    m_wave.originTick = SquareWave::TickAt(m_clock) + 1;
  }
  m_speaker.SetChannel2(m_wave);
}

} // namespace Elite
