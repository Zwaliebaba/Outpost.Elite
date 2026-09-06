#pragma once

#include "Ship.h"

#include <array>
#include <cstddef>
#include <cstdint>

/*
 * The tests' byte-level view of a ship, through the codec (Design/Modernize.md slice M1-c).
 *
 * The oracle's `INWK` is bytes at offsets, and a fixture that sweeps "axis X = 0, 3, 6" or "the
 * vector at Y" is written in those offsets because the routine under test is entered with them.
 * `Ship` no longer indexes by number; these two go through `ToBytes` and `FromBytes`, so a fixture
 * keeps its offsets and the layout is written down in one place. A library routine never uses
 * them -- a computed offset there is `PositionAt`, `VectorAt` or `ComponentAt`.
 */
namespace GameLogicTests
{

  /// 6502: STA INWK,X -- write one byte of a ship at an offset.
  inline void PokeShip(Elite::Ship& _ship, std::size_t _offset, std::uint8_t _value)
  {
    std::array<std::uint8_t, Elite::SHIP_BLOCK_SIZE> bytes = _ship.ToBytes();
    bytes[_offset] = _value;
    _ship = Elite::Ship::FromBytes(bytes);
  }

  /// 6502: LDA INWK,X -- read one byte of a ship at an offset.
  [[nodiscard]] inline std::uint8_t PeekShip(const Elite::Ship& _ship, std::size_t _offset)
  {
    return _ship.ToBytes()[_offset];
  }

} // namespace GameLogicTests
