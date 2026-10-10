#include "pch.h"

#include "Flight.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> FlightEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
