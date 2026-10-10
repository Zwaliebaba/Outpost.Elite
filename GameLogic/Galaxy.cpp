#include "pch.h"

#include "Galaxy.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> GalaxyEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
