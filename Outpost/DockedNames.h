// Outpost/DockedNames.h
#pragma once

#include "StatusView.h"

#include <cstddef>

namespace Outpost
{

/// The names the docked screens print for the game's goods (D82): the 17 commodities in the market's order with their units, the
/// equipment as the status screen and the shipyard list it, and the laser mounts and types.
class DockedNames
{
public:
  /// Commodity _commodity's name, from 0 in the market's order: Food to Alien Items.
  [[nodiscard]] static const wchar_t* Commodity(std::size_t _commodity) noexcept;

  /// Commodity _commodity's unit: t, kg for gold and platinum, g for gem-stones.
  [[nodiscard]] static const wchar_t* Unit(std::size_t _commodity) noexcept;

  /// The name of _kind, as the status screen and the equipment list print it: Cargo Bay Extension; the missiles' is Missiles.
  [[nodiscard]] static const wchar_t* Equipment(Elite::FittedKind _kind) noexcept;

  /// Row _row of the shipyard's list, from 1, as D02 prints it: Fuel (per light year), Missile, then the rest as Equipment names them.
  [[nodiscard]] static const wchar_t* ListItem(std::size_t _row) noexcept;

  /// Mount _mount, as the game numbers them, 0 to 3, the original's fore, aft, right and left, by D03's names: Front, Rear, Right and
  /// Left (D104).
  [[nodiscard]] static const wchar_t* Mount(std::size_t _mount) noexcept;

  /// Laser type _type, 0 to 3: Pulse, Beam, Mining and Military.
  [[nodiscard]] static const wchar_t* LaserType(std::size_t _type) noexcept;
};

} // namespace Outpost
