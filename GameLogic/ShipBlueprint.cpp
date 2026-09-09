#include "pch.h"

#include "ShipBlueprint.h"

namespace Elite
{

  namespace
  {
    /// One byte of the region, read INDIRECTLY through `XX0` -- so by ADDRESS. Outside the
    /// region it reads zero, which is what the old `ShipByte` did and what `NO_BLUEPRINT` keeps.
    [[nodiscard]] std::uint8_t RegionByte(std::uint16_t _address) noexcept
    {
      const std::uint32_t offset = static_cast<std::uint32_t>(_address) - SHIP_DATA_BASE;
      return (offset < SHIP_DATA.size()) ? SHIP_DATA[offset] : std::uint8_t{0};
    }

    /// The twenty header bytes at an address, and the three tables they describe.
    [[nodiscard]] Blueprint ParseBlueprint(ShipType _type, std::uint16_t _address) noexcept
    {
      Blueprint blueprint;
      blueprint.type = _type;
      blueprint.address = _address;

      const auto header = [_address](std::uint8_t _byte) noexcept { return RegionByte(static_cast<std::uint16_t>(_address + _byte)); };
      const auto pair = [&header](std::uint8_t _low, std::uint8_t _high) noexcept
      {
        return static_cast<std::uint16_t>(header(_low) | (header(_high) << 8));
      };

      blueprint.cargo = header(SHIP_HEADER_CARGO);
      blueprint.targetArea = pair(SHIP_HEADER_TARGET_AREA_LOW, SHIP_HEADER_TARGET_AREA_HIGH);
      blueprint.edgesOffset = pair(SHIP_HEADER_EDGES_LOW, SHIP_HEADER_EDGES_HIGH);
      blueprint.facesOffset = pair(SHIP_HEADER_FACES_LOW, SHIP_HEADER_FACES_HIGH);
      blueprint.heapBytes = header(SHIP_HEADER_HEAP_BYTES);
      blueprint.laserVertex = header(SHIP_HEADER_LASER_VERTEX);
      blueprint.explosionCount = header(SHIP_HEADER_EXPLOSION_COUNT);
      blueprint.vertexBytes = header(SHIP_HEADER_VERTEX_BYTES);
      blueprint.edgeCount = header(SHIP_HEADER_EDGE_COUNT);
      blueprint.bounty = pair(SHIP_HEADER_BOUNTY_LOW, static_cast<std::uint8_t>(SHIP_HEADER_BOUNTY_LOW + 1u));
      blueprint.faceBytes = header(SHIP_HEADER_FACE_BYTES);
      blueprint.visibility = header(SHIP_HEADER_VISIBILITY);
      blueprint.maxEnergy = header(SHIP_HEADER_MAX_ENERGY);
      blueprint.maxSpeed = header(SHIP_HEADER_MAX_SPEED);
      blueprint.normalShifts = header(SHIP_HEADER_NORMAL_SHIFTS);
      blueprint.weapons = header(SHIP_HEADER_WEAPONS);

      /*
       * The tables are spans over the region at the ADDRESS the header names: `XX0` plus the offset,
       * in sixteen-bit arithmetic that wraps -- and for two of the thirty-three it does wrap. The
       * Splinter's edges offset is &FD78 and the Thargon's &E7E6, so their edge tables sit BEFORE
       * them: the Splinter draws with the Escape Pod's six edges and the Thargon with the Canister's
       * fifteen, shared. The Splinter's FACES, at +68, start eight bytes into the Shuttle's header,
       * which follows it sixty bytes on -- so that one table really is read out of the next
       * blueprint, and that is what `ShipDataTests` measures as a header extent that overruns the
       * gap. Every table the data names lands inside the region, which the tests prove against the
       * assembled original; one that did not would be clamped to the region's edge here rather than
       * read past it.
       */
      const std::span<const std::uint8_t> region{SHIP_DATA.data(), SHIP_DATA.size()};
      const auto table = [&region](std::uint16_t _absolute, std::size_t _length) noexcept
      {
        const std::size_t from = static_cast<std::uint32_t>(_absolute) - SHIP_DATA_BASE;
        if (from >= region.size())
        {
          return region.subspan(region.size(), 0);
        }
        const std::size_t length = (from + _length <= region.size()) ? _length : region.size() - from;
        return region.subspan(from, length);
      };
      blueprint.vertices = table(static_cast<std::uint16_t>(_address + SHIP_HEADER_SIZE), blueprint.vertexBytes);
      blueprint.edges = table(static_cast<std::uint16_t>(_address + blueprint.edgesOffset), 4u * blueprint.edgeCount);
      blueprint.faces = table(static_cast<std::uint16_t>(_address + blueprint.facesOffset), blueprint.faceBytes);
      return blueprint;
    }

    /// The table, parsed once: entry 0 is the empty slot and stays `NO_BLUEPRINT`.
    [[nodiscard]] const std::array<Blueprint, SHIP_TYPE_COUNT + 1u>& Blueprints() noexcept
    {
      static const std::array<Blueprint, SHIP_TYPE_COUNT + 1u> BLUEPRINTS = []() noexcept
      {
        std::array<Blueprint, SHIP_TYPE_COUNT + 1u> table{};
        for (std::uint8_t type = 1; type <= SHIP_TYPE_COUNT; ++type)
        {
          // The type doubled and used as an index, then two bytes fetched below the table's
          // base. The doubling is the two bytes an address takes; the -1 and -2 are what make the
          // table one-based.
          const std::uint16_t entry = static_cast<std::uint16_t>(SHIP_DATA_BASE + (type - 1u) * 2u);
          const std::uint16_t address = static_cast<std::uint16_t>(RegionByte(entry) | (RegionByte(static_cast<std::uint16_t>(entry + 1u)) << 8));
          table[type] = (address == 0u) ? NO_BLUEPRINT : ParseBlueprint(TypeOf(type), address);
        }
        return table;
      }();
      return BLUEPRINTS;
    }
  } // namespace

  const Blueprint* BlueprintOf(ShipType _shipType) noexcept
  {
    if (Byte(_shipType) == 0u || Byte(_shipType) > SHIP_TYPE_COUNT)
    {
      return nullptr;
    }
    const Blueprint& blueprint = Blueprints()[Byte(_shipType)];
    return (blueprint.address == 0u) ? nullptr : &blueprint;
  }

  const Blueprint* BlueprintAt(std::uint16_t _address) noexcept
  {
    if (_address == 0u)
    {
      return &NO_BLUEPRINT;
    }
    for (const Blueprint& blueprint : Blueprints())
    {
      if (blueprint.address == _address)
      {
        return &blueprint;
      }
    }
    return nullptr;
  }

  std::uint8_t DefaultNewbFor(ShipType _shipType) noexcept
  {
    return RegionByte(static_cast<std::uint16_t>(SHIP_DEFAULT_FLAGS + Byte(_shipType) - 1u));
  }

  KillWorth KillWorthFor(ShipType _shipType) noexcept
  {
    const std::uint16_t fraction = static_cast<std::uint16_t>(SHIP_KILL_FRACTION + Byte(_shipType) - 1u);
    return {RegionByte(fraction), RegionByte(static_cast<std::uint16_t>(fraction + SHIP_TYPE_COUNT))};
  }

} // namespace Elite
