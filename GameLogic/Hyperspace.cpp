#include "pch.h"

#include "Hyperspace.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> HyperspaceEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
