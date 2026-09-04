#include "pch.h"

#include "ShipBlueprint.h"

namespace Elite
{

  std::uint8_t ShipByte(std::uint16_t _address) noexcept
  {
    const std::uint32_t offset = static_cast<std::uint32_t>(_address) - SHIP_DATA_BASE;
    return (offset < SHIP_DATA.size()) ? SHIP_DATA[offset] : std::uint8_t{0};
  }

  std::uint16_t BlueprintAddress(std::uint8_t _shipType) noexcept
  {
    if (_shipType == 0 || _shipType > SHIP_TYPE_COUNT)
    {
      return 0;
    }

    // 6502: ASL A / TAY / LDA XX21-1,Y / ... / LDA XX21-2,Y. The doubling is the two bytes an
    // address takes; the -1 and -2 are what make the table one-based.
    const std::uint16_t entry = static_cast<std::uint16_t>(SHIP_DATA_BASE + (_shipType - 1) * 2);
    const std::uint8_t low = ShipByte(entry);
    const std::uint8_t high = ShipByte(static_cast<std::uint16_t>(entry + 1));
    return static_cast<std::uint16_t>(low | (high << 8));
  }

  std::uint16_t ShipBlueprintExtent(std::uint16_t _blueprint) noexcept
  {
    const std::uint8_t vertexBytes = ShipByte(static_cast<std::uint16_t>(_blueprint + SHIP_HEADER_VERTEX_BYTES));
    const std::uint8_t edgeCount = ShipByte(static_cast<std::uint16_t>(_blueprint + SHIP_HEADER_EDGE_COUNT));
    const std::uint8_t faceBytes = ShipByte(static_cast<std::uint16_t>(_blueprint + SHIP_HEADER_FACE_BYTES));

    return static_cast<std::uint16_t>(SHIP_HEADER_SIZE + vertexBytes + 4 * edgeCount + faceBytes);
  }

} // namespace Elite
