#include "pch.h"

#include "MemoryMap.h"

namespace Elite
{

  void SetMemoryMap(MemoryMap& _map, std::uint8_t _mode) noexcept
  {
    // The requested mode is stored, then merged into the processor port's low three bits --
    // the top five are left alone.
    _map.requested = _mode;
    _map.port = static_cast<std::uint8_t>((_map.port & 0b11111000u) | _map.requested);
  }

} // namespace Elite
