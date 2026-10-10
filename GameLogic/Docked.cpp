#include "pch.h"

#include "Docked.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> DockedEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
