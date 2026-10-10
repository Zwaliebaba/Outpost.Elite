#include "pch.h"

#include "Ships.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> ShipsEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
