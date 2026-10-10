#include "pch.h"

#include "Ai.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> AiEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
