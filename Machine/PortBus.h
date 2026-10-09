// Machine/PortBus.h
#pragma once

#include <cstdint>

namespace Machine
{

/// The 8088's I/O space: what IN and OUT talk to. The PC's devices (the PIT, the PIC, the keyboard
/// controller, the CGA, the speaker gate) sit behind an implementation of this.
///
/// The 8088 has an 8-bit data bus, so a 16-bit IN or OUT is two byte transfers, to the port and to
/// the port after it. That is the default here; a device that decodes 16-bit ports differently can
/// override it.
class PortBus
{
public:
  PortBus() = default;
  PortBus(const PortBus&) = delete;
  PortBus& operator=(const PortBus&) = delete;
  virtual ~PortBus() = default;

  [[nodiscard]] virtual std::uint8_t In8(std::uint16_t _port) = 0;
  virtual void Out8(std::uint16_t _port, std::uint8_t _value) = 0;

  [[nodiscard]] virtual std::uint16_t In16(std::uint16_t _port)
  {
    const std::uint8_t low = In8(_port);
    const std::uint8_t high = In8(static_cast<std::uint16_t>(_port + 1));
    return static_cast<std::uint16_t>(low | (high << 8));
  }

  virtual void Out16(std::uint16_t _port, std::uint16_t _value)
  {
    Out8(_port, static_cast<std::uint8_t>(_value & 0xFF));
    Out8(static_cast<std::uint16_t>(_port + 1), static_cast<std::uint8_t>(_value >> 8));
  }
};

} // namespace Machine
