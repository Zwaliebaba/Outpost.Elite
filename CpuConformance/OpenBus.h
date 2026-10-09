// CpuConformance/OpenBus.h
#pragma once

#include "PortBus.h"

#include <cstdint>

namespace CpuConformance
{

/// An I/O space with nothing on it. The suite was recorded with every IN reading 0xFF, which is
/// what a floating 8088 data bus returns, and nothing it checks depends on an OUT.
class OpenBus final : public Machine::PortBus
{
public:
  [[nodiscard]] std::uint8_t In8(std::uint16_t /*_port*/) override
  {
    return 0xFF;
  }

  void Out8(std::uint16_t /*_port*/, std::uint8_t /*_value*/) override {}
};

} // namespace CpuConformance
