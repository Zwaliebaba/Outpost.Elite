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

/// hyperspaceRingStart (DS:783C in 1987): 3 bytes; delay frames, radius, colour per ring; copied to hyperspaceRings by ResetHyperspaceRings
constexpr std::array<std::array<std::uint8_t, 3>, 10> HYPERSPACE_RING_START = {{
  {0x01, 0x14, 0x02}, // DS:783C
  {0x05, 0x0F, 0x02}, // DS:783F
  {0x08, 0x0B, 0x02}, // DS:7842
  {0x0A, 0x08, 0x01}, // DS:7845
  {0x0B, 0x06, 0x01}, // DS:7848
  {0x0C, 0x06, 0x03}, // DS:784B
  {0x0D, 0x06, 0x03}, // DS:784E
  {0x0E, 0x06, 0x03}, // DS:7851
  {0x0F, 0x06, 0x03}, // DS:7854
  {0x14, 0x06, 0x02}, // DS:7857
}};

} // namespace

} // namespace Elite
