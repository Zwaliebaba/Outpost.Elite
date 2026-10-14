// GameLogic/HyperspaceTables.h
#pragma once

#include <array>
#include <cstdint>

namespace Elite
{

// The tables and constants this subsystem reads, which nothing writes (D33, D38): source since the 1987 image went (Phase 7,
// step 5).
// Hyperspace.cpp alone includes this, so they are in an unnamed namespace (D38).
namespace
{

/// hyperspaceRingStart: 3 bytes; delay frames, radius, colour per ring; copied to hyperspaceRings by ResetHyperspaceRings
constexpr std::array<std::array<std::uint8_t, 3>, 10> HYPERSPACE_RING_START = {{
  {0x01, 0x14, 0x02},
  {0x05, 0x0F, 0x02},
  {0x08, 0x0B, 0x02},
  {0x0A, 0x08, 0x01},
  {0x0B, 0x06, 0x01},
  {0x0C, 0x06, 0x03},
  {0x0D, 0x06, 0x03},
  {0x0E, 0x06, 0x03},
  {0x0F, 0x06, 0x03},
  {0x14, 0x06, 0x02},
}};

} // namespace

} // namespace Elite
