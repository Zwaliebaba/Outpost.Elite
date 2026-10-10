#include "pch.h"

#include "PortRouter.h"

#include <algorithm>
#include <format>

namespace Machine
{

namespace
{

constexpr std::size_t PORT_COUNT = 0x10000;
constexpr std::size_t MAXIMUM_DEVICES = 255;
constexpr std::uint8_t OPEN_BUS = 0xFF;

} // namespace

PortRouter::PortRouter()
  : m_owner(PORT_COUNT, 0),
    m_devices(1, nullptr)
{
}

bool PortRouter::Map(std::uint16_t _first, std::uint16_t _last, PortBus& _device)
{
  if (_last < _first || m_devices.size() > MAXIMUM_DEVICES)
  {
    return false;
  }
  const auto begin = m_owner.begin() + _first;
  const auto end = m_owner.begin() + _last + 1;
  if (std::any_of(begin, end, [](std::uint8_t _owner) { return _owner != 0; }))
  {
    return false;
  }
  const auto index = static_cast<std::uint8_t>(m_devices.size());
  m_devices.push_back(&_device);
  std::fill(begin, end, index);
  return true;
}

void PortRouter::StartReplay(std::span<const Access> _expected) noexcept
{
  m_expected = _expected;
  m_replayed = 0;
  m_replaying = true;
  m_replayDifference.clear();
}

std::string PortRouter::ReplayDifference() const
{
  if (m_replayDifference.empty() && m_replayed != m_expected.size())
  {
    const Access& missing = m_expected[m_replayed];
    return std::format("no {} of port {:X}h, which the original made after {} accesses", missing.write ? "write" : "read", missing.port,
                       m_replayed);
  }
  return m_replayDifference;
}

bool PortRouter::Replayed(const Access& _access)
{
  if (!m_replayDifference.empty())
  {
    return false;
  }
  if (m_replayed >= m_expected.size())
  {
    m_replayDifference =
      std::format("{} of port {:X}h after the original's {} accesses", _access.write ? "write" : "read", _access.port, m_expected.size());
    return false;
  }
  const Access& expected = m_expected[m_replayed];
  if (expected.port != _access.port || expected.write != _access.write || expected.word != _access.word ||
      (_access.write && expected.value != _access.value))
  {
    m_replayDifference = std::format("access {}: {} of port {:X}h value {:X}h, where the original made a {} of port {:X}h value {:X}h",
                                     m_replayed + 1, _access.write ? "write" : "read", _access.port, _access.value,
                                     expected.write ? "write" : "read", expected.port, expected.value);
    return false;
  }
  ++m_replayed;
  return true;
}

std::uint8_t PortRouter::In8(std::uint16_t _port)
{
  if (m_replaying)
  {
    const std::size_t index = m_replayed;
    return Replayed(Access{_port, 0, false, false}) ? static_cast<std::uint8_t>(m_expected[index].value) : OPEN_BUS;
  }
  PortBus* device = DeviceAt(_port);
  std::uint8_t value = OPEN_BUS;
  if (device == nullptr)
  {
    ++m_unmapped[_port].reads;
  }
  else
  {
    value = device->In8(_port);
  }
  if (m_log != nullptr)
  {
    m_log->push_back(Access{_port, value, false, false});
  }
  return value;
}

void PortRouter::Out8(std::uint16_t _port, std::uint8_t _value)
{
  ++m_writes;
  if (m_replaying)
  {
    (void)Replayed(Access{_port, _value, true, false});
    return;
  }
  if (m_log != nullptr)
  {
    m_log->push_back(Access{_port, _value, true, false});
  }
  PortBus* device = DeviceAt(_port);
  if (device == nullptr)
  {
    ++m_unmapped[_port].writes;
    return;
  }
  device->Out8(_port, _value);
}

std::uint16_t PortRouter::In16(std::uint16_t _port)
{
  PortBus* device = DeviceAt(_port);
  if (m_replaying && device != nullptr && device == DeviceAt(static_cast<std::uint16_t>(_port + 1)))
  {
    const std::size_t index = m_replayed;
    return Replayed(Access{_port, 0, false, true}) ? m_expected[index].value : static_cast<std::uint16_t>(0xFFFF);
  }
  if (device != nullptr && device == DeviceAt(static_cast<std::uint16_t>(_port + 1)))
  {
    const std::uint16_t value = device->In16(_port);
    if (m_log != nullptr)
    {
      m_log->push_back(Access{_port, value, false, true});
    }
    return value;
  }
  return PortBus::In16(_port);
}

void PortRouter::Out16(std::uint16_t _port, std::uint16_t _value)
{
  PortBus* device = DeviceAt(_port);
  if (device != nullptr && device == DeviceAt(static_cast<std::uint16_t>(_port + 1)))
  {
    ++m_writes;
    if (m_replaying)
    {
      (void)Replayed(Access{_port, _value, true, true});
      return;
    }
    if (m_log != nullptr)
    {
      m_log->push_back(Access{_port, _value, true, true});
    }
    device->Out16(_port, _value);
    return;
  }
  PortBus::Out16(_port, _value);
}

} // namespace Machine
