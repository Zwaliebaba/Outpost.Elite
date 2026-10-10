#include "pch.h"

#include "Scene.h"

#include "DataOverlay.h"

namespace Elite
{

namespace
{

constexpr std::array<NativeEntry, 0> ENTRIES{};

} // namespace

std::span<const NativeEntry> SceneEntries() noexcept
{
  return ENTRIES;
}

} // namespace Elite
