// Outpost/DockedNames.h
#pragma once

#include "StatusView.h"

#include <cstddef>

namespace Outpost
{

/// The names the docked screens print for the game's goods (D82): the 17 commodities in the market's order with their units, and
/// the equipment as the status screen lists it.
class DockedNames
{
public:
  /// Commodity _commodity's name, from 0 in the market's order: Food to Alien Items.
  [[nodiscard]] static const wchar_t* Commodity(std::size_t _commodity) noexcept;

  /// Commodity _commodity's unit: t, kg for gold and platinum, g for gem-stones.
  [[nodiscard]] static const wchar_t* Unit(std::size_t _commodity) noexcept;

  /// The name of _kind, as the status screen and the equipment list print it: Cargo Bay Extension; the missiles' is Missiles.
  [[nodiscard]] static const wchar_t* Equipment(Elite::FittedKind _kind) noexcept;
};

} // namespace Outpost
