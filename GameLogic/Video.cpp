#include "pch.h"

#include "Video.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> VideoEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
