#include "pch.h"

#include "Input.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> InputEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
