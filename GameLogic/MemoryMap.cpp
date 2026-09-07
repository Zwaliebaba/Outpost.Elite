#include "pch.h"

#include "MemoryMap.h"

namespace Elite
{

  void SetMemoryMap(MemoryMap& _map, std::uint8_t _mode) noexcept
  {
    // 6502: STA L1M / LDA l1 / AND #%11111000 / ORA L1M / STA l1 -- the top five bits are left alone.
    _map.requested = _mode;
    _map.port = static_cast<std::uint8_t>((_map.port & 0b11111000u) | _map.requested);
  }

} // namespace Elite
