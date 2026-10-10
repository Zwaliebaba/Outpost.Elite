#include "pch.h"

#include "Combat.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> CombatEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
