#include "pch.h"

#include "Text.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> TextEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
