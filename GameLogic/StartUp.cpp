#include "pch.h"

#include "StartUp.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> StartUpEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
