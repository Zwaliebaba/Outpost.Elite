#include "pch.h"

#include "SaveLoad.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> SaveLoadEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
