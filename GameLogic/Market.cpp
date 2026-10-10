#include "pch.h"

#include "Market.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> MarketEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
